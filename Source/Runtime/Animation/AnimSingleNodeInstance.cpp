#include "EnginePCH.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cmath>

bool UAnimSingleNodeInstance::SetAnimationAsset(UAnimSequence* InSequence)
{
	if (!Player) InitializeAnimation();
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (InSequence && (!InSequence->Skeleton || !InSequence->GetDataModel() ||
		(Mesh && Mesh->Skeleton != InSequence->Skeleton))) return false;
	Player->Sequence = InSequence;
	Player->Reset();
	Player->bPlaying = false;
	return true;
}

void UAnimSingleNodeInstance::Play(bool bInLooping)
{
	if (!Player) return;
	Player->bLoop = bInLooping;
	Player->bPlaying = Player->Sequence != nullptr;
}

void UAnimSingleNodeInstance::SetPosition(float InSeconds)
{
	if (!std::isfinite(InSeconds)) return;
	const UAnimDataModel* Model = GetAnimationAsset() ? GetAnimationAsset()->GetDataModel() : nullptr;
	if (Player)
	{
		Player->CurrentTime = Model ? std::clamp(InSeconds, 0.0f, Model->PlayLength) : 0.0f;
		Player->bHasPendingRootMotion = false;
	}
}

void UAnimSingleNodeInstance::SetPlayRate(float InPlayRate)
{
	if (Player && std::isfinite(InPlayRate) && InPlayRate >= 0.0f) Player->PlayRate = InPlayRate;
}

void UAnimSingleNodeInstance::NativeInitializeAnimation()
{
	Player = CreateNode<FAnimNode_SequencePlayer>();
	RootNode = Player;
}
