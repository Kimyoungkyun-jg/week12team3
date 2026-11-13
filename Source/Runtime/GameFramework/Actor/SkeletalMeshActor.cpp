#include "EnginePCH.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "Asset/AssetManager.h"

ASkeletalMeshActor::ASkeletalMeshActor()
{
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
    SetRootComponent(SkeletalMeshComponent);
}
