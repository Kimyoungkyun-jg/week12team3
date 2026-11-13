#pragma once

#include "Animation/AnimInstance.h"
#include "Asset/AnimSequence.h"
#include "AnimSingleNodeInstance.generated.h"

UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	bool SetAnimationAsset(UAnimSequence* InSequence);
	UAnimSequence* GetAnimationAsset() const { return Player ? Player->Sequence : nullptr; }
	void Play(bool bInLooping = true);
	void Stop() { if (Player) Player->bPlaying = false; }
	void SetPosition(float InSeconds);
	float GetPosition() const { return Player ? Player->CurrentTime : 0.0f; }
	void SetPlayRate(float InPlayRate);
	float GetPlayRate() const { return Player ? Player->PlayRate : 1.0f; }
	bool IsPlaying() const { return Player && Player->bPlaying; }
	bool IsLooping() const { return Player && Player->bLoop; }

protected:
	void NativeInitializeAnimation() override;

private:
	FAnimNode_SequencePlayer* Player = nullptr;
};
