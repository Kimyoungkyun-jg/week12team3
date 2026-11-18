#include "EnginePCH.h"

#include "RenderCommand.h"

#include "Material.h"
#include "Render/MaterialRenderProxy.h"
#include "Render/MaterialInstance.h"

#include "Asset/AssetManager.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace
{
	UTexture2D* FindOrLoadTexture(const FString& Path)
	{
		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Path))
		{
			return Texture;
		}
		return UAssetManager::Get().LoadTexture(Path);
	}

	const FMaterialExpressionNode* FindExpressionNode(const UMaterial& Material, int32 NodeId)
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

	int32 GetMaterialValueWidth(EMaterialValueType Type)
	{
		switch (Type)
		{
		case EMaterialValueType::Scalar:
			return 1;
		case EMaterialValueType::Vector2:
			return 2;
		case EMaterialValueType::Vector3:
			return 3;
		case EMaterialValueType::Vector4:
			return 4;
		default:
			return 0;
		}
	}

	EMaterialValueType GetMaterialValueTypeFromWidth(int32 Width)
	{
		switch (Width)
		{
		case 1:
			return EMaterialValueType::Scalar;
		case 2:
			return EMaterialValueType::Vector2;
		case 3:
			return EMaterialValueType::Vector3;
		case 4:
			return EMaterialValueType::Vector4;
		default:
			return EMaterialValueType::Unknown;
		}
	}

	EMaterialValueType ResolveMaterialBinaryType(EMaterialValueType A, EMaterialValueType B)
	{
		if (A == EMaterialValueType::Unknown)
		{
			return B;
		}
		if (B == EMaterialValueType::Unknown)
		{
			return A;
		}

		if (A == EMaterialValueType::Scalar)
		{
			return B;
		}
		if (B == EMaterialValueType::Scalar)
		{
			return A;
		}

		return A == B ? A : EMaterialValueType::Unknown;
	}

	bool IsMaterialNumericType(EMaterialValueType Type)
	{
		return Type == EMaterialValueType::Scalar ||
			Type == EMaterialValueType::Vector2 ||
			Type == EMaterialValueType::Vector3 ||
			Type == EMaterialValueType::Vector4;
	}

	bool IsMaterialVectorType(EMaterialValueType Type)
	{
		return Type == EMaterialValueType::Vector2 ||
			Type == EMaterialValueType::Vector3 ||
			Type == EMaterialValueType::Vector4;
	}

	int32 GetMaterialComponentMaskCount(uint8 Mask)
	{
		int32 Count = 0;

		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			if ((Mask & (1u << Channel)) != 0)
			{
				++Count;
			}
		}

		return Count;
	}

}

void UMaterial::EnsureDefaultGraph()
{
	int32 MaxId = 0;
	int32 FirstOutputId = 0;
	int32 OutputCount = 0;

	for (const FMaterialExpressionNode& Node : ExpressionNodes)
	{
		MaxId = std::max(MaxId, Node.Id);

		if (Node.Type == EMaterialExpressionType::MaterialOutput)
		{
			if (FirstOutputId == 0)
			{
				FirstOutputId = Node.Id;
			}

			++OutputCount;
		}
	}

	if (OutputCount == 0)
	{
		FMaterialExpressionNode Output;
		Output.Id = ++MaxId;
		Output.Type = EMaterialExpressionType::MaterialOutput;
		Output.Position = FVector2(650.0f, 180.0f);

		ExpressionNodes.Add(Output);
		OutputNodeId = Output.Id;
	}
	else
	{
		const FMaterialExpressionNode* CurrentOutput = FindExpressionNode(*this, OutputNodeId);

		if (!CurrentOutput || CurrentOutput->Type != EMaterialExpressionType::MaterialOutput)
		{
			OutputNodeId = FirstOutputId;
		}
	}

	bool bHasExistingGraphNodes = false;

	for (const FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Type != EMaterialExpressionType::MaterialOutput)
		{
			bHasExistingGraphNodes = true;
			break;
		}
	}

	if (!ExpressionConnections.IsEmpty() || bHasExistingGraphNodes)
	{
		return;
	}

	const auto AddNode = [&](EMaterialExpressionType Type, const FVector2& Position) -> int32
	{
		FMaterialExpressionNode Node{};
		Node.Id = ++MaxId;
		Node.Type = Type;
		Node.Position = Position;

		ExpressionNodes.Add(Node);
		return Node.Id;
	};

	const auto Connect = [&](int32 OutputNode, int32 OutputPin, int32 InputNode, int32 InputPin)
	{
		FMaterialExpressionConnection Connection{};
		Connection.OutputNodeId = OutputNode;
		Connection.OutputPin = OutputPin;
		Connection.InputNodeId = InputNode;
		Connection.InputPin = InputPin;

		ExpressionConnections.Add(Connection);
	};

	const int32 BaseColorConstant = AddNode(EMaterialExpressionType::Constant4Vector, FVector2(40.0f, 350.0f));
	FindExpressionNode(*this, BaseColorConstant);

	FMaterialExpressionNode* TintNode = nullptr;

	for (FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Id == BaseColorConstant)
		{
			TintNode = &Node;
			break;
		}
	}

	if (TintNode)
	{
		TintNode->VectorValue = BaseColor;
	}

	int32 BaseColorSource = 0;

	UTexture2D* ColorTexture = Textures.IsValidIndex(0) ? Textures[0] : nullptr;
	const bool bHasColorTexture = ColorTexture && !ColorTexture->GetPath().empty();

	if (bHasColorTexture)
	{
		const int32 TexCoordId = AddNode(EMaterialExpressionType::TextureCoordinate, FVector2(40.0f, 80.0f));
		const int32 TextureSampleId = AddNode(EMaterialExpressionType::TextureSample, FVector2(300.0f, 80.0f));

		for (FMaterialExpressionNode& Node : ExpressionNodes)
		{
			if (Node.Id == TextureSampleId)
			{
				Node.TexturePath = ColorTexture->GetPath();
				break;
			}
		}

		Connect(TexCoordId, 0, TextureSampleId, 0);

		const int32 MultiplyId = AddNode(EMaterialExpressionType::Multiply, FVector2(570.0f, 180.0f));

		Connect(TextureSampleId, 0, MultiplyId, 0);
		Connect(BaseColorConstant, 0, MultiplyId, 1);

		BaseColorSource = MultiplyId;
	}
	else
	{
		const int32 VertexColorId = AddNode(EMaterialExpressionType::VertexColor, FVector2(40.0f, 80.0f));
		const int32 MultiplyId = AddNode(EMaterialExpressionType::Multiply, FVector2(570.0f, 180.0f));

		Connect(VertexColorId, 0, MultiplyId, 0);
		Connect(BaseColorConstant, 0, MultiplyId, 1);

		BaseColorSource = MultiplyId;
	}

	Connect(BaseColorSource, 0, OutputNodeId, 0);

	const int32 SpecularNodeId = AddNode(EMaterialExpressionType::Constant3Vector, FVector2(40.0f, 620.0f));

	for (FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Id == SpecularNodeId)
		{
			Node.VectorValue = FVector4(SpecularColor.X * SpecularFactor, SpecularColor.Y * SpecularFactor, SpecularColor.Z * SpecularFactor, 1.0f);
			break;
		}
	}

	Connect(SpecularNodeId, 0, OutputNodeId, 1);

	const int32 ShininessNodeId = AddNode(EMaterialExpressionType::Constant, FVector2(300.0f, 620.0f));

	for (FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Id == ShininessNodeId)
		{
			Node.ScalarValue = Shininess;
			break;
		}
	}

	Connect(ShininessNodeId, 0, OutputNodeId, 2);

	const int32 EmissiveNodeId = AddNode(EMaterialExpressionType::Constant3Vector, FVector2(40.0f, 760.0f));

	for (FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Id == EmissiveNodeId)
		{
			Node.VectorValue = FVector4(EmissiveColor.X, EmissiveColor.Y, EmissiveColor.Z, 1.0f);
			break;
		}
	}

	Connect(EmissiveNodeId, 0, OutputNodeId, 3);

	const int32 EmissiveFactorNodeId = AddNode(EMaterialExpressionType::Constant, FVector2(300.0f, 760.0f));

	for (FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Id == EmissiveFactorNodeId)
		{
			Node.ScalarValue = EmissiveFactor;
			break;
		}
	}

	Connect(EmissiveFactorNodeId, 0, OutputNodeId, 4);

	bLegacyGraphMigrated = true;
}

int32 UMaterial::GetExpressionInputPinCount(EMaterialExpressionType Type) const
{
	switch (Type)
	{
	case EMaterialExpressionType::TextureSample:
	case EMaterialExpressionType::TextureSampleParameter2D:
	case EMaterialExpressionType::OneMinus:
	case EMaterialExpressionType::Abs:
	case EMaterialExpressionType::Frac:
	case EMaterialExpressionType::Sine:
	case EMaterialExpressionType::Normalize:
	case EMaterialExpressionType::ComponentMask:
	case EMaterialExpressionType::Saturate:
	case EMaterialExpressionType::Cosine:
	case EMaterialExpressionType::Floor:
	case EMaterialExpressionType::Length:
		return 1;

	case EMaterialExpressionType::Add:
	case EMaterialExpressionType::Subtract:
	case EMaterialExpressionType::Multiply:
	case EMaterialExpressionType::Divide:
	case EMaterialExpressionType::Power:
	case EMaterialExpressionType::Min:
	case EMaterialExpressionType::Max:
	case EMaterialExpressionType::DotProduct:
	case EMaterialExpressionType::AppendVector:
	case EMaterialExpressionType::Custom:
	case EMaterialExpressionType::Panner:
	case EMaterialExpressionType::Distance:
	case EMaterialExpressionType::CrossProduct:
	case EMaterialExpressionType::Desaturation:
		return 2;

	case EMaterialExpressionType::Lerp:
	case EMaterialExpressionType::Clamp:
	case EMaterialExpressionType::Fresnel:
	case EMaterialExpressionType::ConstantBiasScale:
	case EMaterialExpressionType::Rotator:
		return 3;

	case EMaterialExpressionType::SphereMask:
		return 4;

	case EMaterialExpressionType::MaterialOutput:
		return 8;

	default:
		return 0;
	}
}


int32 UMaterial::GetExpressionOutputPinCount(EMaterialExpressionType Type) const
{
	return Type == EMaterialExpressionType::MaterialOutput ? 0 : 1;
}


const char* UMaterial::GetExpressionInputPinName(EMaterialExpressionType Type, int32 PinIndex) const
{
	static const char* BinaryNames[] = { "A", "B" };
	static const char* TernaryNames[] = { "A", "B", "Alpha" };
	static const char* ClampNames[] = { "Input", "Min", "Max" };
	static const char* PannerNames[] = { "Coordinate", "Speed" };
	static const char* FresnelNames[] = { "Normal", "Exponent", "Base Reflect Fraction" };
	static const char* DesaturationNames[] = { "Color", "Fraction" };
	static const char* SphereMaskNames[] = { "A", "B", "Radius", "Softness" };
	static const char* ConstantBiasScaleNames[] = { "Input", "Bias", "Scale" };
	static const char* RotatorNames[] = { "Coordinate", "Center", "Angle" };
	static const char* OutputNames[] = {
		"Base Color",
		"Specular Color",
		"Shininess",
		"Emissive Color",
		"Emissive Factor",
		"Metallic",
		"Roughness",
		"Normal"
	};

	switch (Type)
	{
	case EMaterialExpressionType::TextureSample:
	case EMaterialExpressionType::TextureSampleParameter2D:
		return PinIndex == 0 ? "UV" : nullptr;

	case EMaterialExpressionType::Add:
	case EMaterialExpressionType::Subtract:
	case EMaterialExpressionType::Multiply:
	case EMaterialExpressionType::Divide:
	case EMaterialExpressionType::Power:
	case EMaterialExpressionType::Min:
	case EMaterialExpressionType::Max:
	case EMaterialExpressionType::DotProduct:
	case EMaterialExpressionType::AppendVector:
	case EMaterialExpressionType::Custom:
	case EMaterialExpressionType::Saturate:
	case EMaterialExpressionType::Cosine:
	case EMaterialExpressionType::Floor:
	case EMaterialExpressionType::Length:
		return PinIndex >= 0 && PinIndex < 2 ? BinaryNames[PinIndex] : nullptr;

	case EMaterialExpressionType::Lerp:
		return PinIndex >= 0 && PinIndex < 3 ? TernaryNames[PinIndex] : nullptr;

	case EMaterialExpressionType::Clamp:
		return PinIndex >= 0 && PinIndex < 3 ? ClampNames[PinIndex] : nullptr;

	case EMaterialExpressionType::OneMinus:
	case EMaterialExpressionType::Abs:
	case EMaterialExpressionType::Frac:
	case EMaterialExpressionType::Sine:
	case EMaterialExpressionType::Normalize:
	case EMaterialExpressionType::ComponentMask:
		return PinIndex == 0 ? "Input" : nullptr;

	case EMaterialExpressionType::MaterialOutput:
		return PinIndex >= 0 && PinIndex < 8 ? OutputNames[PinIndex] : nullptr;

	case EMaterialExpressionType::Panner:
		return PinIndex >= 0 && PinIndex < 2 ? PannerNames[PinIndex] : nullptr;

	case EMaterialExpressionType::Fresnel:
		return PinIndex >= 0 && PinIndex < 3 ? FresnelNames[PinIndex] : nullptr;

	case EMaterialExpressionType::Distance:
	case EMaterialExpressionType::CrossProduct:
		return PinIndex >= 0 && PinIndex < 2 ? BinaryNames[PinIndex] : nullptr;

	case EMaterialExpressionType::Desaturation:
		return PinIndex >= 0 && PinIndex < 2 ? DesaturationNames[PinIndex] : nullptr;

	case EMaterialExpressionType::SphereMask:
		return PinIndex >= 0 && PinIndex < 4 ? SphereMaskNames[PinIndex] : nullptr;

	case EMaterialExpressionType::ConstantBiasScale:
		return PinIndex >= 0 && PinIndex < 3 ? ConstantBiasScaleNames[PinIndex] : nullptr;

	case EMaterialExpressionType::Rotator:
		return PinIndex >= 0 && PinIndex < 3 ? RotatorNames[PinIndex] : nullptr;

	default:
		return nullptr;
	}
}


const char* UMaterial::GetExpressionOutputPinName(EMaterialExpressionType Type, int32 PinIndex) const
{
	return GetExpressionOutputPinCount(Type) > PinIndex && PinIndex >= 0 ? "Value" : nullptr;
}

EMaterialValueType UMaterial::GetExpressionOutputType(int32 NodeId, int32 OutputPin) const
{
	if (OutputPin != 0)
	{
		return EMaterialValueType::Unknown;
	}

	std::unordered_set<int32> Visiting;

	const auto Resolve = [&](auto&& Self, int32 CurrentId) -> EMaterialValueType
	{
		if (!Visiting.insert(CurrentId).second)
		{
			return EMaterialValueType::Unknown;
		}

		const FMaterialExpressionNode* Node = FindExpressionNode(*this, CurrentId);
		if (!Node)
		{
			Visiting.erase(CurrentId);
			return EMaterialValueType::Unknown;
		}

		const auto GetInputType = [&](int32 InputPin)
		{
			for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
			{
				if (Connection.InputNodeId == CurrentId && Connection.InputPin == InputPin)
				{
					return Self(Self, Connection.OutputNodeId);
				}
			}

			return EMaterialValueType::Unknown;
		};

		const EMaterialValueType A = GetInputType(0);
		const EMaterialValueType B = GetInputType(1);
		const EMaterialValueType C = GetInputType(2);

		EMaterialValueType Result = EMaterialValueType::Unknown;

		switch (Node->Type)
		{
		case EMaterialExpressionType::Constant:
		case EMaterialExpressionType::ScalarParameter:
		case EMaterialExpressionType::Time:
			Result = EMaterialValueType::Scalar;
			break;

		case EMaterialExpressionType::Constant2Vector:
		case EMaterialExpressionType::TextureCoordinate:
			Result = EMaterialValueType::Vector2;
			break;

		case EMaterialExpressionType::Constant3Vector:
		case EMaterialExpressionType::WorldPosition:
		case EMaterialExpressionType::CameraVectorWS:
		case EMaterialExpressionType::VertexNormalWS:
			Result = EMaterialValueType::Vector3;
			break;

		case EMaterialExpressionType::Constant4Vector:
		case EMaterialExpressionType::VectorParameter:
		case EMaterialExpressionType::TextureSample:
		case EMaterialExpressionType::TextureSampleParameter2D:
		case EMaterialExpressionType::VertexColor:
			Result = EMaterialValueType::Vector4;
			break;

		case EMaterialExpressionType::Add:
		case EMaterialExpressionType::Subtract:
		case EMaterialExpressionType::Multiply:
		case EMaterialExpressionType::Divide:
		case EMaterialExpressionType::Power:
		case EMaterialExpressionType::Min:
		case EMaterialExpressionType::Max:
			Result = ResolveMaterialBinaryType(A, B);
			break;

		case EMaterialExpressionType::Lerp:
			Result = ResolveMaterialBinaryType(A, B);
			break;

		case EMaterialExpressionType::Clamp:
			Result = IsMaterialNumericType(A) ? A : EMaterialValueType::Unknown;
			break;

		case EMaterialExpressionType::OneMinus:
		case EMaterialExpressionType::Abs:
		case EMaterialExpressionType::Frac:
		case EMaterialExpressionType::Sine:
			Result = IsMaterialNumericType(A) ? A : EMaterialValueType::Unknown;
			break;

		case EMaterialExpressionType::Normalize:
			Result = IsMaterialVectorType(A) ? A : EMaterialValueType::Unknown;
			break;

		case EMaterialExpressionType::DotProduct:
			if (IsMaterialVectorType(A) && A == B)
			{
				Result = EMaterialValueType::Scalar;
			}
			break;

		case EMaterialExpressionType::AppendVector:
		{
			const int32 WidthA = GetMaterialValueWidth(A);
			const int32 WidthB = GetMaterialValueWidth(B);
			Result = WidthA > 0 && WidthB > 0
				? GetMaterialValueTypeFromWidth(WidthA + WidthB)
				: EMaterialValueType::Unknown;
			break;
		}

		case EMaterialExpressionType::ComponentMask:
		{
			const int32 InputWidth = GetMaterialValueWidth(A);
			const int32 SelectedCount = GetMaterialComponentMaskCount(Node->ComponentMask);
			const uint8 ValidMask = InputWidth >= 4 ? 0x0F : static_cast<uint8>((1u << InputWidth) - 1u);

			if (InputWidth > 0 && (Node->ComponentMask & ~ValidMask) == 0)
			{
				Result = GetMaterialValueTypeFromWidth(SelectedCount);
			}
			break;
		}

		case EMaterialExpressionType::Custom:
			Result = IsMaterialNumericType(Node->CustomOutputType)
				? Node->CustomOutputType
				: EMaterialValueType::Unknown;
			break;

		case EMaterialExpressionType::Saturate:
		case EMaterialExpressionType::Cosine:
		case EMaterialExpressionType::Floor:
			Result = IsMaterialNumericType(A) ? A : EMaterialValueType::Unknown;
			break;

		case EMaterialExpressionType::Panner:
		case EMaterialExpressionType::Rotator:
			Result = EMaterialValueType::Vector2;
			break;

		case EMaterialExpressionType::Fresnel:
		case EMaterialExpressionType::SphereMask:
		case EMaterialExpressionType::Distance:
		case EMaterialExpressionType::Length:
			Result = EMaterialValueType::Scalar;
			break;

		case EMaterialExpressionType::CrossProduct:
			Result = EMaterialValueType::Vector3;
			break;

		case EMaterialExpressionType::Desaturation:
			Result = A == EMaterialValueType::Vector3 || A == EMaterialValueType::Vector4
				? A
				: EMaterialValueType::Unknown;
			break;

		case EMaterialExpressionType::ConstantBiasScale:
			Result = IsMaterialNumericType(A) ? A : EMaterialValueType::Unknown;
			break;

		default:
			break;
		}

		Visiting.erase(CurrentId);
		return Result;
	};

	return Resolve(Resolve, NodeId);
}

void UMaterial::GetGraphParameterLayout(TArray<FMaterialGraphParameter>& OutParameters) const
{
	OutParameters.Reset();

	for (const FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Type != EMaterialExpressionType::ScalarParameter && Node.Type != EMaterialExpressionType::VectorParameter)
		{
			continue;
		}

		bool bAlreadyExists = false;

		for (const FMaterialGraphParameter& Parameter : OutParameters)
		{
			if (Parameter.Name == Node.ParameterName && Parameter.Type == Node.Type)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists)
		{
			continue;
		}

		FMaterialGraphParameter& Parameter = OutParameters.AddDefaulted_GetRef();
		Parameter.Name = Node.ParameterName;
		Parameter.Type = Node.Type;
		Parameter.ScalarValue = Node.ScalarValue;
		Parameter.VectorValue = Node.VectorValue;
	}
}

bool UMaterial::ConnectExpressionPins(int32 OutputId, int32 OutputPin, int32 InputId, int32 InputPin, FString& OutError)
{
	if (OutputId == InputId)
	{
		OutError = "A node cannot connect to itself.";
		return false;
	}

	const FMaterialExpressionNode* Output = FindExpressionNode(*this, OutputId);
	const FMaterialExpressionNode* Input = FindExpressionNode(*this, InputId);

	if (!Output || !Input)
	{
		OutError = "Connected node does not exist.";
		return false;
	}

	if (OutputPin < 0 || OutputPin >= GetExpressionOutputPinCount(Output->Type) ||
		InputPin < 0 || InputPin >= GetExpressionInputPinCount(Input->Type))
	{
		OutError = "Invalid expression pin.";
		return false;
	}

	const TArray<FMaterialExpressionConnection> Previous = ExpressionConnections;

	for (int32 Index = ExpressionConnections.Num() - 1; Index >= 0; --Index)
	{
		const auto& Connection = ExpressionConnections[Index];

		if (Connection.InputNodeId == InputId && Connection.InputPin == InputPin)
		{
			ExpressionConnections.RemoveAt(Index);
		}
	}

	ExpressionConnections.Add({ OutputId, OutputPin, InputId, InputPin });

	if (!ValidateExpressionGraph(OutError))
	{
		ExpressionConnections = Previous;
		return false;
	}

	OutError.clear();
	return true;
}


bool UMaterial::ValidateExpressionGraph(FString& OutError) const
{
	std::unordered_map<int32, const FMaterialExpressionNode*> Nodes;
	std::set<std::pair<FString, uint8>> ParameterKeys;
	int32 OutputCount = 0;
	int32 TextureNodeCount = 0;

	for (const FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Id <= 0 || !Nodes.emplace(Node.Id, &Node).second)
		{
			OutError = "Duplicate or invalid expression node ID.";
			return false;
		}

		if (static_cast<uint8>(Node.Type) >= static_cast<uint8>(EMaterialExpressionType::Count))
		{
			OutError = "Invalid expression node type.";
			return false;
		}

		if (Node.Type == EMaterialExpressionType::MaterialOutput)
		{
			++OutputCount;
		}

		if (Node.Type == EMaterialExpressionType::ScalarParameter ||
			Node.Type == EMaterialExpressionType::VectorParameter ||
			Node.Type == EMaterialExpressionType::TextureSampleParameter2D)
		{
			if (Node.ParameterName.empty())
			{
				OutError = "Parameter names must not be empty.";
				return false;
			}

			const uint8 ParameterType = static_cast<uint8>(Node.Type);
			const auto Key = std::make_pair(Node.ParameterName, ParameterType);

			if (!ParameterKeys.insert(Key).second)
			{
				for (const FMaterialExpressionNode& Other : ExpressionNodes)
				{
					if (&Other == &Node)
					{
						break;
					}

					if (Other.ParameterName != Node.ParameterName || Other.Type != Node.Type)
					{
						continue;
					}

					const bool bSameScalar = Node.Type != EMaterialExpressionType::ScalarParameter ||
						std::abs(Other.ScalarValue - Node.ScalarValue) <= 0.000001f;

					const bool bSameVector = Node.Type != EMaterialExpressionType::VectorParameter ||
						(std::abs(Other.VectorValue.X - Node.VectorValue.X) <= 0.000001f &&
							std::abs(Other.VectorValue.Y - Node.VectorValue.Y) <= 0.000001f &&
							std::abs(Other.VectorValue.Z - Node.VectorValue.Z) <= 0.000001f &&
							std::abs(Other.VectorValue.W - Node.VectorValue.W) <= 0.000001f);

					const bool bSameTexture = Node.Type != EMaterialExpressionType::TextureSampleParameter2D ||
						Other.TexturePath == Node.TexturePath;

					if (!bSameScalar || !bSameVector || !bSameTexture)
					{
						OutError = "Duplicated parameters must have matching default values.";
						return false;
					}

					break;
				}
			}
		}

		if (Node.Type == EMaterialExpressionType::TextureSample ||
			Node.Type == EMaterialExpressionType::TextureSampleParameter2D)
		{
			++TextureNodeCount;
		}

		if (Node.Type == EMaterialExpressionType::TextureCoordinate &&
			Node.CoordinateIndex != 0)
		{
			OutError = "Only Texture Coordinate index 0 is supported.";
			return false;
		}

		if (Node.Type == EMaterialExpressionType::ComponentMask &&
			(Node.ComponentMask & 0x0F) == 0)
		{
			OutError = "Component Mask must select at least one channel.";
			return false;
		}
	}

	if (OutputCount != 1 || !Nodes.contains(OutputNodeId) ||
		Nodes[OutputNodeId]->Type != EMaterialExpressionType::MaterialOutput)
	{
		OutError = "Material must have exactly one valid Output node.";
		return false;
	}

	TArray<FMaterialGraphParameter> GraphParameters;
	GetGraphParameterLayout(GraphParameters);

	if (GraphParameters.Num() > MaxMaterialGraphParameters)
	{
		OutError = "Material Graph supports up to 16 unique scalar/vector parameters.";
		return false;
	}

	if (TextureNodeCount > MaxMaterialGraphTextures)
	{
		OutError = "Material Graph supports up to 6 texture nodes.";
		return false;
	}

	std::set<std::pair<int32, int32>> ConnectedInputs;

	for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
	{
		const auto OutputIt = Nodes.find(Connection.OutputNodeId);
		const auto InputIt = Nodes.find(Connection.InputNodeId);

		if (OutputIt == Nodes.end() || InputIt == Nodes.end())
		{
			OutError = "Connection references a missing node.";
			return false;
		}

		const FMaterialExpressionNode* Output = OutputIt->second;
		const FMaterialExpressionNode* Input = InputIt->second;

		if (Connection.OutputNodeId == Connection.InputNodeId ||
			Connection.OutputPin < 0 || Connection.OutputPin >= GetExpressionOutputPinCount(Output->Type) ||
			Connection.InputPin < 0 || Connection.InputPin >= GetExpressionInputPinCount(Input->Type))
		{
			OutError = "Connection references an invalid pin.";
			return false;
		}

		if (!ConnectedInputs.insert({ Connection.InputNodeId, Connection.InputPin }).second)
		{
			OutError = "An input pin cannot have multiple connections.";
			return false;
		}
	}

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

		for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
		{
			if (Connection.OutputNodeId == NodeId && !Self(Self, Connection.InputNodeId))
			{
				return false;
			}
		}

		VisitState[NodeId] = 2;
		return true;
	};

	for (const auto& [NodeId, Node] : Nodes)
	{
		if (!Visit(Visit, NodeId))
		{
			OutError = "Material graph contains a circular dependency.";
			return false;
		}
	}

	const auto GetInputType = [&](int32 NodeId, int32 Pin)
	{
		for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
		{
			if (Connection.InputNodeId == NodeId && Connection.InputPin == Pin)
			{
				return GetExpressionOutputType(Connection.OutputNodeId, Connection.OutputPin);
			}
		}

		return EMaterialValueType::Unknown;
	};

	const auto IsConnected = [&](int32 NodeId, int32 Pin)
	{
		for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
		{
			if (Connection.InputNodeId == NodeId && Connection.InputPin == Pin)
			{
				return true;
			}
		}

		return false;
	};

	for (const FMaterialExpressionNode& Node : ExpressionNodes)
	{
		bool bUsed = false;

		for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
		{
			if (Connection.OutputNodeId == Node.Id)
			{
				bUsed = true;
				break;
			}
		}

		if (!bUsed)
		{
			continue;
		}

		int32 RequiredInputCount = 0;

		switch (Node.Type)
		{
		case EMaterialExpressionType::TextureSample:
		case EMaterialExpressionType::TextureSampleParameter2D:
		case EMaterialExpressionType::OneMinus:
		case EMaterialExpressionType::Abs:
		case EMaterialExpressionType::Frac:
		case EMaterialExpressionType::Sine:
		case EMaterialExpressionType::Normalize:
		case EMaterialExpressionType::ComponentMask:
		case EMaterialExpressionType::Saturate:
		case EMaterialExpressionType::Cosine:
		case EMaterialExpressionType::Floor:
		case EMaterialExpressionType::Length:
			RequiredInputCount = 1;
			break;

		case EMaterialExpressionType::Add:
		case EMaterialExpressionType::Subtract:
		case EMaterialExpressionType::Multiply:
		case EMaterialExpressionType::Divide:
		case EMaterialExpressionType::Power:
		case EMaterialExpressionType::Min:
		case EMaterialExpressionType::Max:
		case EMaterialExpressionType::DotProduct:
		case EMaterialExpressionType::AppendVector:
		case EMaterialExpressionType::Custom:
		case EMaterialExpressionType::Panner:
		case EMaterialExpressionType::Distance:
		case EMaterialExpressionType::CrossProduct:
		case EMaterialExpressionType::Desaturation:
			RequiredInputCount = 2;
			break;

		case EMaterialExpressionType::Lerp:
		case EMaterialExpressionType::Clamp:
		case EMaterialExpressionType::Fresnel:
		case EMaterialExpressionType::ConstantBiasScale:
		case EMaterialExpressionType::Rotator:
			RequiredInputCount = 3;
			break;
		case EMaterialExpressionType::SphereMask:
			RequiredInputCount = 4;
			break;
		default:
			break;
		}

		for (int32 Pin = 0; Pin < RequiredInputCount; ++Pin)
		{
			if (!IsConnected(Node.Id, Pin))
			{
				OutError = "Expression node has an unconnected required input.";
				return false;
			}
		}

		if (Node.Type == EMaterialExpressionType::TextureSample && Node.TexturePath.empty())
		{
			OutError = "Texture Sample requires a texture.";
			return false;
		}

		if (Node.Type == EMaterialExpressionType::TextureSampleParameter2D &&
			Node.ParameterName.empty())
		{
			OutError = "Texture Sample Parameter 2D requires a parameter name.";
			return false;
		}

		const EMaterialValueType A = GetInputType(Node.Id, 0);
		const EMaterialValueType B = GetInputType(Node.Id, 1);
		const EMaterialValueType C = GetInputType(Node.Id, 2);
		const EMaterialValueType D = GetInputType(Node.Id, 3);

		switch (Node.Type)
		{
		case EMaterialExpressionType::TextureSample:
		case EMaterialExpressionType::TextureSampleParameter2D:
			if (A != EMaterialValueType::Vector2)
			{
				OutError = "Texture Sample requires a float2 UV input.";
				return false;
			}
			break;

		case EMaterialExpressionType::Add:
		case EMaterialExpressionType::Subtract:
		case EMaterialExpressionType::Multiply:
		case EMaterialExpressionType::Divide:
		case EMaterialExpressionType::Power:
		case EMaterialExpressionType::Min:
		case EMaterialExpressionType::Max:
		case EMaterialExpressionType::Lerp:
			if (ResolveMaterialBinaryType(A, B) == EMaterialValueType::Unknown)
			{
				OutError = "Math node inputs must have matching types or one scalar input.";
				return false;
			}

			if (Node.Type == EMaterialExpressionType::Lerp && C != EMaterialValueType::Scalar)
			{
				OutError = "Linear Interpolate Alpha must be a scalar.";
				return false;
			}
			break;

		case EMaterialExpressionType::Clamp:
			if (!IsMaterialNumericType(A) ||
				(B != EMaterialValueType::Scalar && B != A) ||
				(C != EMaterialValueType::Scalar && C != A))
			{
				OutError = "Clamp requires scalar or matching numeric Min and Max inputs.";
				return false;
			}
			break;

		case EMaterialExpressionType::DotProduct:
			if (!IsMaterialVectorType(A) || A != B)
			{
				OutError = "Dot Product requires vectors of the same dimension.";
				return false;
			}
			break;

		case EMaterialExpressionType::AppendVector:
			if (GetMaterialValueWidth(A) + GetMaterialValueWidth(B) > 4 ||
				GetMaterialValueWidth(A) == 0 || GetMaterialValueWidth(B) == 0)
			{
				OutError = "Append Vector supports a maximum of four components.";
				return false;
			}
			break;

		case EMaterialExpressionType::Normalize:
			if (!IsMaterialVectorType(A))
			{
				OutError = "Normalize requires a vector input.";
				return false;
			}
			break;

		case EMaterialExpressionType::ComponentMask:
		{
			const int32 Width = GetMaterialValueWidth(A);
			const uint8 ValidMask = Width >= 4 ? 0x0F : static_cast<uint8>((1u << Width) - 1u);

			if (Width == 0 || (Node.ComponentMask & ~ValidMask) != 0 ||
				GetMaterialComponentMaskCount(Node.ComponentMask) == 0)
			{
				OutError = "Component Mask has invalid channels for its input type.";
				return false;
			}
			break;
		}

		case EMaterialExpressionType::Custom:
			if (!IsMaterialNumericType(A) || !IsMaterialNumericType(B) ||
				!IsMaterialNumericType(Node.CustomOutputType) || Node.CustomCode.empty())
			{
				OutError = "Custom Expression requires two numeric inputs, a numeric output type, and HLSL code.";
				return false;
			}
			break;

		case EMaterialExpressionType::Saturate:
		case EMaterialExpressionType::Cosine:
		case EMaterialExpressionType::Floor:
			if (!IsMaterialNumericType(A))
			{
				OutError = "This node requires a numeric input.";
				return false;
			}
			break;

		case EMaterialExpressionType::Panner:
			if (A != EMaterialValueType::Vector2 || B != EMaterialValueType::Vector2)
			{
				OutError = "Panner requires Vector2 Coordinate and Speed inputs.";
				return false;
			}
			break;

		case EMaterialExpressionType::Fresnel:
			if (A != EMaterialValueType::Vector3 ||
				B != EMaterialValueType::Scalar ||
				C != EMaterialValueType::Scalar)
			{
				OutError = "Fresnel requires a Vector3 Normal and Scalar Exponent and Base Reflect Fraction.";
				return false;
			}
			break;

		case EMaterialExpressionType::Distance:
			if (!IsMaterialVectorType(A) || A != B)
			{
				OutError = "Distance requires vectors of the same dimension.";
				return false;
			}
			break;

		case EMaterialExpressionType::Length:
			if (!IsMaterialVectorType(A))
			{
				OutError = "Length requires a vector input.";
				return false;
			}
			break;

		case EMaterialExpressionType::CrossProduct:
			if (A != EMaterialValueType::Vector3 || B != EMaterialValueType::Vector3)
			{
				OutError = "Cross Product requires two Vector3 inputs.";
				return false;
			}
			break;

		case EMaterialExpressionType::Desaturation:
			if ((A != EMaterialValueType::Vector3 && A != EMaterialValueType::Vector4) ||
				B != EMaterialValueType::Scalar)
			{
				OutError = "Desaturation requires a Vector3 or Vector4 Color and Scalar Fraction.";
				return false;
			}
			break;

		case EMaterialExpressionType::SphereMask:
			if (A != EMaterialValueType::Vector3 ||
				B != EMaterialValueType::Vector3 ||
				C != EMaterialValueType::Scalar ||
				D != EMaterialValueType::Scalar)
			{
				OutError = "Sphere Mask requires two Vector3 positions, Scalar Radius, and Scalar Softness.";
				return false;
			}
			break;

		case EMaterialExpressionType::ConstantBiasScale:
			if (!IsMaterialNumericType(A) ||
				(B != EMaterialValueType::Scalar && B != A) ||
				(C != EMaterialValueType::Scalar && C != A))
			{
				OutError = "Constant Bias Scale requires numeric Input, Bias, and Scale values.";
				return false;
			}
			break;

		case EMaterialExpressionType::Rotator:
			if (A != EMaterialValueType::Vector2 ||
				B != EMaterialValueType::Vector2 ||
				C != EMaterialValueType::Scalar)
			{
				OutError = "Rotator requires Vector2 Coordinate, Vector2 Center, and Scalar Angle.";
				return false;
			}
			break;

		default:
			break;
		}
	}

	for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
	{
		const FMaterialExpressionNode* Input = Nodes[Connection.InputNodeId];
		const EMaterialValueType SourceType = GetExpressionOutputType(Connection.OutputNodeId, Connection.OutputPin);

		if (SourceType == EMaterialValueType::Unknown)
		{
			OutError = "Cannot resolve expression output type.";
			return false;
		}

		if (Input->Type != EMaterialExpressionType::MaterialOutput)
		{
			continue;
		}

		bool bValidType = false;

		switch (Connection.InputPin)
		{
		case 0:
		case 1:
		case 3:
			bValidType = SourceType == EMaterialValueType::Vector3 ||
				SourceType == EMaterialValueType::Vector4;
			break;

		case 2:
		case 4:
		case 5:
		case 6:
			bValidType = SourceType == EMaterialValueType::Scalar;
			break;

		case 7:
			bValidType = SourceType == EMaterialValueType::Vector3;
			break;

		default:
			break;
		}

		if (!bValidType)
		{
			OutError = "Material Output pin type mismatch.";
			return false;
		}
	}

	OutError.clear();
	return true;
}

void UMaterial::ApplyImportedProperties(const FImportedMaterialProperties& Properties)
{
	SpecularColor = Properties.SpecularColor;
	SpecularFactor = Properties.SpecularFactor;
	EmissiveColor = Properties.EmissiveColor;
	EmissiveFactor = Properties.EmissiveFactor;
	Shininess = Properties.Shininess;
	ShadingModel = Properties.ShadingModel;
	bOverrideShadingModel = Properties.bOverrideShadingModel;
}

void UMaterial::GetRenderProxy(FMaterialRenderProxy& OutProxy) const
{
	OutProxy = FMaterialRenderProxy{};
	OutProxy.BaseMaterial = this;
	OutProxy.Textures = Textures;
	OutProxy.GraphTextures.Reset();
	OutProxy.GraphTextureNodeIds.Reset();

	for (const FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Type != EMaterialExpressionType::TextureSample &&
			Node.Type != EMaterialExpressionType::TextureSampleParameter2D)
		{
			continue;
		}

		OutProxy.GraphTextureNodeIds.Add(Node.Id);
		OutProxy.GraphTextures.Add(Node.TexturePath.empty() ? nullptr : FindOrLoadTexture(Node.TexturePath));
	}

	OutProxy.BaseColor = BaseColor;
	OutProxy.SpecularColor = SpecularColor;
	OutProxy.Shininess = Shininess;
	OutProxy.UVScrollSpeed = UVScrollSpeed;
	OutProxy.bFlipNormalY = bFlipNormalY;
	OutProxy.SpecularFactor = SpecularFactor;
	OutProxy.EmissiveColor = EmissiveColor;
	OutProxy.EmissiveFactor = EmissiveFactor;
	OutProxy.UVTiling = UVTiling;
	OutProxy.WorldUVTileSize = WorldUVTileSize;
	OutProxy.bWorldUVLocalSpace = bWorldUVLocalSpace;
	OutProxy.ShadingModel = ShadingModel;
	OutProxy.bOverrideShadingModel = bOverrideShadingModel;
	OutProxy.PSOType = PSOType;
	OutProxy.BlendState = BlendState;
	OutProxy.DepthStencilState = DepthStencilState;
	OutProxy.SamplerState = SamplerState;
}

void UMaterial::SetBlendState(EBlendState InBlendState)
{
	BlendState = InBlendState;
	if (PSOType == EPSOType::StaticMesh_Opaque || PSOType == EPSOType::StaticMesh_Translucent || PSOType == EPSOType::StaticMesh_Wireframe)
	{
		PSOType = BlendState == EBlendState::Opaque ? EPSOType::StaticMesh_Opaque : EPSOType::StaticMesh_Translucent;
		DepthStencilState = BlendState == EBlendState::Opaque ? EDepthStencilState::Default : EDepthStencilState::ReadOnly;
	}
}

UMaterialInstanceDynamic* UMaterial::CreateDynamicInstance(UMaterialInterface* Parent)
{
	if (!Parent)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = NewObject<UMaterialInstanceDynamic>();
	if (!Instance)
	{
		return nullptr;
	}

	Instance->SetParent(Parent);
	return Instance;
}

json UMaterial::SaveMaterial(const UMaterialInterface* Material)
{
	if (!Material)
	{
		return nullptr;
	}

	if (!Material->GetPath().empty())
	{
		return json{ { "Asset", Material->GetPath() } };
	}

	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	if (!Instance || !Instance->GetParent())
	{
		return nullptr;
	}

	json Out;
	Out["Base"] = Instance->GetParent()->GetPath();
	json Parameters = json::array();
	for (const FMaterialParameterOverride& Override : Instance->GetParameterOverrides())
	{
		json Parameter;
		Parameter["Name"] = Override.ParameterName.ToString();
		Parameter["Type"] = static_cast<uint8>(Override.Type);
		if (Override.Type == EMaterialParameterType::Scalar)
		{
			Parameter["Scalar"] = Override.ScalarValue;
		}
		else if (Override.Type == EMaterialParameterType::Vector)
		{
			Parameter["Vector"] = Override.VectorValue;
		}
		else if (Override.Type == EMaterialParameterType::Texture)
		{
			Parameter["Texture"] = Override.TextureValue ? Override.TextureValue->GetPath() : FString();
		}
		Parameters.push_back(Parameter);
	}
	Out["Parameters"] = Parameters;
	return Out;
}

UMaterialInterface* UMaterial::LoadMaterial(const json& In)
{
	if (!In.is_object())
	{
		return nullptr;
	}

	if (In.contains("Asset"))
	{
		return UAssetManager::LoadMaterialAsset(In["Asset"].get<FString>());
	}

	FString BasePath = In.value("Base", FString("DefaultMaterial"));
	UMaterialInterface* Base = UAssetManager::LoadMaterialAsset(BasePath);
	if (!Base)
	{
		Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
	}
	if (!Base)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = NewObject<UMaterialInstanceDynamic>();
	if (!Instance)
	{
		return nullptr;
	}
	Instance->SetParent(Base);

	if (In.contains("Parameters") && In["Parameters"].is_array())
	{
		for (const json& Parameter : In["Parameters"])
		{
			if (!Parameter.is_object() || !Parameter.contains("Name") || !Parameter.contains("Type"))
			{
				continue;
			}
			const FName Name(Parameter["Name"].get<FString>());
			const EMaterialParameterType Type = static_cast<EMaterialParameterType>(Parameter["Type"].get<uint8>());
			if (Type == EMaterialParameterType::Scalar && Parameter.contains("Scalar"))
			{
				Instance->SetScalarParameterValue(Name, Parameter["Scalar"].get<float>());
			}
			else if (Type == EMaterialParameterType::Vector && Parameter.contains("Vector"))
			{
				FVector4 Value;
				Parameter["Vector"].get_to(Value);
				Instance->SetVectorParameterValue(Name, Value);
			}
			else if (Type == EMaterialParameterType::Texture && Parameter.contains("Texture"))
			{
				const FString Path = Parameter["Texture"].get<FString>();
				Instance->SetTextureParameterValue(Name, Path.empty() ? nullptr : UAssetManager::Get().LoadTexture(Path));
			}
		}
	}

	if (In.contains("BaseColor"))
	{
		FVector4 Color;
		In["BaseColor"].get_to(Color);
		Instance->SetBaseColorOverride(Color);
	}

	FString TexturePath;
	if (In.contains("Texture0Override"))
	{
		TexturePath = In["Texture0Override"].get<FString>();
	}
	else if (In.contains("Textures") && In["Textures"].is_array() && !In["Textures"].empty() && In["Textures"][0].is_string())
	{
		TexturePath = In["Textures"][0].get<FString>();
	}
	if (!TexturePath.empty())
	{
		Instance->SetTexture0Override(UAssetManager::Get().LoadTexture(TexturePath));
	}
	return Instance;
}

void UMaterial::SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterialInterface*& Material)
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	const bool bIsAsset = Ar.IsSaving() && Material && !Material->GetPath().empty();
	if (auto AssetSlot = Record.TryEnterField("Asset", bIsAsset))
	{
		FString Path = Ar.IsSaving() ? Material->GetPath() : FString();
		AssetSlot.GetValue() << Path;
		if (Ar.IsLoading())
		{
			Material = UAssetManager::LoadMaterialAsset(Path);
		}
		return;
	}

	const bool bIsInstance = Ar.IsSaving() && Cast<UMaterialInstance>(Material) != nullptr;
	auto InstanceSlot = Record.TryEnterField("Instance", bIsInstance);
	if (!InstanceSlot.IsSet())
	{
		if (Ar.IsLoading())
		{
			Material = nullptr;
		}
		return;
	}
	if (Ar.IsLoading())
	{
		Material = NewObject<UMaterialInstanceDynamic>();
	}
	if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
	{
		Instance->Serialize(InstanceSlot.GetValue().EnterRecord());
	}
}

void UMaterial::CopySettingsFrom(const UMaterial& Source)
{
	UsageFlags = Source.UsageFlags;
	Shader = Source.Shader;
	Textures = Source.Textures;
	BlendState = Source.BlendState;
	DepthStencilState = Source.DepthStencilState;
	SamplerState = Source.SamplerState;
	BaseColor = Source.BaseColor;
	SpecularColor = Source.SpecularColor;
	Shininess = Source.Shininess;
	UVScrollSpeed = Source.UVScrollSpeed;
	bFlipNormalY = Source.bFlipNormalY;
	SpecularFactor = Source.SpecularFactor;
	EmissiveColor = Source.EmissiveColor;
	EmissiveFactor = Source.EmissiveFactor;
	UVTiling = Source.UVTiling;
	WorldUVTileSize = Source.WorldUVTileSize;
	bWorldUVLocalSpace = Source.bWorldUVLocalSpace;
	ShadingModel = Source.ShadingModel;
	bOverrideShadingModel = Source.bOverrideShadingModel;
	PSOType = Source.PSOType;
	ExpressionNodes = Source.ExpressionNodes;
	ExpressionConnections = Source.ExpressionConnections;
	OutputNodeId = Source.OutputNodeId;
	EnsureDefaultGraph();

	ParamBuffer.reset();
	if (Source.ParamBuffer)
	{
		ParamBuffer = RenderCommand::CreateConstantBuffer(Source.ParamBuffer->GetBufferSize());
	}
}

void UMaterial::InitializeRenderResources(const UMaterial& Template)
{
	Shader = Template.Shader;
	DepthStencilState = Template.DepthStencilState;
	ParamBuffer.reset();
	if (Template.ParamBuffer)
	{
		ParamBuffer = RenderCommand::CreateConstantBuffer(Template.ParamBuffer->GetBufferSize());
	}
}

void UMaterial::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();

	if (TOptional<FStructuredArchive::FSlot> ColorSlot = Record.TryEnterField("BaseColor", Ar.IsSaving())) ColorSlot.GetValue() << BaseColor;
	else if (Ar.IsTextFormat())
	{
		if (TOptional<FStructuredArchive::FSlot> LegacyColor = Record.TryEnterField("DiffuseOrFactor", false))
			LegacyColor.GetValue() << BaseColor;
	}
	Record << SA_VALUE("Shininess", Shininess) << SA_VALUE("UVScrollSpeed", UVScrollSpeed);
	auto Optional = [&]<typename T>(const char* Name, T& Value)
	{
		if (auto Slot = Record.TryEnterField(Name, Ar.IsSaving())) Slot.GetValue() << Value;
	};
	Optional("SpecularColor", SpecularColor);
	Optional("FlipNormalY", bFlipNormalY);
	Optional("SpecularFactor", SpecularFactor);
	Optional("EmissiveColor", EmissiveColor);
	Optional("EmissiveFactor", EmissiveFactor);
	Optional("UVTiling", UVTiling);
	Optional("WorldUVTileSize", WorldUVTileSize);
	Optional("WorldUVLocalSpace", bWorldUVLocalSpace);
	Optional("OverrideShadingModel", bOverrideShadingModel);
	uint8 Model = static_cast<uint8>(ShadingModel);
	Optional("ShadingModel", Model);
	if (Ar.IsLoading()) ShadingModel = static_cast<ELightingModel>(std::min<uint8>(Model, 3));

	FString Sampler = SamplerState == ESamplerState::LinearWrap ? "LinearWrap" : "LinearClamp";
	Record << SA_VALUE("SamplerState", Sampler);
	if (Ar.IsLoading()) SamplerState = Sampler == "LinearWrap" ? ESamplerState::LinearWrap : ESamplerState::LinearClamp;
	FString Blend = "Opaque";
	if (BlendState == EBlendState::AlphaBlend)
		Blend = "AlphaBlend";
	else if (BlendState == EBlendState::Additive)
		Blend = "Additive";
	Record << SA_VALUE("BlendState", Blend);
	if (Ar.IsLoading())
	{
		if (Blend == "AlphaBlend")
			BlendState = EBlendState::AlphaBlend;
		else if (Blend == "Additive")
			BlendState = EBlendState::Additive;
		else
			BlendState = EBlendState::Opaque;
	}
	uint8 PSOVal = static_cast<uint8>(PSOType);
	Record << SA_VALUE("PSOType", PSOVal);
	if (Ar.IsLoading()) PSOType = static_cast<EPSOType>(PSOVal);
	if (Ar.IsLoading()) SetBlendState(BlendState);

	if (auto UsageSlot = Record.TryEnterField("UsageFlags", Ar.IsSaving()))
	{
		UsageSlot.GetValue() << UsageFlags;
	}
	else if (Ar.IsLoading())
	{
		UsageFlags = static_cast<uint32>(EMaterialUsage::StaticMesh);

		if (PSOType == EPSOType::Particle_Sprite || PSOType == EPSOType::Particle_AlphaBlend || PSOType == EPSOType::Particle_Additive)
		{
			UsageFlags = static_cast<uint32>(EMaterialUsage::ParticleSprite);
		}
		else if (PSOType == EPSOType::Particle_MeshOpaque || PSOType == EPSOType::Particle_MeshTranslucent || PSOType == EPSOType::Particle_MeshAdditive)
		{
			UsageFlags = static_cast<uint32>(EMaterialUsage::ParticleMesh);
		}
		else if (PSOType == EPSOType::Particle_BeamTrailTranslucent || PSOType == EPSOType::Particle_BeamTrailAdditive)
		{
			UsageFlags = static_cast<uint32>(EMaterialUsage::ParticleBeamTrail);
		}
	}

	int32 Num = Textures.Num();
	FStructuredArchive::FArray TextureArray = Record.EnterArray("Textures", Num);
	if (Ar.IsLoading()) Textures.Reset();
	for (int32 i = 0; i < Num; ++i)
	{
		FString Path;
		if (Ar.IsSaving() && Textures[i]) Path = Textures[i]->GetPath();
		TextureArray.EnterElement() << Path;
		if (Ar.IsLoading()) Textures.Add(Path.empty() ? nullptr : FindOrLoadTexture(Path));
	}

	int32 NodeCount = ExpressionNodes.Num();

	if (auto NodeSlot = Record.TryEnterField("ExpressionNodes", Ar.IsSaving()))
	{
		FStructuredArchive::FArray NodeArray = NodeSlot.GetValue().EnterArray(NodeCount);

		if (Ar.IsLoading())
		{
			ExpressionNodes.Reset();
		}

		for (int32 Index = 0; Index < NodeCount; ++Index)
		{
			FMaterialExpressionNode Node = Ar.IsSaving() ? ExpressionNodes[Index] : FMaterialExpressionNode{};
			uint8 Type = static_cast<uint8>(Node.Type);

			FStructuredArchive::FRecord NodeRecord = NodeArray.EnterElement().EnterRecord();
			NodeRecord << SA_VALUE("Id", Node.Id);
			NodeRecord << SA_VALUE("Type", Type);
			NodeRecord << SA_VALUE("Position", Node.Position);
			NodeRecord << SA_VALUE("ScalarValue", Node.ScalarValue);
			NodeRecord << SA_VALUE("VectorValue", Node.VectorValue);
			NodeRecord << SA_VALUE("ParameterName", Node.ParameterName);
			NodeRecord << SA_VALUE("ParameterGroup", Node.ParameterGroup);
			NodeRecord << SA_VALUE("TexturePath", Node.TexturePath);
			NodeRecord << SA_VALUE("CoordinateIndex", Node.CoordinateIndex);

			if (auto MaskSlot = NodeRecord.TryEnterField("ComponentMask", Ar.IsSaving()))
			{
				MaskSlot.GetValue() << Node.ComponentMask;
			}

			if (auto CustomCodeSlot = NodeRecord.TryEnterField("CustomCode", Ar.IsSaving()))
			{
				CustomCodeSlot.GetValue() << Node.CustomCode;
			}

			uint8 CustomType = static_cast<uint8>(Node.CustomOutputType);

			if (auto CustomTypeSlot = NodeRecord.TryEnterField("CustomOutputType", Ar.IsSaving()))
			{
				CustomTypeSlot.GetValue() << CustomType;
			}

			if (Ar.IsLoading())
			{
				Node.CustomOutputType = CustomType <= static_cast<uint8>(EMaterialValueType::Vector4)
					? static_cast<EMaterialValueType>(CustomType)
					: EMaterialValueType::Vector3;
			}

			if (auto SRGBSlot = NodeRecord.TryEnterField("SRGB", Ar.IsSaving()))
			{
				SRGBSlot.GetValue() << Node.bSRGB;
			}

			if (Ar.IsLoading())
			{
				if (Type >= static_cast<uint8>(EMaterialExpressionType::Count))
				{
					continue;
				}

				Node.Type = static_cast<EMaterialExpressionType>(Type);
				ExpressionNodes.Add(Node);
			}
		}
	}

	int32 ConnectionCount = ExpressionConnections.Num();

	if (auto ConnectionSlot = Record.TryEnterField("ExpressionConnections", Ar.IsSaving()))
	{
		FStructuredArchive::FArray ConnectionArray = ConnectionSlot.GetValue().EnterArray(ConnectionCount);

		if (Ar.IsLoading())
		{
			ExpressionConnections.Reset();
		}

		for (int32 Index = 0; Index < ConnectionCount; ++Index)
		{
			FMaterialExpressionConnection Connection = Ar.IsSaving() ? ExpressionConnections[Index] : FMaterialExpressionConnection{};
			FStructuredArchive::FRecord ConnectionRecord = ConnectionArray.EnterElement().EnterRecord();

			ConnectionRecord << SA_VALUE("OutputNodeId", Connection.OutputNodeId);
			ConnectionRecord << SA_VALUE("OutputPin", Connection.OutputPin);
			ConnectionRecord << SA_VALUE("InputNodeId", Connection.InputNodeId);
			ConnectionRecord << SA_VALUE("InputPin", Connection.InputPin);

			if (Ar.IsLoading())
			{
				ExpressionConnections.Add(Connection);
			}
		}
	}

	if (auto OutputSlot = Record.TryEnterField("OutputNodeId", Ar.IsSaving()))
	{
		OutputSlot.GetValue() << OutputNodeId;
	}

	if (Ar.IsLoading())
	{
		EnsureDefaultGraph();
	}
}
