#pragma once

#include "AnimBlueprintData.h"
#include "Containers/Map.h"

class UAnimGraphInstance;
class USkeletalMeshComponent;

namespace AnimBlueprintCompiler
{
    using FNodeMap = TMap<int32, TArray<int32>>;

    bool Build(const FAnimBlueprintData& Data, USkeletalMeshComponent& Component, TUniquePtr<UAnimGraphInstance>& OutInstance, FNodeMap& OutNodeIndices, FString& OutError);
}
