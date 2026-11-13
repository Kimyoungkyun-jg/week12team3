#pragma once

#include "Asset/Asset.h"

#include "RenderAsset.generated.h"

UCLASS()
class URenderAsset : public UAsset
{
	GENERATED_BODY()
public:
	URenderAsset() = default;
	virtual ~URenderAsset() = default;
};
