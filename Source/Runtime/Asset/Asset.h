#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Core/EngineString.h"
#include <utility>
#include "Asset.generated.h"

UCLASS()
class UAsset : public UObject
{
	GENERATED_BODY()
public:
	UAsset() = default;
	virtual ~UAsset() = default;

	void SetPath(FString Path) { AssetPath = std::move(Path); }
	const FString& GetPath() const { return AssetPath; }
	bool IsRetired() const { return bRetired; }
	void SetRetired(bool Value) { bRetired = Value; }

private:
	FString AssetPath;
	bool bRetired = false;
};
