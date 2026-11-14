#pragma once

#include "MaterialInterface.h"
#include "MaterialInstance.generated.h"

class UTexture2D;

UCLASS()
class UMaterialInstance : public UMaterialInterface
{
	GENERATED_BODY()

public:
	UMaterialInstance();

	void SetParent(UMaterialInterface* InParent);
	UMaterialInterface* GetParent() const { return Parent; }

	UMaterial* GetBaseMaterial() const override;
	void GetRenderProxy(FMaterialRenderProxy& OutProxy) const override;

	EPSOType GetPSOType() const override;
	EBlendState GetBlendState() const override;
	ESamplerState GetSamplerState() const override;
	uint16 GetSortID() const override { return SortID; }

	void SetBaseColorOverride(const FVector4& Value);
	void SetTexture0Override(UTexture2D* Texture);

	bool HasBaseColorOverride() const { return bOverrideBaseColor; }
	const FVector4& GetBaseColorOverride() const { return BaseColorOverride; }
	bool HasTexture0Override() const { return bOverrideTexture0; }
	UTexture2D* GetTexture0Override() const { return Texture0Override; }

	virtual bool IsDynamicInstance() const = 0;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	UMaterialInterface* Parent = nullptr;

	bool bOverrideBaseColor = false;
	FVector4 BaseColorOverride = FVector4(1, 1, 1, 1);

	bool bOverrideTexture0 = false;
	UTexture2D* Texture0Override = nullptr;

	const uint16 SortID;
	inline static uint16 NextSortID = 0;
};

UCLASS()
class UMaterialInstanceConstant : public UMaterialInstance
{
	GENERATED_BODY()

public:
	bool IsDynamicInstance() const override { return false; }
};

UCLASS()
class UMaterialInstanceDynamic : public UMaterialInstance
{
	GENERATED_BODY()

public:
	bool IsDynamicInstance() const override { return true; }
};