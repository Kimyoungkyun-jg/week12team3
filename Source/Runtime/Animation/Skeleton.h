#pragma once
#include "Asset/RenderAsset.h"
#include "Animation/ReferenceSkeleton.h"
#include "Skeleton.generated.h"

UCLASS()
class USkeleton : public URenderAsset
{
	GENERATED_BODY()
public:
	FReferenceSkeleton RefSkeleton;
};
