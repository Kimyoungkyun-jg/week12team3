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
	for (UMaterialInterface* Current = InParent; Current;)
	{
		if (Current == this)
		{
			return;
		}

		UMaterialInstance* Instance = Cast<UMaterialInstance>(Current);
		Current = Instance ? Instance->GetParent() : nullptr;
	}

	Parent = InParent;
	ParameterOverrides.Reset();
}

const FMaterialParameterOverride* UMaterialInstance::GetParameterOverride(const FName& ParameterName) const
{
	for (const FMaterialParameterOverride& Override : ParameterOverrides)
	{
		if (Override.ParameterName == ParameterName)
		{
			return &Override;
		}
	}

	return nullptr;
}

bool UMaterialInstance::HasParameterOverride(const FName& ParameterName) const
{
	return GetParameterOverride(ParameterName) != nullptr;
}

void UMaterialInstance::SetScalarParameterValue(const FName& ParameterName, float Value)
{
	for (FMaterialParameterOverride& Override : ParameterOverrides)
	{
		if (Override.ParameterName == ParameterName)
		{
			Override.Type = EMaterialParameterType::Scalar;
			Override.ScalarValue = Value;
			return;
		}
	}

	FMaterialParameterOverride Override{};
	Override.ParameterName = ParameterName;
	Override.Type = EMaterialParameterType::Scalar;
	Override.ScalarValue = Value;
	ParameterOverrides.Add(Override);
}

void UMaterialInstance::SetVectorParameterValue(const FName& ParameterName, const FVector4& Value)
{
	for (FMaterialParameterOverride& Override : ParameterOverrides)
	{
		if (Override.ParameterName == ParameterName)
		{
			Override.Type = EMaterialParameterType::Vector;
			Override.VectorValue = Value;
			return;
		}
	}

	FMaterialParameterOverride Override{};
	Override.ParameterName = ParameterName;
	Override.Type = EMaterialParameterType::Vector;
	Override.VectorValue = Value;
	ParameterOverrides.Add(Override);
}

void UMaterialInstance::SetTextureParameterValue(const FName& ParameterName, UTexture2D* Texture)
{
	for (FMaterialParameterOverride& Override : ParameterOverrides)
	{
		if (Override.ParameterName == ParameterName)
		{
			Override.Type = EMaterialParameterType::Texture;
			Override.TextureValue = Texture;
			return;
		}
	}

	FMaterialParameterOverride Override{};
	Override.ParameterName = ParameterName;
	Override.Type = EMaterialParameterType::Texture;
	Override.TextureValue = Texture;
	ParameterOverrides.Add(Override);
}

void UMaterialInstance::ClearParameterOverride(const FName& ParameterName)
{
	for (int32 Index = ParameterOverrides.Num() - 1; Index >= 0; --Index)
	{
		if (ParameterOverrides[Index].ParameterName == ParameterName)
		{
			ParameterOverrides.RemoveAt(Index);
			return;
		}
	}
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

namespace
{
void ApplyMaterialParameter(FMaterialRenderProxy& Proxy, const FMaterialParameterOverride& Parameter)
{
	const FName& Name = Parameter.ParameterName;

	if (Parameter.Type == EMaterialParameterType::Scalar)
	{
		const float Value = Parameter.ScalarValue;

		if (Name == FName("Shininess"))
		{
			Proxy.Shininess = Value;
		}
		else if (Name == FName("SpecularFactor"))
		{
			Proxy.SpecularFactor = Value;
		}
		else if (Name == FName("EmissiveFactor"))
		{
			Proxy.EmissiveFactor = Value;
		}
		else if (Name == FName("FlipNormalY"))
		{
			Proxy.bFlipNormalY = Value >= 0.5f;
		}
		else if (Name == FName("WorldUVLocalSpace"))
		{
			Proxy.bWorldUVLocalSpace = Value >= 0.5f;
		}
	}
	else if (Parameter.Type == EMaterialParameterType::Vector)
	{
		const FVector4& Value = Parameter.VectorValue;

		if (Name == FName("BaseColor"))
		{
			Proxy.BaseColor = Value;
		}
		else if (Name == FName("SpecularColor"))
		{
			Proxy.SpecularColor = FVector(Value.X, Value.Y, Value.Z);
		}
		else if (Name == FName("EmissiveColor"))
		{
			Proxy.EmissiveColor = FVector(Value.X, Value.Y, Value.Z);
		}
		else if (Name == FName("UVScrollSpeed"))
		{
			Proxy.UVScrollSpeed = FVector2(Value.X, Value.Y);
		}
		else if (Name == FName("UVTiling"))
		{
			Proxy.UVTiling = FVector2(Value.X, Value.Y);
		}
		else if (Name == FName("WorldUVTileSize"))
		{
			Proxy.WorldUVTileSize = FVector2(Value.X, Value.Y);
		}
	}
	else if (Parameter.Type == EMaterialParameterType::Texture)
	{
		int32 TextureIndex = -1;

		if (Name == FName("Texture0"))
		{
			TextureIndex = 0;
		}
		else if (Name == FName("Texture1"))
		{
			TextureIndex = 1;
		}

		if (TextureIndex >= 0)
		{
			while (Proxy.Textures.Num() <= TextureIndex)
			{
				Proxy.Textures.Add(nullptr);
			}

			Proxy.Textures[TextureIndex] = Parameter.TextureValue;
		}
	}
}
}

void UMaterialInstance::GetRenderProxy(FMaterialRenderProxy& OutProxy) const
{
	if (!Parent)
	{
		OutProxy = FMaterialRenderProxy{};
		return;
	}

	Parent->GetRenderProxy(OutProxy);

	for (const FMaterialParameterOverride& Parameter : ParameterOverrides)
	{
		bool bReplaced = false;

		for (FMaterialParameterOverride& Existing : OutProxy.ParameterOverrides)
		{
			if (Existing.ParameterName == Parameter.ParameterName)
			{
				Existing = Parameter;
				bReplaced = true;
				break;
			}
		}

		if (!bReplaced)
		{
			OutProxy.ParameterOverrides.Add(Parameter);
		}

		ApplyMaterialParameter(OutProxy, Parameter);
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

	bool bLegacyOverrideBaseColor = false;
	bool bLegacyOverrideTexture0 = false;
	FVector4 LegacyBaseColor = FVector4(1, 1, 1, 1);
	FString LegacyTexturePath;

	if (Ar.IsLoading())
	{
		if (auto Slot = Record.TryEnterField("OverrideBaseColor", false))
		{
			Slot.GetValue() << bLegacyOverrideBaseColor;
		}

		if (auto Slot = Record.TryEnterField("OverrideTexture0", false))
		{
			Slot.GetValue() << bLegacyOverrideTexture0;
		}

		if (auto Slot = Record.TryEnterField("BaseColorOverride", false))
		{
			Slot.GetValue() << LegacyBaseColor;
		}

		if (auto Slot = Record.TryEnterField("Texture0Override", false))
		{
			Slot.GetValue() << LegacyTexturePath;
		}
	}

	int32 Num = ParameterOverrides.Num();
	auto ParameterSlot = Record.TryEnterField("ParameterOverrides", Ar.IsSaving() && Num > 0);

	if (ParameterSlot.IsSet())
	{
		FStructuredArchive::FArray Array = ParameterSlot.GetValue().EnterArray(Num);

		if (Ar.IsLoading())
		{
			ParameterOverrides.Reset();
		}

		for (int32 Index = 0; Index < Num; ++Index)
		{
			FMaterialParameterOverride Parameter{};

			if (Ar.IsSaving())
			{
				Parameter = ParameterOverrides[Index];
			}

			uint8 Type = static_cast<uint8>(Parameter.Type);
			FString TexturePath = Ar.IsSaving() && Parameter.TextureValue ? Parameter.TextureValue->GetPath() : "";

			FStructuredArchive::FRecord Element = Array.EnterElement().EnterRecord();
			Element << SA_VALUE("ParameterName", Parameter.ParameterName);
			Element << SA_VALUE("Type", Type);
			Element << SA_VALUE("ScalarValue", Parameter.ScalarValue);
			Element << SA_VALUE("VectorValue", Parameter.VectorValue);
			Element << SA_VALUE("TexturePath", TexturePath);

			if (Ar.IsLoading())
			{
				Parameter.Type = static_cast<EMaterialParameterType>(Type);
				Parameter.TextureValue = TexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(TexturePath);
				ParameterOverrides.Add(Parameter);
			}
		}
	}

	if (Ar.IsLoading())
	{
		if (bLegacyOverrideBaseColor && !HasParameterOverride(FName("BaseColor")))
		{
			SetVectorParameterValue(FName("BaseColor"), LegacyBaseColor);
		}

		if (bLegacyOverrideTexture0 && !HasParameterOverride(FName("Texture0")))
		{
			UTexture2D* Texture = LegacyTexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(LegacyTexturePath);
			SetTextureParameterValue(FName("Texture0"), Texture);
		}

		Parent = UAssetManager::GetAssetByPath<UMaterialInterface>(ParentPath);

		if (!Parent)
		{
			Parent = UAssetManager::LoadMaterialAsset(ParentPath);
		}
	}
}