#include "EnginePCH.h"
#include "AnimSingleNodeInstance.h"
#include "UObject/Casts.h"
#include <cmath>

void UAnimSingleNodeInstance::SetAnimationAsset(UAnimationAsset* InAsset, bool bLoop, float InPlayRate)
{
	if (InAsset && !Cast<UAnimSequence>(InAsset) && !Cast<UBlendSpace1D>(InAsset))
	{
		return;
	}

	CurrentAsset = InAsset;
	bLooping = bLoop;
	PlayRate = InPlayRate;
	SingleNode.bPlaying = false;

	InitializeAnimation();
}

void UAnimSingleNodeInstance::NativeInitializeAnimation()
{
	SingleNode.SetAnimationAsset(CurrentAsset, bLooping, PlayRate);
	SingleNode.SetLooping(bLooping);
	RootNode = &SingleNode;
}

void UAnimSingleNodeInstance::SetBlendSpaceAsset(UBlendSpace1D* InBlendSpace, float InPlayRate)
{
    SetAnimationAsset(InBlendSpace, true, InPlayRate);
}

void UAnimSingleNodeInstance::SetBlendSpaceInput(float Value)
{
	if (std::isfinite(Value))
	{
		SingleNode.SetBlendSpaceInput(Value);
	}
}

void UAnimSingleNodeInstance::SetPlaying(bool bInPlaying)
{
	SingleNode.bPlaying = bInPlaying && CurrentAsset != nullptr;
}

bool UAnimSingleNodeInstance::IsPlaying() const
{
	return SingleNode.bPlaying;
}

void UAnimSingleNodeInstance::SetLooping(bool bInLooping)
{
	bLooping = bInLooping;
	SingleNode.SetLooping(bLooping);
}

void UAnimSingleNodeInstance::SetPlayRate(float InPlayRate)
{
	if (std::isfinite(InPlayRate))
	{
		PlayRate = InPlayRate;
		SingleNode.SetPlayRate(PlayRate);
	}
}

float UAnimSingleNodeInstance::GetPlayRate() const
{
	return PlayRate;
}

void UAnimSingleNodeInstance::SetPosition(float InPosition)
{
	if (std::isfinite(InPosition))
	{
		SingleNode.SetPosition(InPosition);
	}
}

float UAnimSingleNodeInstance::GetCurrentTime() const
{
	return SingleNode.GetCurrentTime();
}

float UAnimSingleNodeInstance::GetLength() const
{
	return SingleNode.GetLength();
}

UAnimationAsset* UAnimSingleNodeInstance::GetCurrentAsset() const
{
	return CurrentAsset;
}
