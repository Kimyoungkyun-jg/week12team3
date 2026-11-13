#pragma once

#include "Asset/AnimationAsset.h"
#include "Asset/AnimDataModel.h"
#include "AnimSequenceBase.generated.h"

UCLASS()
class UAnimSequenceBase : public UAnimationAsset
{
	GENERATED_BODY()
public:
	UPROPERTY()
	UAnimDataModel* DataModel = nullptr;
	const UAnimDataModel* GetDataModel() const { return DataModel; }
	UAnimDataModel* GetDataModel() { return DataModel; }
};
