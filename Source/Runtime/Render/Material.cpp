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

namespace
{
	// 저장된 경로로 텍스처를 찾고, 아직 로드 전이면 로드한다
	UTexture2D* FindOrLoadTexture(const FString& Path)
	{
		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Path))
		{
			return Texture;
		}
		return UAssetManager::Get().LoadTexture(Path);
	}
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

// 머티리얼 하나를 JSON으로.
// 에셋이면 경로만, 경로 없는 인스턴스면 "기준 에셋 + 텍스처 + 색"으로 풀어서 쓴다
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

// Material.cpp
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
	Super::Serialize(Record);   // "Properties" (리플렉션 프로퍼티. 지금은 없음)

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (auto ColorSlot = Record.TryEnterField("BaseColor", Ar.IsSaving())) ColorSlot.GetValue() << BaseColor;
	else if (auto LegacyColor = Record.TryEnterField("DiffuseOrFactor", false)) LegacyColor.GetValue() << BaseColor;
	Record << SA_VALUE("Shininess", Shininess) << SA_VALUE("UVScrollSpeed", UVScrollSpeed);
	// Optional fields preserve older scenes and clipboard records.
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

	// enum은 이름 문자열로 (UE 텍스트 포맷도 enum을 이름으로 쓴다)
	FString Sampler = SamplerState == ESamplerState::LinearWrap ? "LinearWrap" : "LinearClamp";
	Record << SA_VALUE("SamplerState", Sampler);
	if (Ar.IsLoading())
		SamplerState = Sampler == "LinearWrap" ? ESamplerState::LinearWrap : ESamplerState::LinearClamp;

	FString Blend = BlendState == EBlendState::AlphaBlend ? "AlphaBlend" : "Opaque";
	Record << SA_VALUE("BlendState", Blend);
	if (Ar.IsLoading())
		BlendState = Blend == "AlphaBlend" ? EBlendState::AlphaBlend : EBlendState::Opaque;

	uint8 PSOVal = static_cast<uint8>(PSOType);
	Record << SA_VALUE("PSOType", PSOVal);
	if (Ar.IsLoading())
		PSOType = static_cast<EPSOType>(PSOVal);
	if (Ar.IsLoading()) SetBlendState(BlendState);

	// 텍스처: 경로 문자열 배열. 없는 칸은 빈 문자열
	int32 Num = Textures.Num();
	FStructuredArchive::FArray TextureArray = Record.EnterArray("Textures", Num);
	if (Ar.IsLoading())
		Textures.Reset();

	for (int32 i = 0; i < Num; ++i)
	{
		FString Path;
		if (Ar.IsSaving() && Textures[i])
			Path = Textures[i]->GetPath();

		TextureArray.EnterElement() << Path;

		if (Ar.IsLoading())
			Textures.Add(Path.empty() ? nullptr : FindOrLoadTexture(Path));
	}
}