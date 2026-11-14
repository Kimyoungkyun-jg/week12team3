#pragma once

#include "Asset/RenderAsset.h"
#include "MaterialInterface.generated.h"

class UMaterial;
struct FMaterialRenderProxy;

enum class EPSOType : uint8;
enum class EBlendState : uint8;
enum class ESamplerState : uint8;

UCLASS()
class UMaterialInterface : public URenderAsset
{
	GENERATED_BODY()

public:
	virtual UMaterial* GetBaseMaterial() const = 0;
	virtual void GetRenderProxy(FMaterialRenderProxy& OutProxy) const = 0;

	virtual EPSOType GetPSOType() const = 0;
	virtual EBlendState GetBlendState() const = 0;
	virtual ESamplerState GetSamplerState() const = 0;
	virtual uint16 GetSortID() const = 0;
};