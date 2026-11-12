#pragma once

#include "AnimInstance.h"
#include "AnimNode_SingleNode.h"
#include "AnimationAsset.h"
#include "AnimSingleNodeInstance.generated.h"

UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetAnimationAsset(UAnimationAsset* InAsset, bool bLoop = true, float InPlayRate = 1.0f);
	void SetBlendSpaceAsset(UBlendSpace1D* InBlendSpace, float InPlayRate = 1.0f);
	void SetBlendSpaceInput(float Value);

	void SetPlaying(bool bInPlaying);
	bool IsPlaying() const;

	void SetLooping(bool bInLooping);
	bool IsLooping() const { return bLooping; }
	void SetPlayRate(float InPlayRate);
	float GetPlayRate() const;

	void SetPosition(float InPosition);
	float GetCurrentTime() const;
	float GetLength() const;

	UAnimationAsset* GetCurrentAsset() const;

protected:
	void NativeInitializeAnimation() override;

private:
	FAnimNode_SingleNode SingleNode;

	UPROPERTY()
	UAnimationAsset* CurrentAsset = nullptr;

	UPROPERTY()
	bool bLooping = true;

	UPROPERTY()
	float PlayRate = 1.0f;
};
