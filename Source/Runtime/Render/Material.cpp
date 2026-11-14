#include "EnginePCH.h"

#include "RenderCommand.h"

#include "Material.h"
#include "Render/MaterialRenderProxy.h"

#include "Asset/AssetManager.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"

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

const UMaterial* UMaterial::GetBaseAsset() const
{
	for (const UMaterial* Current = this; Current; Current = Current->Parent)
	{
		if (!Current->bIsInstance && !Current->GetPath().empty())
		{
			return Current;
		}
	}

	return nullptr;
}

// 머티리얼 하나를 JSON으로.
// 에셋이면 경로만, 경로 없는 인스턴스면 "기준 에셋 + 텍스처 + 색"으로 풀어서 쓴다
json UMaterial::SaveMaterial(const UMaterial* Material)
{
	json Out;
	if (!Material) return json::object();

	if (!Material->GetPath().empty())
	{
		Out["Asset"] = Material->GetPath();
		return Out;
	}

	const UMaterial* Base = Material->GetBaseAsset();
	Out["Base"] = Base ? Base->GetPath() : FString("DefaultMaterial");
	Out["PSOType"] = static_cast<uint8>(Material->PSOType);
	Out["BaseColor"] = Material->BaseColor;
	Out["SpecularColor"] = Material->SpecularColor;
	Out["Shininess"] = Material->Shininess;
	Out["SpecularFactor"] = Material->SpecularFactor;
	Out["EmissiveColor"] = Material->EmissiveColor;
	Out["EmissiveFactor"] = Material->EmissiveFactor;
	Out["UVTiling"] = { Material->UVTiling.X, Material->UVTiling.Y };
	Out["WorldUVTileSize"] = { Material->WorldUVTileSize.X, Material->WorldUVTileSize.Y };
	Out["WorldUVLocalSpace"] = Material->bWorldUVLocalSpace;
	Out["ShadingModel"] = static_cast<int>(Material->ShadingModel);
	Out["OverrideShadingModel"] = Material->bOverrideShadingModel;
	Out["FlipNormalY"] = Material->bFlipNormalY;
	Out["UVScrollSpeed"] = {
		Material->UVScrollSpeed.X,
		Material->UVScrollSpeed.Y
	};
	Out["SamplerState"] = Material->SamplerState == ESamplerState::LinearWrap ? "LinearWrap" : "LinearClamp";
	Out["BlendState"] = Material->BlendState == EBlendState::AlphaBlend ? "AlphaBlend" : "Opaque";

	json Textures = json::array();
	for (UTexture2D* Texture : Material->Textures)
	{
		if (Texture && !Texture->GetPath().empty())
			Textures.push_back(Texture->GetPath());
		else
			Textures.push_back(nullptr);
	}
	Out["Textures"] = Textures;

	return Out;
}

UMaterial* UMaterial::LoadMaterial(const json& In)
{
	if (In.contains("Asset"))
	{
		return UAssetManager::GetAssetByPath<UMaterial>(In["Asset"].get<FString>());
	}

	UMaterial* Base = nullptr;
	if (In.contains("Base"))
	{
		Base = UAssetManager::GetAssetByPath<UMaterial>(In["Base"].get<FString>());
	}
	if (!Base)
	{
		Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
	}

	UMaterial* Instance = UMaterial::CreateInstance(Base);
	if (!Instance)
	{
		return nullptr;
	}

	if (In.contains("BaseColor"))
	{
		In["BaseColor"].get_to(Instance->BaseColor);
	}
	else if (In.contains("DiffuseOrFactor")) In["DiffuseOrFactor"].get_to(Instance->BaseColor);
	if (In.contains("SpecularColor"))
	{
		In["SpecularColor"].get_to(Instance->SpecularColor);
	}
	if (In.contains("Shininess"))
	{
		Instance->Shininess = std::max(1.0f, In["Shininess"].get<float>());
	}
	if (In.contains("FlipNormalY"))
	{
		Instance->bFlipNormalY = In["FlipNormalY"].get<bool>();
	}
	if (In.contains("SpecularFactor")) Instance->SpecularFactor = std::max(0.0f, In["SpecularFactor"].get<float>());
	if (In.contains("EmissiveColor")) In["EmissiveColor"].get_to(Instance->EmissiveColor);
	if (In.contains("EmissiveFactor")) Instance->EmissiveFactor = std::max(0.0f, In["EmissiveFactor"].get<float>());
	if (In.contains("UVTiling")) Instance->UVTiling = FVector2(In["UVTiling"][0].get<float>(), In["UVTiling"][1].get<float>());
	if (In.contains("WorldUVTileSize")) Instance->WorldUVTileSize = FVector2(In["WorldUVTileSize"][0].get<float>(), In["WorldUVTileSize"][1].get<float>());
	if (In.contains("WorldUVLocalSpace")) Instance->bWorldUVLocalSpace = In["WorldUVLocalSpace"].get<bool>();
	if (In.contains("ShadingModel")) Instance->ShadingModel = static_cast<ELightingModel>(std::clamp(In["ShadingModel"].get<int>(), 0, 3));
	if (In.contains("OverrideShadingModel")) Instance->bOverrideShadingModel = In["OverrideShadingModel"].get<bool>();

	if (In.contains("Textures"))
	{
		Instance->Textures.Reset();
		for (const json& TexturePath : In["Textures"])
		{
			Instance->Textures.Add(TexturePath.is_null() ? nullptr : FindOrLoadTexture(TexturePath.get<FString>()));
		}
	}
	if (In.contains("UVScrollSpeed"))
	{
		const auto& UV = In["UVScrollSpeed"];
		Instance->UVScrollSpeed = FVector2(UV[0].get<float>(), UV[1].get<float>());
	}
	if (In.contains("SamplerState"))
	{
		const FString State = In["SamplerState"].get<FString>();
		Instance->SamplerState = State == "LinearWrap" ? ESamplerState::LinearWrap : ESamplerState::LinearClamp;
	}
	if (In.contains("PSOType"))
	{
		Instance->PSOType = static_cast<EPSOType>(In["PSOType"].get<uint8>());
	}
	else if (In.contains("BlendState"))
	{
		const FString State = In["BlendState"].get<FString>();
		Instance->PSOType = (State == "AlphaBlend")
			? EPSOType::StaticMesh_Translucent
			: EPSOType::StaticMesh_Opaque;
		Instance->BlendState = State == "AlphaBlend" ? EBlendState::AlphaBlend : EBlendState::Opaque;
	}

	if (In.contains("BlendState"))
		Instance->SetBlendState(In["BlendState"].get<FString>() == "AlphaBlend" ? EBlendState::AlphaBlend : EBlendState::Opaque);
	else if (Instance->PSOType == EPSOType::StaticMesh_Translucent)
		Instance->SetBlendState(EBlendState::AlphaBlend);
	else Instance->SetBlendState(Instance->BlendState);
	return Instance;
}

// Material.cpp
void UMaterial::SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterial*& Material)
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	// ── 에셋 참조 ──
	// 저장과 불러오기가 같은 순서로 같은 함수를 부르게 TryEnterField를 쓴다 (바이너리 포맷에서도 맞도록)
	const bool bIsAsset = Ar.IsSaving() && Material && !Material->GetPath().empty();
	if (TOptional<FStructuredArchive::FSlot> AssetSlot = Record.TryEnterField("Asset", bIsAsset))
	{
		FString Path = Ar.IsSaving() ? Material->GetPath() : FString();
		AssetSlot.GetValue() << Path;
		if (Ar.IsLoading())
			Material = UAssetManager::GetAssetByPath<UMaterial>(Path);
		return;
	}

	// ── 인스턴스 ──
	const bool bIsInstance = Ar.IsSaving() && Material != nullptr;
	TOptional<FStructuredArchive::FSlot> BaseSlot = Record.TryEnterField("Base", bIsInstance);
	if (!BaseSlot.IsSet())
	{
		if (Ar.IsLoading())
			Material = nullptr;     // 빈 Record = 머티리얼 없음
		return;
	}

	FString BasePath;
	if (Ar.IsSaving())
	{
		const UMaterial* Base = Material->GetBaseAsset();
		BasePath = Base ? Base->GetPath() : FString("DefaultMaterial");
	}
	BaseSlot.GetValue() << BasePath;

	if (Ar.IsLoading())
	{
		UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>(BasePath);
		if (!Base)
		{
			HTR_LOG(Warning, "Load: base material '{}' not found, using DefaultMaterial", BasePath);
			Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		}
		Material = CreateInstance(Base);   // 먼저 만들고
	}

	if (Material)
		Material->Serialize(Record.EnterRecord("Instance"));   // 그다음 값을 채운다
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

void UMaterialInstance::SetParent(const UMaterial* InParent)
{
	Parent = InParent;
	bIsInstance = true;
	RefreshFromParent();
}

void UMaterialInstance::RefreshFromParent()
{
	if (!Parent)
	{
		return;
	}

	const bool bKeepBaseColor = bOverrideBaseColor;
	const FVector4 SavedBaseColor = BaseColorOverride;
	const bool bKeepTexture0 = bOverrideTexture0;
	UTexture2D* SavedTexture0 = Texture0Override;

	CopySettingsFrom(*Parent);

	if (bKeepBaseColor)
	{
		BaseColor = SavedBaseColor;
	}

	if (bKeepTexture0)
	{
		if (Textures.IsEmpty())
		{
			Textures.Add(nullptr);
		}
		Textures[0] = SavedTexture0;
	}

	bIsInstance = true;
}

void UMaterialInstance::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	FString ParentPath = Ar.IsSaving() && Parent ? Parent->GetPath() : "";
	Record << SA_VALUE("ParentAsset", ParentPath);
	Record << SA_VALUE("OverrideBaseColor", bOverrideBaseColor);
	Record << SA_VALUE("OverrideTexture0", bOverrideTexture0);

	if (auto Slot = Record.TryEnterField("BaseColorOverride", Ar.IsSaving() && bOverrideBaseColor))
	{
		Slot.GetValue() << BaseColorOverride;
	}

	FString TexturePath = Ar.IsSaving() && Texture0Override ? Texture0Override->GetPath() : "";

	if (auto Slot = Record.TryEnterField("Texture0Override", Ar.IsSaving() && bOverrideTexture0))
	{
		Slot.GetValue() << TexturePath;
	}

	if (Ar.IsLoading())
	{
		Parent = UAssetManager::GetAssetByPath<UMaterial>(ParentPath);
		Texture0Override = TexturePath.empty() ? nullptr : FindOrLoadTexture(TexturePath);
		RefreshFromParent();
	}
}