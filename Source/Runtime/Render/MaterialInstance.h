#pragma once
#include "MaterialInterface.h"
#include "Core/NameTypes.h"
#include "MaterialInstance.generated.h"

class UTexture2D;
enum class EMaterialParameterType : uint8
{
	Scalar,
	Vector,
	Texture
};

struct FMaterialParameterOverride
{
	FName ParameterName;
	EMaterialParameterType Type = EMaterialParameterType::Scalar;
	float ScalarValue = 0.0f;
	FVector4 VectorValue = FVector4(0, 0, 0, 0);
	UTexture2D* TextureValue = nullptr;
};
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

	void SetScalarParameterValue(const FName& ParameterName, float Value);
	void SetVectorParameterValue(const FName& ParameterName, const FVector4& Value);
	void SetTextureParameterValue(const FName& ParameterName, UTexture2D* Texture);
	void ClearParameterOverride(const FName& ParameterName);
	bool HasParameterOverride(const FName& ParameterName) const;
	const FMaterialParameterOverride* GetParameterOverride(const FName& ParameterName) const;
	const TArray<FMaterialParameterOverride>& GetParameterOverrides() const { return ParameterOverrides; }
	void SetBaseColorOverride(const FVector4& Value) { SetVectorParameterValue(FName("BaseColor"), Value); }
	void SetTexture0Override(UTexture2D* Texture) { SetTextureParameterValue(FName("Texture0"), Texture); }
	bool HasBaseColorOverride() const { return HasParameterOverride(FName("BaseColor")); }
	const FVector4& GetBaseColorOverride() const
	{
		const FMaterialParameterOverride* Override = GetParameterOverride(FName("BaseColor"));
		return Override ? Override->VectorValue : DefaultBaseColor;
	}
	bool HasTexture0Override() const
	{
		return HasParameterOverride(FName("Texture0"));
	}
	UTexture2D* GetTexture0Override() const
	{
		const FMaterialParameterOverride* Override = GetParameterOverride(FName("Texture0"));
		return Override ? Override->TextureValue : nullptr;
	}

	virtual bool IsDynamicInstance() const = 0;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	UMaterialInterface* Parent = nullptr;

	FVector4 DefaultBaseColor = FVector4(1, 1, 1, 1);

	const uint16 SortID;
	inline static uint16 NextSortID = 0;

	TArray<FMaterialParameterOverride> ParameterOverrides;
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