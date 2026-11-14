#include "EnginePCH.h"
#include "MaterialInstance.h"
#include "Material.h"
#include "MaterialRenderProxy.h"
#include "Asset/AssetManager.h"
#include "Serialization/TypeSerializer.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"

UMaterialInstance::UMaterialInstance()
	: SortID(NextSortID++)
{
}

void UMaterialInstance::SetParent(UMaterialInterface* InParent)
{
	Parent = InParent;
	bOverrideBaseColor = false;
	bOverrideTexture0 = false;
	Texture0Override = nullptr;
}

UMaterial* UMaterialInstance::GetBaseMaterial() const
{
	return Parent ? Parent->GetBaseMaterial() : nullptr;
}

EPSOType UMaterialInstance::GetPSOType() const
{
	return Parent ? Parent->GetPSOType() : EPSOType::StaticMesh_Opaque;
}

EBlendState UMaterialInstance::GetBlendState() const
{
	return Parent ? Parent->GetBlendState() : EBlendState::Opaque;
}

ESamplerState UMaterialInstance::GetSamplerState() const
{
	return Parent ? Parent->GetSamplerState() : ESamplerState::LinearClamp;
}

void UMaterialInstance::GetRenderProxy(FMaterialRenderProxy& OutProxy) const
{
	if (!Parent)
	{
		OutProxy = FMaterialRenderProxy{};
		return;
	}

	Parent->GetRenderProxy(OutProxy);

	if (bOverrideBaseColor)
	{
		OutProxy.BaseColor = BaseColorOverride;
	}

	if (bOverrideTexture0)
	{
		if (OutProxy.Textures.IsEmpty())
		{
			OutProxy.Textures.Add(Texture0Override);
		}
		else
		{
			OutProxy.Textures[0] = Texture0Override;
		}
	}
}

void UMaterialInstance::SetBaseColorOverride(const FVector4& Value)
{
	bOverrideBaseColor = true;
	BaseColorOverride = Value;
}

void UMaterialInstance::SetTexture0Override(UTexture2D* Texture)
{
	bOverrideTexture0 = true;
	Texture0Override = Texture;
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
		Parent = UAssetManager::LoadMaterialAsset(ParentPath);
		Texture0Override = TexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(TexturePath);
	}
}