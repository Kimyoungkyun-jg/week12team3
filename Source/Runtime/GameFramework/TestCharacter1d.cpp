#include "EnginePCH.h"
#include "GameFramework/TestCharacter1d.h"
#include "Components/SkeletalMeshComponent.h"

ATestCharacter1d::ATestCharacter1d()
{
	if (!GetSkeletalMeshComponent()->SetAnimGraphPath(GetAnimationGraphPath()))
		HTR_LOG(Error, "Failed to assign TestCharacter1d.animgraph to ATestCharacter1d");
}
