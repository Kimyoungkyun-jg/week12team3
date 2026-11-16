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
		Output.Id = MaxId + 1;
		Output.Type = EMaterialExpressionType::MaterialOutput;
		Output.Position = FVector2(650.0f, 150.0f);

		ExpressionNodes.Add(Output);
		OutputNodeId = Output.Id;
	}
	else if (OutputNodeId == 0 || !FindExpressionNode(*this, OutputNodeId))
	{
		OutputNodeId = FirstOutputId;
	}
}

int32 UMaterial::GetExpressionInputPinCount(EMaterialExpressionType Type) const
{
	switch (Type)
	{
	case EMaterialExpressionType::TextureSample:
		return 1;
	case EMaterialExpressionType::Add:
	case EMaterialExpressionType::Multiply:
		return 2;
	case EMaterialExpressionType::Lerp:
		return 3;
	case EMaterialExpressionType::OneMinus:
		return 1;
	case EMaterialExpressionType::MaterialOutput:
		return 5;
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
	static const char* MathNames[] = { "A", "B", "Alpha" };
	static const char* OutputNames[] = { "Base Color", "Specular Color", "Shininess", "Emissive Color", "Emissive Factor" };

	switch (Type)
	{
	case EMaterialExpressionType::TextureSample:
		return PinIndex == 0 ? "UV" : nullptr;
	case EMaterialExpressionType::Add:
	case EMaterialExpressionType::Multiply:
		return PinIndex >= 0 && PinIndex < 2 ? MathNames[PinIndex] : nullptr;
	case EMaterialExpressionType::Lerp:
		return PinIndex >= 0 && PinIndex < 3 ? MathNames[PinIndex] : nullptr;
	case EMaterialExpressionType::OneMinus:
		return PinIndex == 0 ? "Input" : nullptr;
	case EMaterialExpressionType::MaterialOutput:
		return PinIndex >= 0 && PinIndex < 5 ? OutputNames[PinIndex] : nullptr;
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

		EMaterialValueType Result = EMaterialValueType::Unknown;

		switch (Node->Type)
		{
		case EMaterialExpressionType::Constant:
		case EMaterialExpressionType::ScalarParameter:
			Result = EMaterialValueType::Scalar;
			break;

		case EMaterialExpressionType::Constant2Vector:
		case EMaterialExpressionType::TextureCoordinate:
			Result = EMaterialValueType::Vector2;
			break;

		case EMaterialExpressionType::Constant3Vector:
			Result = EMaterialValueType::Vector3;
			break;

		case EMaterialExpressionType::Constant4Vector:
		case EMaterialExpressionType::VectorParameter:
		case EMaterialExpressionType::TextureSample:
			Result = EMaterialValueType::Vector4;
			break;

		case EMaterialExpressionType::Add:
		case EMaterialExpressionType::Multiply:
		{
			const EMaterialValueType A = GetInputType(0);
			const EMaterialValueType B = GetInputType(1);

			if (A != EMaterialValueType::Unknown && B != EMaterialValueType::Unknown && A != B)
			{
				Result = EMaterialValueType::Unknown;
			}
			else
			{
				Result = A != EMaterialValueType::Unknown ? A : B;
			}
			break;
		}

		case EMaterialExpressionType::Lerp:
		{
			const EMaterialValueType A = GetInputType(0);
			const EMaterialValueType B = GetInputType(1);

			if (A != EMaterialValueType::Unknown && B != EMaterialValueType::Unknown && A != B)
			{
				Result = EMaterialValueType::Unknown;
			}
			else
			{
				Result = A != EMaterialValueType::Unknown ? A : B;
			}
			break;
		}

		case EMaterialExpressionType::OneMinus:
			Result = GetInputType(0);
			break;

		default:
			break;
		}

		Visiting.erase(CurrentId);
		return Result;
	};

	return Resolve(Resolve, NodeId);
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
	std::set<FString> ParameterNames;

	int32 OutputCount = 0;

	for (const FMaterialExpressionNode& Node : ExpressionNodes)
	{
		if (Node.Id <= 0 || !Nodes.emplace(Node.Id, &Node).second)
		{
			OutError = "Duplicate or invalid expression node ID.";
			return false;
		}

		if (static_cast<uint8>(Node.Type) > static_cast<uint8>(EMaterialExpressionType::MaterialOutput))
		{
			OutError = "Invalid expression node type.";
			return false;
		}

		if (Node.Type == EMaterialExpressionType::MaterialOutput)
		{
			++OutputCount;
		}

		if (Node.Type == EMaterialExpressionType::ScalarParameter || Node.Type == EMaterialExpressionType::VectorParameter)
		{
			if (Node.ParameterName.empty() || !ParameterNames.insert(Node.ParameterName).second)
			{
				OutError = "Parameter names must be unique and non-empty.";
				return false;
			}
		}

		if (Node.Type == EMaterialExpressionType::TextureCoordinate && (Node.CoordinateIndex < 0 || Node.CoordinateIndex > 7))
		{
			OutError = "Texture Coordinate index must be between 0 and 7.";
			return false;
		}
	}

	if (OutputCount != 1 || !Nodes.contains(OutputNodeId) ||
		Nodes[OutputNodeId]->Type != EMaterialExpressionType::MaterialOutput)
	{
		OutError = "Material must have exactly one valid Output node.";
		return false;
	}

	std::set<std::pair<int32, int32>> ConnectedInputs;

	for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
	{
		auto OutputIt = Nodes.find(Connection.OutputNodeId);
		auto InputIt = Nodes.find(Connection.InputNodeId);

		if (OutputIt == Nodes.end() || InputIt == Nodes.end())
		{
			OutError = "Connection references a missing node.";
			return false;
		}

		const auto* Output = OutputIt->second;
		const auto* Input = InputIt->second;

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

	const auto IsNumeric = [](EMaterialValueType Type)
	{
		return Type == EMaterialValueType::Scalar || Type == EMaterialValueType::Vector2 ||
			Type == EMaterialValueType::Vector3 || Type == EMaterialValueType::Vector4;
	};

	for (const FMaterialExpressionConnection& Connection : ExpressionConnections)
	{
		const auto* Input = Nodes[Connection.InputNodeId];
		const EMaterialValueType SourceType = GetExpressionOutputType(Connection.OutputNodeId, Connection.OutputPin);

		if (SourceType == EMaterialValueType::Unknown)
		{
			continue;
		}

		bool bValidType = true;

		switch (Input->Type)
		{
		case EMaterialExpressionType::TextureSample:
			bValidType = SourceType == EMaterialValueType::Vector2;
			break;

		case EMaterialExpressionType::Add:
		case EMaterialExpressionType::Multiply:
		case EMaterialExpressionType::OneMinus:
			bValidType = IsNumeric(SourceType);
			break;

		case EMaterialExpressionType::Lerp:
			bValidType = Connection.InputPin == 2 ? SourceType == EMaterialValueType::Scalar : IsNumeric(SourceType);
			break;

		case EMaterialExpressionType::MaterialOutput:
			if (Connection.InputPin == 0 || Connection.InputPin == 1 || Connection.InputPin == 3)
			{
				bValidType = SourceType == EMaterialValueType::Vector3 || SourceType == EMaterialValueType::Vector4;
			}
			else
			{
				bValidType = SourceType == EMaterialValueType::Scalar;
			}
			break;

		default:
			break;
		}

		if (!bValidType)
		{
			OutError = "Expression pin type mismatch.";
			return false;
		}

		if (Input->Type == EMaterialExpressionType::Add || Input->Type == EMaterialExpressionType::Multiply ||
			Input->Type == EMaterialExpressionType::Lerp)
		{
			for (const FMaterialExpressionConnection& Other : ExpressionConnections)
			{
				if (Other.InputNodeId != Connection.InputNodeId || Other.InputPin == Connection.InputPin)
				{
					continue;
				}

				if (Input->Type == EMaterialExpressionType::Lerp && (Other.InputPin == 2 || Connection.InputPin == 2))
				{
					continue;
				}

				const EMaterialValueType OtherType = GetExpressionOutputType(Other.OutputNodeId, Other.OutputPin);

				if (OtherType != EMaterialValueType::Unknown && OtherType != SourceType)
				{
					OutError = "Math node inputs must have matching types.";
					return false;
				}
			}
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
	ParamLayout = Source.ParamLayout;
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
	ParamLayout = Template.ParamLayout;
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

	if (auto ColorSlot = Record.TryEnterField("BaseColor", Ar.IsSaving())) ColorSlot.GetValue() << BaseColor;
	else if (auto LegacyColor = Record.TryEnterField("DiffuseOrFactor", false)) LegacyColor.GetValue() << BaseColor;
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
	FString Blend = BlendState == EBlendState::AlphaBlend ? "AlphaBlend" : "Opaque";
	Record << SA_VALUE("BlendState", Blend);
	if (Ar.IsLoading()) BlendState = Blend == "AlphaBlend" ? EBlendState::AlphaBlend : EBlendState::Opaque;
	uint8 PSOVal = static_cast<uint8>(PSOType);
	Record << SA_VALUE("PSOType", PSOVal);
	if (Ar.IsLoading()) PSOType = static_cast<EPSOType>(PSOVal);
	if (Ar.IsLoading()) SetBlendState(BlendState);

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

			if (Ar.IsLoading())
			{
				if (Type > static_cast<uint8>(EMaterialExpressionType::MaterialOutput))
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
