#include "EnginePCH.h"
#include "RenderResourceManager.h"

#include "RenderCommand.h"

#include "RenderUtil.h"
#include "Render/ParticleCurveTexture.h"
#include "Render/Material.h"
#include "Render/MaterialRenderProxy.h"
#include "Asset/AssetManager.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace
{
	// 파일 본문과 #include "..."로 끌어온 파일들을 이어 붙인다. 진입점이 include한 파일에만 있는 셰이더
	// (예: EditorArrowShader.hlsl → UberLit.hlsl)도 VS/PS 셰이더로 알아보려면 include까지 봐야 한다.
	void AppendShaderSource(const fs::path& Path, std::set<fs::path>& Visited, std::string& Out)
	{
		std::error_code Ec;
		const fs::path Canonical = fs::weakly_canonical(Path, Ec);
		if (Ec || !Visited.insert(Canonical).second) return;
		std::ifstream File(Path, std::ios::binary);
		if (!File) return;
		const std::string Source(std::istreambuf_iterator<char>{ File }, std::istreambuf_iterator<char>{});
		Out += Source;
		for (size_t Pos = Source.find("#include"); Pos != std::string::npos; Pos = Source.find("#include", Pos + 8))
		{
			const size_t Open = Source.find('"', Pos);
			const size_t LineEnd = Source.find('\n', Pos);
			if (Open == std::string::npos || (LineEnd != std::string::npos && Open > LineEnd)) continue;
			const size_t Close = Source.find('"', Open + 1);
			if (Close == std::string::npos) continue;
			AppendShaderSource(Path.parent_path() / Source.substr(Open + 1, Close - Open - 1), Visited, Out);
		}
	}

	const FMaterialExpressionNode* FindGraphNode(const UMaterial& Material, int32 NodeId)
	{
		for (const FMaterialExpressionNode& Node : Material.ExpressionNodes)
		{
			if (Node.Id == NodeId)
			{
				return &Node;
			}
		}

		return nullptr;
	}

	const FMaterialExpressionConnection* FindGraphInput(const UMaterial& Material, int32 NodeId, int32 Pin)
	{
		for (const FMaterialExpressionConnection& Connection : Material.ExpressionConnections)
		{
			if (Connection.InputNodeId == NodeId && Connection.InputPin == Pin)
			{
				return &Connection;
			}
		}

		return nullptr;
	}

	FString GraphNodeVariable(int32 NodeId)
	{
		return "GraphNode_" + std::to_string(NodeId);
	}

	FString HlslFloat(float Value)
	{
		std::ostringstream Stream;
		Stream << std::fixed << std::setprecision(6) << Value << "f";
		return Stream.str();
	}

	const char* GetMaterialHlslType(EMaterialValueType Type)
	{
		switch (Type)
		{
		case EMaterialValueType::Scalar:
			return "float";
		case EMaterialValueType::Vector2:
			return "float2";
		case EMaterialValueType::Vector3:
			return "float3";
		case EMaterialValueType::Vector4:
			return "float4";
		default:
			return nullptr;
		}
	}

	FString GetMaterialGraphParameterKey(const FString& Name, EMaterialExpressionType Type)
	{
		return Name + "#" + std::to_string(static_cast<uint8>(Type));
	}


	
	bool GenerateMaterialGraphSource(const UMaterial& Material, FString& OutSource, FString& OutError)
	{
		if (!Material.ValidateExpressionGraph(OutError))
		{
			return false;
		}

		std::unordered_map<int32, int32> TextureIndices;
		std::unordered_map<FString, int32> ParameterIndices;

		TArray<int32> TextureNodeIds;
		TArray<FMaterialGraphParameter> GraphParameters;

		Material.GetGraphParameterLayout(GraphParameters);

		for (int32 Index = 0; Index < GraphParameters.Num(); ++Index)
		{
			ParameterIndices[GetMaterialGraphParameterKey(GraphParameters[Index].Name, GraphParameters[Index].Type)] = Index;
		}

		TArray<int32> ReachableNodeIds;
		Material.GetReachableExpressionNodeIds(ReachableNodeIds);

		std::unordered_set<int32> ReachableNodeSet;
		for (int32 NodeId : ReachableNodeIds)
		{
			ReachableNodeSet.insert(NodeId);
		}

		for (const FMaterialExpressionNode& Node : Material.ExpressionNodes)
		{
			if (!ReachableNodeSet.contains(Node.Id))
			{
				continue;
			}

			if (Node.Type != EMaterialExpressionType::TextureSample &&
				Node.Type != EMaterialExpressionType::TextureSampleParameter2D)
			{
				continue;
			}

			TextureIndices[Node.Id] = TextureNodeIds.Num();
			TextureNodeIds.Add(Node.Id);
		}

		if (TextureNodeIds.Num() > MaxMaterialGraphTextures ||
			GraphParameters.Num() > MaxMaterialGraphParameters)
		{
			OutError = "Material Graph resource limit exceeded.";
			return false;
		}

		TArray<const FMaterialExpressionNode*> EvaluationOrder;
		std::unordered_map<int32, uint8> VisitState;

		const auto Visit = [&](auto&& Self, int32 NodeId) -> bool
		{
			if (VisitState[NodeId] == 1)
			{
				return false;
			}

			if (VisitState[NodeId] == 2)
			{
				return true;
			}

			VisitState[NodeId] = 1;

			for (const FMaterialExpressionConnection& Connection : Material.ExpressionConnections)
			{
				if (Connection.InputNodeId == NodeId && !Self(Self, Connection.OutputNodeId))
				{
					return false;
				}
			}

			const FMaterialExpressionNode* Node = FindGraphNode(Material, NodeId);

			if (!Node)
			{
				return false;
			}

			if (Node->Type != EMaterialExpressionType::MaterialOutput)
			{
				EvaluationOrder.Add(Node);
			}

			VisitState[NodeId] = 2;
			return true;
		};

		if (!Visit(Visit, Material.OutputNodeId))
		{
			OutError = "Failed to build Material Graph evaluation order.";
			return false;
		}

		const auto FindInputConnection = [&](int32 NodeId, int32 Pin) -> const FMaterialExpressionConnection*
		{
			for (const FMaterialExpressionConnection& Connection : Material.ExpressionConnections)
			{
				if (Connection.InputNodeId == NodeId && Connection.InputPin == Pin)
				{
					return &Connection;
				}
			}

			return nullptr;
		};

		const auto GetInputType = [&](int32 NodeId, int32 Pin)
		{
			const FMaterialExpressionConnection* Connection = FindInputConnection(NodeId, Pin);
			return Connection ? Material.GetExpressionOutputType(Connection->OutputNodeId, Connection->OutputPin) : EMaterialValueType::Unknown;
		};

		const auto GetInputVariable = [&](int32 NodeId, int32 Pin, EMaterialValueType TargetType = EMaterialValueType::Unknown) -> FString
		{
			const FMaterialExpressionConnection* Connection = FindInputConnection(NodeId, Pin);

			if (!Connection)
			{
				return FString();
			}

			const FString Variable = GraphNodeVariable(Connection->OutputNodeId);
			const EMaterialValueType SourceType = Material.GetExpressionOutputType(Connection->OutputNodeId, Connection->OutputPin);

			if (TargetType == EMaterialValueType::Unknown || SourceType == TargetType || SourceType != EMaterialValueType::Scalar)
			{
				return Variable;
			}

			switch (TargetType)
			{
			case EMaterialValueType::Vector2:
				return "float2(" + Variable + ", " + Variable + ")";

			case EMaterialValueType::Vector3:
				return "float3(" + Variable + ", " + Variable + ", " + Variable + ")";

			case EMaterialValueType::Vector4:
				return "float4(" + Variable + ", " + Variable + ", " + Variable + ", " + Variable + ")";

			default:
				return Variable;
			}
		};

		OutSource.clear();

		OutSource += "struct FMaterialGraphOutput\n";
		OutSource += "{\n";
		OutSource += "    float4 BaseColor;\n";
		OutSource += "    float3 SpecularColor;\n";
		OutSource += "    float Shininess;\n";
		OutSource += "    float3 EmissiveColor;\n";
		OutSource += "    float EmissiveFactor;\n";
		OutSource += "    float Metallic;\n";
		OutSource += "    float Roughness;\n";
		OutSource += "    float OpacityMask;\n";
		OutSource += "    float3 Normal;\n";
		OutSource += "    float AmbientOcclusion;\n";
		OutSource += "};\n";

		OutSource += "cbuffer MaterialGraphParameters : register(b4)\n";
		OutSource += "{\n";
		OutSource += "    float4 GraphParameterValues[16];\n";
		OutSource += "};\n";

		for (int32 Index = 0; Index < TextureNodeIds.Num(); ++Index)
		{
			OutSource += "Texture2D GraphTexture" + std::to_string(Index) + " : register(t" + std::to_string(Index + 2) + ");\n";
		}

		for (const FMaterialExpressionNode* Node : EvaluationOrder)
		{
			if (Node->Type != EMaterialExpressionType::Custom)
			{
				continue;
			}

			const FMaterialExpressionConnection* InputA = FindGraphInput(Material, Node->Id, 0);
			const FMaterialExpressionConnection* InputB = FindGraphInput(Material, Node->Id, 1);

			if (!InputA || !InputB)
			{
				OutError = "Custom Expression requires two connected inputs.";
				return false;
			}

			const EMaterialValueType TypeA = Material.GetExpressionOutputType(InputA->OutputNodeId, InputA->OutputPin);
			const EMaterialValueType TypeB = Material.GetExpressionOutputType(InputB->OutputNodeId, InputB->OutputPin);

			const char* HlslTypeA = GetMaterialHlslType(TypeA);
			const char* HlslTypeB = GetMaterialHlslType(TypeB);
			const char* HlslOutputType = GetMaterialHlslType(Node->CustomOutputType);

			if (!HlslTypeA || !HlslTypeB || !HlslOutputType)
			{
				OutError = "Custom Expression has an unsupported input or output type.";
				return false;
			}

			OutSource += FString(HlslOutputType) + " EvaluateCustomNode_" + std::to_string(Node->Id);
			OutSource += "(" + FString(HlslTypeA) + " A, " + FString(HlslTypeB) + " B)\n";
			OutSource += "{\n";
			OutSource += "    return (" + Node->CustomCode + ");\n";
			OutSource += "}\n";
		}

		OutSource += "float MaterialGraphHash21(float2 P)\n";
		OutSource += "{\n";
		OutSource += "    P = frac(P * float2(123.34f, 456.21f));\n";
		OutSource += "    P += dot(P, P + 45.32f);\n";
		OutSource += "    return frac(P.x * P.y);\n";
		OutSource += "}\n";
		OutSource += "float MaterialGraphValueNoise(float2 P)\n";
		OutSource += "{\n";
		OutSource += "    float2 I = floor(P);\n";
		OutSource += "    float2 F = frac(P);\n";
		OutSource += "    F = F * F * (3.0f - 2.0f * F);\n";
		OutSource += "    float A = MaterialGraphHash21(I);\n";
		OutSource += "    float B = MaterialGraphHash21(I + float2(1.0f, 0.0f));\n";
		OutSource += "    float C = MaterialGraphHash21(I + float2(0.0f, 1.0f));\n";
		OutSource += "    float D = MaterialGraphHash21(I + float2(1.0f, 1.0f));\n";
		OutSource += "    return lerp(lerp(A, B, F.x), lerp(C, D, F.x), F.y);\n";
		OutSource += "}\n";
		OutSource += "float MaterialGraphNoise2D(float2 P)\n";
		OutSource += "{\n";
		OutSource += "    return MaterialGraphValueNoise(P) * 0.65f + MaterialGraphValueNoise(P * 2.03f) * 0.35f;\n";
		OutSource += "}\n";

		OutSource += "FMaterialGraphOutput EvaluateMaterialGraph(float2 UV, float3 WorldPosition, float3 CameraVectorWS, float3 VertexNormalWS, float4 VertexColor, float Time)\n";
		OutSource += "{\n";
		OutSource += "    FMaterialGraphOutput Result = (FMaterialGraphOutput)0;\n";

		for (const FMaterialExpressionNode* Node : EvaluationOrder)
		{
			const FString Variable = GraphNodeVariable(Node->Id);

			const EMaterialValueType OutputType = Material.GetExpressionOutputType(Node->Id, 0);
			const char* HlslType = GetMaterialHlslType(OutputType);

			if (!HlslType)
			{
				OutError = "Cannot resolve expression output type for node " + std::to_string(Node->Id);
				return false;
			}

			const EMaterialValueType AType = GetInputType(Node->Id, 0);
			const EMaterialValueType BType = GetInputType(Node->Id, 1);
			const EMaterialValueType CType = GetInputType(Node->Id, 2);

			const FString A = GetInputVariable(Node->Id, 0, OutputType);
			const FString B = GetInputVariable(Node->Id, 1, OutputType);
			const FString C = GetInputVariable(Node->Id, 2, OutputType);

			switch (Node->Type)
			{
			case EMaterialExpressionType::Constant:
				OutSource += "    float " + Variable + " = " + HlslFloat(Node->ScalarValue) + ";\n";
				break;

			case EMaterialExpressionType::Constant2Vector:
				OutSource += "    float2 " + Variable + " = float2(" + HlslFloat(Node->VectorValue.X) + ", " + HlslFloat(Node->VectorValue.Y) + ");\n";
				break;

			case EMaterialExpressionType::Constant3Vector:
				OutSource += "    float3 " + Variable + " = float3(" + HlslFloat(Node->VectorValue.X) + ", " + HlslFloat(Node->VectorValue.Y) + ", " + HlslFloat(Node->VectorValue.Z) + ");\n";
				break;

			case EMaterialExpressionType::Constant4Vector:
				OutSource += "    float4 " + Variable + " = float4(" + HlslFloat(Node->VectorValue.X) + ", " + HlslFloat(Node->VectorValue.Y) + ", " + HlslFloat(Node->VectorValue.Z) + ", " + HlslFloat(Node->VectorValue.W) + ");\n";
				break;

			case EMaterialExpressionType::ScalarParameter:
			case EMaterialExpressionType::VectorParameter:
			{
				const FString Key = GetMaterialGraphParameterKey(Node->ParameterName, Node->Type);
				const auto It = ParameterIndices.find(Key);

				if (It == ParameterIndices.end())
				{
					OutError = "Material Graph parameter index was not generated.";
					return false;
				}

				if (Node->Type == EMaterialExpressionType::ScalarParameter)
				{
					OutSource += "    float " + Variable + " = GraphParameterValues[" + std::to_string(It->second) + "].x;\n";
				}
				else
				{
					OutSource += "    float4 " + Variable + " = GraphParameterValues[" + std::to_string(It->second) + "];\n";
				}
				break;
			}

			case EMaterialExpressionType::TextureCoordinate:
				OutSource += "    float2 " + Variable + " = UV;\n";
				break;

			case EMaterialExpressionType::TextureSample:
			case EMaterialExpressionType::TextureSampleParameter2D:
			{
				const auto It = TextureIndices.find(Node->Id);

				if (It == TextureIndices.end())
				{
					OutError = "Material Graph texture index was not generated.";
					return false;
				}

				const FString UVInput = GetInputVariable(Node->Id, 0);
				OutSource += "    float4 " + Variable + " = GraphTexture" + std::to_string(It->second) + ".Sample(ColorSampler, " + UVInput + ");\n";

				if (Node->bSRGB)
				{
					OutSource += "    " + Variable + ".rgb = pow(max(" + Variable + ".rgb, 0.0f), 2.2f);\n";
				}

				break;
			}

			case EMaterialExpressionType::Time:
				OutSource += "    float " + Variable + " = Time;\n";
				break;

			case EMaterialExpressionType::VertexColor:
				OutSource += "    float4 " + Variable + " = VertexColor;\n";
				break;

			case EMaterialExpressionType::WorldPosition:
				OutSource += "    float3 " + Variable + " = WorldPosition;\n";
				break;

			case EMaterialExpressionType::CameraVectorWS:
				OutSource += "    float3 " + Variable + " = CameraVectorWS;\n";
				break;

			case EMaterialExpressionType::VertexNormalWS:
				OutSource += "    float3 " + Variable + " = normalize(VertexNormalWS);\n";
				break;

			case EMaterialExpressionType::Add:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = " + A + " + " + B + ";\n";
				break;

			case EMaterialExpressionType::Subtract:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = " + A + " - " + B + ";\n";
				break;

			case EMaterialExpressionType::Multiply:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = " + A + " * " + B + ";\n";
				break;

			case EMaterialExpressionType::Divide:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = " + A + " / " + B + ";\n";
				break;

			case EMaterialExpressionType::Lerp:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = lerp(" + A + ", " + B + ", " + GetInputVariable(Node->Id, 2) + ");\n";
				break;

			case EMaterialExpressionType::OneMinus:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = 1.0f - " + GetInputVariable(Node->Id, 0) + ";\n";
				break;

			case EMaterialExpressionType::Step:
				OutSource += "    float " + Variable + " = step(" + A + ", " + B + ");\n";
				break;

			case EMaterialExpressionType::Noise:
				OutSource += "    float " + Variable + " = MaterialGraphNoise2D(" + A + ");\n";
				break;

			case EMaterialExpressionType::Clamp:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = clamp(" + A + ", " + GetInputVariable(Node->Id, 1, OutputType) + ", " + GetInputVariable(Node->Id, 2, OutputType) + ");\n";
				break;

			case EMaterialExpressionType::Power:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = pow(" + A + ", " + B + ");\n";
				break;

			case EMaterialExpressionType::Abs:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = abs(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::Frac:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = frac(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::Sine:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = sin(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::Min:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = min(" + A + ", " + B + ");\n";
				break;

			case EMaterialExpressionType::Max:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = max(" + A + ", " + B + ");\n";
				break;

			case EMaterialExpressionType::DotProduct:
				OutSource += "    float " + Variable + " = dot(" + GetInputVariable(Node->Id, 0) + ", " + GetInputVariable(Node->Id, 1) + ");\n";
				break;

			case EMaterialExpressionType::Normalize:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = normalize(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::AppendVector:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = " + FString(HlslType) + "(" + GetInputVariable(Node->Id, 0) + ", " + GetInputVariable(Node->Id, 1) + ");\n";
				break;

			case EMaterialExpressionType::Custom:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = EvaluateCustomNode_" + std::to_string(Node->Id) + "(" + GetInputVariable(Node->Id, 0) + ", " + GetInputVariable(Node->Id, 1) + ");\n";
				break;

			case EMaterialExpressionType::ComponentMask:
			{
				FString Swizzle;
				static const char Channels[] = { 'r', 'g', 'b', 'a' };

				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					if ((Node->ComponentMask & (1u << Channel)) != 0)
					{
						Swizzle += Channels[Channel];
					}
				}

				const FString Input = GetInputVariable(Node->Id, 0);
				const FString Expression = Swizzle.size() == 1 ? Input + "." + Swizzle : Input + "." + Swizzle;

				OutSource += "    " + FString(HlslType) + " " + Variable + " = " + Expression + ";\n";
				break;
			}

			case EMaterialExpressionType::Saturate:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = saturate(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::Panner:
				OutSource += "    float2 " + Variable + " = " + A + " + Time * " + B + ";\n";
				break;

			case EMaterialExpressionType::Fresnel:
				OutSource += "    float " + Variable + " = " + C + " + (1.0f - " + C + ") * pow(1.0f - saturate(dot(normalize(" + GetInputVariable(Node->Id, 0) + "), normalize(CameraVectorWS))), " + B + ");\n";
				break;

			case EMaterialExpressionType::Cosine:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = cos(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::Floor:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = floor(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::Distance:
				OutSource += "    float " + Variable + " = distance(" + GetInputVariable(Node->Id, 0) + ", " + GetInputVariable(Node->Id, 1) + ");\n";
				break;

			case EMaterialExpressionType::Length:
				OutSource += "    float " + Variable + " = length(" + GetInputVariable(Node->Id, 0) + ");\n";
				break;

			case EMaterialExpressionType::CrossProduct:
				OutSource += "    float3 " + Variable + " = cross(" + GetInputVariable(Node->Id, 0) + ", " + GetInputVariable(Node->Id, 1) + ");\n";
				break;

			case EMaterialExpressionType::Desaturation:
			{
				const FString Color = GetInputVariable(Node->Id, 0);
				const FString Fraction = GetInputVariable(Node->Id, 1);
				const FString Gray = Variable + "_Gray";

				OutSource += "    float " + Gray + " = dot(" + Color + ".rgb, float3(0.2126f, 0.7152f, 0.0722f));\n";

				if (OutputType == EMaterialValueType::Vector4)
				{
					OutSource += "    float4 " + Variable + " = float4(lerp(" + Color + ".rgb, float3(" + Gray + ", " + Gray + ", " + Gray + "), saturate(" + Fraction + ")), " + Color + ".a);\n";
				}
				else
				{
					OutSource += "    float3 " + Variable + " = lerp(" + Color + ", float3(" + Gray + ", " + Gray + ", " + Gray + "), saturate(" + Fraction + "));\n";
				}

				break;
			}

			case EMaterialExpressionType::SphereMask:
			{
				const FString Radius = GetInputVariable(Node->Id, 2);
				const FString Softness = GetInputVariable(Node->Id, 3);

				OutSource += "    float " + Variable + " = 1.0f - smoothstep(" + Radius + " - max(" + Softness + ", 0.0001f), " + Radius + ", distance(" + A + ", " + B + "));\n";
				break;
			}

			case EMaterialExpressionType::ConstantBiasScale:
				OutSource += "    " + FString(HlslType) + " " + Variable + " = (" + A + " + " + B + ") * " + C + ";\n";
				break;

			case EMaterialExpressionType::Rotator:
			{
				const FString SinValue = Variable + "_Sin";
				const FString CosValue = Variable + "_Cos";
				const FString Offset = Variable + "_Offset";

				OutSource += "    float " + SinValue + ";\n";
				OutSource += "    float " + CosValue + ";\n";
				OutSource += "    sincos(" + GetInputVariable(Node->Id, 2) + ", " + SinValue + ", " + CosValue + ");\n";
				OutSource += "    float2 " + Offset + " = " + A + " - " + B + ";\n";
				OutSource += "    float2 " + Variable + " = float2(" + Offset + ".x * " + CosValue + " - " + Offset + ".y * " + SinValue + ", " + Offset + ".x * " + SinValue + " + " + Offset + ".y * " + CosValue + ") + " + B + ";\n";
				break;
			}

			default:
				OutError = "Unsupported Material Expression.";
				return false;
			}
		}

		const auto EmitOutput = [&](int32 Pin, const char* Define, const char* Field, bool bVector4, bool bVector3)
		{
			const FMaterialExpressionConnection* Connection = FindGraphInput(Material, Material.OutputNodeId, Pin);

			if (!Connection)
			{
				return;
			}

			const FString Variable = GraphNodeVariable(Connection->OutputNodeId);
			const EMaterialValueType Type = Material.GetExpressionOutputType(Connection->OutputNodeId, Connection->OutputPin);

			OutSource += FString("#define ") + Define + " 1\n";

			if (bVector4 && Type == EMaterialValueType::Vector3)
			{
				OutSource += "    Result." + FString(Field) + " = float4(" + Variable + ", 1.0f);\n";
			}
			else if (bVector3 && Type == EMaterialValueType::Vector4)
			{
				OutSource += "    Result." + FString(Field) + " = " + Variable + ".rgb;\n";
			}
			else
			{
				OutSource += "    Result." + FString(Field) + " = " + Variable + ";\n";
			}
		};

		OutSource += "    Result.BaseColor = float4(1, 1, 1, 1);\n";
		OutSource += "    Result.SpecularColor = float3(1, 1, 1);\n";
		OutSource += "    Result.Shininess = 32.0f;\n";
		OutSource += "    Result.EmissiveColor = float3(0, 0, 0);\n";
		OutSource += "    Result.EmissiveFactor = 1.0f;\n";
		OutSource += "    Result.Metallic = 0.0f;\n";
		OutSource += "    Result.Roughness = 0.8f;\n";
		OutSource += "    Result.OpacityMask = 1.0f;\n";
		OutSource += "    Result.Normal = float3(0, 0, 1);\n";
		OutSource += "    Result.AmbientOcclusion = 1.0f;\n";

		EmitOutput(0, "MATERIAL_GRAPH_HAS_BASE_COLOR", "BaseColor", true, false);
		EmitOutput(1, "MATERIAL_GRAPH_HAS_SPECULAR_COLOR", "SpecularColor", false, true);
		EmitOutput(2, "MATERIAL_GRAPH_HAS_SHININESS", "Shininess", false, false);
		EmitOutput(3, "MATERIAL_GRAPH_HAS_EMISSIVE_COLOR", "EmissiveColor", false, true);
		EmitOutput(4, "MATERIAL_GRAPH_HAS_EMISSIVE_FACTOR", "EmissiveFactor", false, false);
		EmitOutput(5, "MATERIAL_GRAPH_HAS_METALLIC", "Metallic", false, false);
		EmitOutput(6, "MATERIAL_GRAPH_HAS_ROUGHNESS", "Roughness", false, false);
		EmitOutput(7, "MATERIAL_GRAPH_HAS_OPACITY_MASK", "OpacityMask", false, false);
		EmitOutput(8, "MATERIAL_GRAPH_HAS_NORMAL", "Normal", false, true);
		EmitOutput(9, "MATERIAL_GRAPH_HAS_AMBIENT_OCCLUSION", "AmbientOcclusion", false, false);

		OutSource += "    return Result;\n";
		OutSource += "}\n";

		OutSource += "FMaterialGraphOutput EvaluateMaterialGraph(float2 UV)\n";
		OutSource += "{\n";
		OutSource += "    return EvaluateMaterialGraph(UV, float3(0, 0, 0), float3(0, 0, 1), float3(0, 0, 1), float4(1, 1, 1, 1), 0.0f);\n";
		OutSource += "}\n";

		OutError.clear();
		return true;
	}
}

FString GetMaterialGraphHash(const FString& Source)
{
	uint64 Hash = 14695981039346656037ull;

	for (unsigned char Value : Source)
	{
		Hash ^= Value;
		Hash *= 1099511628211ull;
	}

	std::ostringstream Stream;
	Stream << std::hex << Hash;
	return Stream.str();
}

FString GetMaterialGraphAssetId(const UMaterial& Material)
{
	FString Identity = Material.GetPath();

	if (Identity.empty())
	{
		Identity = std::to_string(reinterpret_cast<uintptr_t>(&Material));
	}

	return GetMaterialGraphHash(Identity);
}

bool SaveGeneratedMaterialGraph(const FString& Source, const FString& AssetId, FString& OutIncludePath)
{
	const fs::path Directory = fs::path(ResourcePaths::ShaderFolder) / "GeneratedMaterial";
	std::error_code Error;
	fs::create_directories(Directory, Error);

	if (Error)
	{
		return false;
	}

	const fs::path File = Directory / ("Graph_" + AssetId + ".hlsli");

	std::ifstream Existing(File, std::ios::binary);
	const FString ExistingSource(std::istreambuf_iterator<char>{ Existing }, std::istreambuf_iterator<char>{});

	if (ExistingSource != Source)
	{
		std::ofstream Output(File, std::ios::binary | std::ios::trunc);

		if (!Output)
		{
			return false;
		}

		Output.write(Source.data(), static_cast<std::streamsize>(Source.size()));

		if (!Output)
		{
			return false;
		}
	}

	OutIncludePath = "GeneratedMaterial/Graph_" + AssetId + ".hlsli";
	return true;
}

bool SaveGeneratedMaterialShaderVariant(const FString& BaseShaderPath, const FString& IncludePath, const FString& AssetId, FString& OutShaderPath)
{
	std::ifstream Input(BaseShaderPath, std::ios::binary);

	if (!Input)
	{
		return false;
	}

	const FString BaseSource(std::istreambuf_iterator<char>{ Input }, std::istreambuf_iterator<char>{});
	const FString IncludeMarker = "#include MATERIAL_GRAPH_INCLUDE";
	const size_t IncludePosition = BaseSource.find(IncludeMarker);

	if (IncludePosition == FString::npos)
	{
		return false;
	}

	FString GeneratedSource = BaseSource;
	const FString LiteralInclude = "#include \"" + IncludePath + "\"";
	GeneratedSource.replace(IncludePosition, IncludeMarker.size(), LiteralInclude);

	const fs::path BasePath(BaseShaderPath);
	const fs::path VariantPath = BasePath.parent_path() / (BasePath.stem().string() + "_Graph_" + AssetId + ".hlsli");

	std::ifstream Existing(VariantPath, std::ios::binary);
	const FString ExistingSource(std::istreambuf_iterator<char>{ Existing }, std::istreambuf_iterator<char>{});

	if (ExistingSource != GeneratedSource)
	{
		std::ofstream Output(VariantPath, std::ios::binary | std::ios::trunc);

		if (!Output)
		{
			return false;
		}

		Output.write(GeneratedSource.data(), static_cast<std::streamsize>(GeneratedSource.size()));

		if (!Output)
		{
			return false;
		}
	}

	OutShaderPath = VariantPath.generic_string();
	return true;
}

struct FPipelineTableEntry
{
	EPSOType Type;
	const char* ShaderPath;
	EPrimitiveTopology Topology;
	ERasterizerState RasterizerState;
	EBlendState BlendState;
	EDepthStencilState DepthStencilState;
};

constexpr FPipelineTableEntry PipelineTable[] =
{
	{ EPSOType::StaticMesh_Opaque,       "Resources/Shader/StaticMeshShader.hlsl",            EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::StaticMesh_Translucent,  "Resources/Shader/StaticMeshTranslucentShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::StaticMesh_Wireframe,    "Resources/Shader/StaticMeshShader.hlsl",    EPrimitiveTopology::TriangleList, ERasterizerState::Wireframe, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::Particle_AlphaBlend,     "Resources/Shader/ParticleSubUVShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_Additive,       "Resources/Shader/ParticleSubUVShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Additive,     EDepthStencilState::ReadOnly },
	{ EPSOType::Skybox,                  "Resources/Shader/SkyboxShader.hlsl",        EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::ReadOnly },
	{ EPSOType::Grid,                    "Resources/Shader/GridShader.hlsl",          EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::AlphaBlend,   EDepthStencilState::Default },
	{ EPSOType::Outline_Mask,            "Resources/Shader/OutlineShader.hlsl",       EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::NoColorWrite, EDepthStencilState::StencilMask },
	{ EPSOType::Outline_Draw,            "Resources/Shader/OutlineShader.hlsl",       EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::StencilOutline },
	{ EPSOType::StaticMesh_GBuffer,     "Resources/Shader/StaticMeshGBufferShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::Reserved_10,             nullptr,                                            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::Reserved_11,             nullptr,                                            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::ToneMap,                "Resources/Shader/ToneMapShader.hlsl",              EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Disabled },
	{ EPSOType::Reserved_13,            nullptr,                                            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::Billboard,              "Resources/Shader/BillboardShader.hlsl",            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::StaticMesh_Forward,		"Resources/Shader/UberLit.hlsl",			EPrimitiveTopology::TriangleList,			ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_WorldNormal, "Resources/Shader/UberLit.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_DepthOnly, "Resources/Shader/DepthPrepass.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_LightShadow, "Resources/Shader/DepthPrepass.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::LightShadow, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::LightCullingDebug, "Resources/Shader/LightCullingDebug.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::Disabled },
	{ EPSOType::ShadowTileClear, "Resources/Shader/ShadowTileClear.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::NoColorWrite, EDepthStencilState::AlwaysWrite },
	{ EPSOType::ShadowDepth, "Resources/Shader/ShadowDepth.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::LightShadow, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::ShadowProjection, "Resources/Shader/ShadowProjection.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::ShadowMaskR, EDepthStencilState::Disabled },
	{ EPSOType::ShadowMaskVisualize, "Resources/Shader/ShadowMaskVisualize.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Disabled },
	{ EPSOType::StaticMesh_DBufferDecal, "Resources/Shader/ProjectedDecal.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::DecalPremultiplied, EDepthStencilState::DecalEqualReadOnly },
	{ EPSOType::DBufferDecal_DepthReconstruction, "Resources/Shader/ProjectedDecalDepth.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::DecalPremultiplied, EDepthStencilState::Disabled },
	{ EPSOType::Particle_Instanced, "Resources/Shader/ParticleInstancedShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_GpuInstanced, "Resources/Shader/ParticleGpuInstancedShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_MeshOpaque, "Resources/Shader/ParticleMeshInstancedShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::Particle_MeshTranslucent, "Resources/Shader/ParticleMeshInstancedShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_MeshAdditive, "Resources/Shader/ParticleMeshInstancedShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Additive, EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_Sprite, "Resources/Shader/ParticleSpriteShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly },
	{ EPSOType::SkeletalMesh_GBuffer, "Resources/Shader/SkeletalMeshGBufferShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::Particle_BeamTrailTranslucent, "Resources/Shader/ParticleBeamTrailShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_BeamTrailAdditive, "Resources/Shader/ParticleBeamTrailShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Additive, EDepthStencilState::ReadOnly }
};

static_assert(sizeof(PipelineTable) / sizeof(PipelineTable[0]) == static_cast<size_t>(EPSOType::Count));

void FRenderResourceManager::InitPipelineStates()
{
	for (size_t i = 0; i < static_cast<size_t>(EPSOType::Count); ++i)
	{
		const auto& Entry = PipelineTable[i];
		const EPSOType Type = static_cast<EPSOType>(i);
		PipelineStateMap[Type] = MakeUnique<FPipelineState>();
		FPipelineState* State = PipelineStateMap[Type].get();
		// World Normal은 전용 매크로 변형이 성공했을 때만 연결한다.
		// 셰이더가 없는 항목은 번호만 남긴 예약 자리다.
		State->Shader = Type == EPSOType::StaticMesh_WorldNormal || !Entry.ShaderPath ? nullptr : GetShaderProgram(Entry.ShaderPath);
		State->Topology = Entry.Topology;
		State->RasterizerState = Entry.RasterizerState;
		State->BlendState = Entry.BlendState;
		State->DepthStencilState = Entry.DepthStencilState;
		State->StencilRef =
			Type == EPSOType::Outline_Mask ? StencilBits::Outline : 0;
	}
	FShaderProgram* DepthProgram = GetShaderProgram("Resources/Shader/DepthPrepass.hlsl");
	const FString ShadowPath = "Resources/Shader/ShadowDepth.hlsl";
	if (auto* Base = ShaderProgramMap.Find(ShadowPath))
	{
		const D3D_SHADER_MACRO Defines[] = { { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
		FString VSPath;
		FShaderByteCode Code = RenderUtil::GetOrCompile(ShadowPath, "mainVS", EShaderType::Vertex, VSPath, Defines);
		if (Code.IsValid())
		{
			auto VS = RenderCommand::CreateVertexShader(Code);
			if (VS && VS->IsValid())
			{
				FVertexShader* VSRaw = VS.get();
				VertexShaderMap[VSPath] = std::move(VS);
				ShaderProgramMap[ShadowPath + "#GPU"] = MakeUnique<FShaderProgram>(VSRaw, (*Base)->PixelShader);
			}
		}
	}

	struct FForwardVariant { const char* Name; const char* Shading; const char* Reflection; };
	const FForwardVariant Variants[] = {
		{ "GouraudPhong", "SHADING_MODEL_GOURAUD", "REFLECTION_MODEL_PHONG" },
		{ "GouraudBlinnPhong", "SHADING_MODEL_GOURAUD", "REFLECTION_MODEL_BLINN_PHONG" },
		{ "GouraudLambert", "SHADING_MODEL_GOURAUD", "REFLECTION_MODEL_LAMBERT" },
		{ "PhongPhong", "SHADING_MODEL_PHONG", "REFLECTION_MODEL_PHONG" },
		{ "PhongBlinnPhong", "SHADING_MODEL_PHONG", "REFLECTION_MODEL_BLINN_PHONG" },
		{ "PhongLambert", "SHADING_MODEL_PHONG", "REFLECTION_MODEL_LAMBERT" },
		{ "VIEW_MODE_UNLIT", "VIEW_MODE_UNLIT", "REFLECTION_MODEL_LAMBERT" },
		{ "VIEW_MODE_WORLD_NORMAL", "VIEW_MODE_WORLD_NORMAL", "REFLECTION_MODEL_LAMBERT" }
	};
	for (const auto& Variant : Variants)
	{
		const char* Name = Variant.Name;
		const D3D_SHADER_MACRO Defines[] = { { Variant.Shading, "1" }, { Variant.Reflection, "1" }, { nullptr, nullptr } };
		const FString Path = "Resources/Shader/UberLit.hlsl";
		// 변형도 .cso 캐시를 쓴다(매크로 이름이 캐시 파일 이름에 들어간다). 매번 컴파일하면 Release에서 시작이 크게 늦어진다.
		FString VSCSOPath;
		FString PSCSOPath;
		FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath, Defines);
		FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath, Defines);
		if (!VSCode.IsValid() || !PSCode.IsValid())
		{
			HTR_LOG(Error, "[Shader] UberShader variant compile failed: {}", Name);
			continue;
		}
		auto VS = RenderCommand::CreateVertexShader(VSCode);
		auto PS = RenderCommand::CreatePixelShader(PSCode);
		if (!VS || !VS->IsValid() || !PS || !PS->IsValid())
		{
			continue;
		}
		FVertexShader* VSRaw = VS.get();
		FPixelShader* PSRaw = PS.get();
		const FString Key = Path + "#" + Name;
		VertexShaderMap[Key + "#VS"] = std::move(VS);
		PixelShaderMap[Key + "#PS"] = std::move(PS);
		ShaderProgramMap[Key] = MakeUnique<FShaderProgram>(VSRaw, PSRaw);
		if (std::strcmp(Name, "VIEW_MODE_WORLD_NORMAL") == 0)
			PipelineStateMap[EPSOType::StaticMesh_WorldNormal]->Shader = ShaderProgramMap[Key].get();

		const D3D_SHADER_MACRO SkinningDefines[] = { { Variant.Shading, "1" }, { Variant.Reflection, "1" }, { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
		FString SkinningVSPath;
		FShaderByteCode SkinningCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, SkinningVSPath, SkinningDefines);
		if (!SkinningCode.IsValid()) continue;
		auto SkinningVS = RenderCommand::CreateVertexShader(SkinningCode);
		if (!SkinningVS || !SkinningVS->IsValid()) continue;
		FVertexShader* SkinningVSRaw = SkinningVS.get();
		VertexShaderMap[SkinningVSPath] = std::move(SkinningVS);
		ShaderProgramMap[Key + "#GPU"] = MakeUnique<FShaderProgram>(SkinningVSRaw, PSRaw);
	}
	const FString TranslucentPath = "Resources/Shader/StaticMeshTranslucentShader.hlsl";
	if (auto* Base = ShaderProgramMap.Find(TranslucentPath))
	{
		const D3D_SHADER_MACRO Defines[] = { { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
		FString VSPath;
		FShaderByteCode Code = RenderUtil::GetOrCompile(TranslucentPath, "mainVS", EShaderType::Vertex, VSPath, Defines);
		if (Code.IsValid())
		{
			auto VS = RenderCommand::CreateVertexShader(Code);
			if (VS && VS->IsValid())
			{
				FVertexShader* VSRaw = VS.get();
				VertexShaderMap[VSPath] = std::move(VS);
				ShaderProgramMap[TranslucentPath + "#GPU"] = MakeUnique<FShaderProgram>(VSRaw, (*Base)->PixelShader);
			}
		}
	}

	const FString SkeletalGBufferPath = "Resources/Shader/SkeletalMeshGBufferShader.hlsl";

	if (auto* Base = ShaderProgramMap.Find(SkeletalGBufferPath))
	{
		const D3D_SHADER_MACRO Defines[] = { { "GPU_SKINNING", "1" }, { nullptr, nullptr } };

		FString VSPath;
		FShaderByteCode Code = RenderUtil::GetOrCompile(SkeletalGBufferPath, "mainVS", EShaderType::Vertex, VSPath, Defines);

		if (Code.IsValid())
		{
			auto VS = RenderCommand::CreateVertexShader(Code);

			if (VS && VS->IsValid())
			{
				FVertexShader* VSRaw = VS.get();
				VertexShaderMap[VSPath] = std::move(VS);
				ShaderProgramMap[SkeletalGBufferPath + "#GPU"] = MakeUnique<FShaderProgram>(VSRaw, (*Base)->PixelShader);
			}
		}
	}

	if (DepthProgram && DepthProgram->PixelShader)
	{
		if (FShaderProgram* StaticVSProg = GetForwardShaderProgram(EReflectionModel::Lambert, EShadingModel::Phong, false, true);
			StaticVSProg && StaticVSProg->VertexShader)
		{
			ShaderProgramMap["DepthPrepass#Uber"] = MakeUnique<FShaderProgram>(StaticVSProg->VertexShader, DepthProgram->PixelShader);
		}
		if (FShaderProgram* SkinVSProg = GetForwardShaderProgram(EReflectionModel::Lambert, EShadingModel::Phong, true, true);
			SkinVSProg && SkinVSProg->VertexShader)
		{
			ShaderProgramMap["DepthPrepass#Uber#GPU"] = MakeUnique<FShaderProgram>(SkinVSProg->VertexShader, DepthProgram->PixelShader);
		}
	}
}

FShaderProgram* FRenderResourceManager::GetSkeletalMeshGBufferShaderProgram(bool bGPUSkinning)
{
	const FString Key = FString("Resources/Shader/SkeletalMeshGBufferShader.hlsl") + (bGPUSkinning ? "#GPU" : "");
	if (auto* Found = Get().ShaderProgramMap.Find(Key))
	{
		return Found->get();
	}
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetGPUSkinningShadowDepthShaderProgram()
{
	if (auto* Found = Get().ShaderProgramMap.Find("Resources/Shader/ShadowDepth.hlsl#GPU")) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetForwardShaderProgram(EReflectionModel Reflection, EShadingModel Shading, bool bGPUSkinning, bool bUnlit)
{
	const char* ReflectionName = Reflection == EReflectionModel::Phong ? "Phong" :
		Reflection == EReflectionModel::Lambert ? "Lambert" : "BlinnPhong";
	const FString Name = bUnlit ? FString("VIEW_MODE_UNLIT") :
		FString(Shading == EShadingModel::Gouraud ? "Gouraud" : "Phong") + ReflectionName;
	const FString Key = FString("Resources/Shader/UberLit.hlsl#") + Name + (bGPUSkinning ? "#GPU" : "");
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetMaterialGraphShaderProgram(const UMaterial& Material, EPSOType TargetPSO, EReflectionModel Reflection, bool bUnlit, bool bGPUSkinning, FString& OutError)
{
	FString GeneratedSource;

	if (!GenerateMaterialGraphSource(Material, GeneratedSource, OutError))
	{
		return nullptr;
	}

	const FString Hash = GetMaterialGraphHash(GeneratedSource);
	const FString AssetId = GetMaterialGraphAssetId(Material);

	FString ShaderPath;
	FShaderProgram* BaseProgram = nullptr;

	switch (TargetPSO)
	{
	case EPSOType::StaticMesh_Forward:
		ShaderPath = "Resources/Shader/UberLit.hlsl";
		BaseProgram = GetForwardShaderProgram(Reflection, EShadingModel::Phong, bGPUSkinning, bUnlit);
		break;

	case EPSOType::StaticMesh_GBuffer:
		ShaderPath = "Resources/Shader/StaticMeshGBufferShader.hlsl";
		BaseProgram = GetShaderProgram(ShaderPath);
		break;

	case EPSOType::StaticMesh_Translucent:
		ShaderPath = "Resources/Shader/StaticMeshTranslucentShader.hlsl";
		BaseProgram = GetTranslucentShaderProgram(bGPUSkinning);
		break;

	case EPSOType::Particle_AlphaBlend:
	case EPSOType::Particle_Additive:
		ShaderPath = "Resources/Shader/ParticleSubUVShader.hlsl";
		break;

	case EPSOType::Particle_Sprite:
		ShaderPath = "Resources/Shader/ParticleSpriteShader.hlsl";
		break;

	case EPSOType::Particle_Instanced:
		ShaderPath = "Resources/Shader/ParticleInstancedShader.hlsl";
		break;

	case EPSOType::Particle_GpuInstanced:
		ShaderPath = "Resources/Shader/ParticleGpuInstancedShader.hlsl";
		break;

	case EPSOType::Particle_MeshOpaque:
	case EPSOType::Particle_MeshTranslucent:
	case EPSOType::Particle_MeshAdditive:
		ShaderPath = "Resources/Shader/ParticleMeshInstancedShader.hlsl";
		break;

	case EPSOType::SkeletalMesh_GBuffer:
		ShaderPath = "Resources/Shader/SkeletalMeshGBufferShader.hlsl";
		BaseProgram = GetSkeletalMeshGBufferShaderProgram(bGPUSkinning);
		break;

	case EPSOType::Particle_BeamTrailTranslucent:
	case EPSOType::Particle_BeamTrailAdditive:
		ShaderPath = "Resources/Shader/ParticleBeamTrailShader.hlsl";
		break;

	default:
		OutError = "Unsupported Material Graph PSO.";
		return nullptr;
	}

	if (!BaseProgram)
	{
		FPipelineState* Pipeline = GetPSO(TargetPSO);
		BaseProgram = Pipeline ? Pipeline->Shader : nullptr;
	}

	if (!BaseProgram || !BaseProgram->VertexShader)
	{
		OutError = "Base Material Shader is not available.";
		return nullptr;
	}

	FString Key = ShaderPath + "#MaterialGraph_" + Hash + "#PSO_" + std::to_string(static_cast<uint8>(TargetPSO));

	if (TargetPSO == EPSOType::StaticMesh_Forward)
	{
		Key += "#Reflection_" + std::to_string(static_cast<int32>(Reflection));
		Key += bUnlit ? "#Unlit" : "#Lit";
	}

	if (bGPUSkinning)
	{
		Key += "#GPU";
	}

	if (auto* Found = Get().ShaderProgramMap.Find(Key))
	{
		OutError.clear();
		return Found->get();
	}

	if (FString* PreviousError = Get().MaterialGraphErrorMap.Find(Key))
	{
		OutError = *PreviousError;
		return nullptr;
	}

	FString IncludePath;

	if (!SaveGeneratedMaterialGraph(GeneratedSource, AssetId, IncludePath))
	{
		OutError = "Failed to write generated Material Graph HLSL.";
		return nullptr;
	}

	FString GeneratedShaderPath;

	if (!SaveGeneratedMaterialShaderVariant(ShaderPath, IncludePath, AssetId, GeneratedShaderPath))
	{
		OutError = "Failed to generate Material Graph shader variant.";
		return nullptr;
	}

	const FString UniqueDefine = "MATERIAL_GRAPH_HASH_" + Hash;

	D3D_SHADER_MACRO Defines[5]{};
	int32 DefineCount = 0;

	if (TargetPSO == EPSOType::StaticMesh_Forward)
	{
		Defines[DefineCount++] = { bUnlit ? "VIEW_MODE_UNLIT" : "SHADING_MODEL_PHONG", "1" };

		const char* ReflectionDefine = Reflection == EReflectionModel::Phong
			? "REFLECTION_MODEL_PHONG"
			: Reflection == EReflectionModel::Lambert
			? "REFLECTION_MODEL_LAMBERT"
			: "REFLECTION_MODEL_BLINN_PHONG";

		Defines[DefineCount++] = { ReflectionDefine, "1" };
	}

	Defines[DefineCount++] = { "MATERIAL_GRAPH", "1" };
	Defines[DefineCount++] = { UniqueDefine.c_str(), "1" };
	Defines[DefineCount] = { nullptr, nullptr };

	FString CsoPath;
	FShaderByteCode PixelCode = RenderUtil::GetOrCompile(GeneratedShaderPath, "mainPS", EShaderType::Pixel, CsoPath, Defines);

	if (!PixelCode.IsValid())
	{
		OutError = "[MaterialGraph] Pixel Shader compilation failed: " + Key;
		Get().MaterialGraphErrorMap[Key] = OutError;
		HTR_LOG(Error, "{}", OutError);
		return nullptr;
	}

	TUniquePtr<FPixelShader> PixelShader = RenderCommand::CreatePixelShader(PixelCode);

	if (!PixelShader || !PixelShader->IsValid())
	{
		OutError = "[MaterialGraph] Failed to create Pixel Shader: " + Key;
		Get().MaterialGraphErrorMap[Key] = OutError;
		HTR_LOG(Error, "{}", OutError);
		return nullptr;
	}

	FPixelShader* PixelRaw = PixelShader.get();
	Get().PixelShaderMap[Key + "#PS"] = std::move(PixelShader);
	Get().ShaderProgramMap[Key] = MakeUnique<FShaderProgram>(BaseProgram->VertexShader, PixelRaw);

	OutError.clear();
	return Get().ShaderProgramMap[Key].get();
}

FShaderProgram* FRenderResourceManager::GetWorldNormalShaderProgram(bool bGPUSkinning)
{
	const FString Key = FString("Resources/Shader/UberLit.hlsl#VIEW_MODE_WORLD_NORMAL") + (bGPUSkinning ? "#GPU" : "");
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetDepthPrepassShaderProgram(bool bGPUSkinning)
{
	const FString Key = bGPUSkinning ? FString("DepthPrepass#Uber#GPU") : FString("DepthPrepass#Uber");
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetTranslucentShaderProgram(bool bGPUSkinning)
{
	const FString Key = FString("Resources/Shader/StaticMeshTranslucentShader.hlsl") + (bGPUSkinning ? "#GPU" : "");
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FPipelineState* FRenderResourceManager::GetPSO(const EPSOType& InType)
{
	if (auto* Found = Get().PipelineStateMap.Find(InType))
	{
		return Found->get();
	}
	return nullptr;
}

void FRenderResourceManager::ScanShaders(const fs::path& ShaderRoot)
{
	std::error_code ErrorCode;

	if (!fs::exists(ShaderRoot, ErrorCode))
	{
		HTR_LOG(Error, "[Shader] Scan Root Not Found : {}", ShaderRoot.generic_string());
		return;
	}

	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(ShaderRoot))
	{
		if (!Entry.is_regular_file()) continue;
		if (Entry.path().extension() != ".hlsl") continue;
		// 컴퓨트 전용 .hlsl에는 mainVS/mainPS가 없다. VS/PS로 컴파일하지 않는다.
		std::string Source;
		std::set<fs::path> Visited;
		AppendShaderSource(Entry.path(), Visited, Source);
		if (Source.find("mainVS(") == std::string::npos ||
			Source.find("mainPS(") == std::string::npos) continue;

		FString Path = Entry.path().generic_string();
		LoadOrCompileShader(Path);
	}
}

FComputeShader* FRenderResourceManager::GetComputeShader(const FString& InPath, const FString& EntryPoint)
{
	auto& Resources = Get();
	const FString Key = InPath + "#" + EntryPoint;
	if (TUniquePtr<FComputeShader>* Found = Resources.ComputeShaderMap.Find(Key))
		return Found->get();

	TUniquePtr<FComputeShader> Shader = RenderCommand::CreateComputeShader(
		RenderUtil::CompileShader(InPath, EntryPoint.c_str(), EShaderType::Compute));
	if (Shader && !Shader->IsValid())
		Shader.reset();

	FComputeShader* Result = Shader.get();
	Resources.ComputeShaderMap[Key] = std::move(Shader);
	return Result;
}

void FRenderResourceManager::Shutdown()
{
	FParticleCurveTexture::Get().Shutdown();
	Get().ComputeShaderMap.Empty();
	Get().VertexShaderMap.Empty();
	Get().PixelShaderMap.Empty();
	Get().ShaderProgramMap.Empty();
	Get().MaterialGraphErrorMap.Empty();
}

FShaderProgram* FRenderResourceManager::GetShaderProgram(const FString& InPath)
{
	if (TUniquePtr<FShaderProgram>* Found = Get().ShaderProgramMap.Find(InPath))
		return Found->get();

	HTR_LOG(Error, "[Shader] not found: {}", InPath);

	if (TUniquePtr<FShaderProgram>* Fallback =
		Get().ShaderProgramMap.Find("Resources/Shader/DefaultShader.hlsl"))
		return Fallback->get();

	return nullptr;
}



void FRenderResourceManager::LoadOrCompileShader(const FString& Path)
{ 
	FString VSCSOPath;
	FString PSCSOPath;
	FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath);
	FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath);

	if (!VSCode.IsValid() || !PSCode.IsValid())
	{
		HTR_LOG(Error, "[Shader] compile failed: {}", Path);
		return;
	}

	TUniquePtr<FVertexShader> Vs = RenderCommand::CreateVertexShader(VSCode);
	TUniquePtr<FPixelShader>  Ps = RenderCommand::CreatePixelShader(PSCode);

	if (!Vs || !Vs->IsValid() || !Ps || !Ps->IsValid())
	{
		HTR_LOG(Error, "[Shader] device create failed: {}", Path);
		return;
	}

	FVertexShader* VsRaw = Vs.get();
	FPixelShader* PsRaw = Ps.get();

	VertexShaderMap[VSCSOPath] = std::move(Vs);
	PixelShaderMap[PSCSOPath] = std::move(Ps);
	ShaderProgramMap[Path] = MakeUnique<FShaderProgram>(VsRaw, PsRaw);

	HTR_LOG(Info, "[Shader] loaded: {}", Path);

}
