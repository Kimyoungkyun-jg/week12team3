#pragma once

#include "Animation/AnimInstance.h"
#include "Asset/AnimGraphAsset.h"
#include <functional>
#include <vector>
#include "AnimGraphInstance.generated.h"

UCLASS()
class UAnimGraphInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	int32 GetActiveStateId() const;
	int32 GetTargetStateId() const;
	bool IsStateActiveOrPending(const FString& Name) const;
	bool IsStateSequenceFinished(const FString& Name) const;
	float GetSpeed() const { return Speed; }
	float GetDirection() const { return Direction; }
	float GetAimPitch() const { return AimPitch; }
	void SetIsAiming(bool bValue) { bIsAiming = bValue; }
	bool IsAiming() const { return bIsAiming; }
	void RequestJump() { bManualJumpRequested = true; }
	void RequestHit();
	void RequestAttack();
	void RequestDeath();
	bool SetPreviewGraph(const FAnimGraphAsset& InGraph);
	void SetPreviewFalling(bool bInFalling) { bPreviewMovementOverride = true; bPreviewFalling = bInFalling; }
	void SetPreviewAimPitch(float InPitch) { bPreviewAimPitchOverride = true; PreviewAimPitch = InPitch; AimPitch = InPitch; }
	float PlayMontage(const FString& MontagePath, float InPlayRate = 1.0f, const FString& StartSection = "");
	void StopMontage(const FString& SlotName = "", float BlendOutOverride = -1.0f);
	bool IsPlayingMontage(const FString& SlotName = "") const;
	float GetMontagePosition(const FString& SlotName = "UpperBody") const;
	float GetMontageWeight(const FString& SlotName = "UpperBody") const;
	const FString& GetLastTriggeredNotify() const { return LastTriggeredNotify; }
	void SetOnMontageNotify(std::function<void(const FString&)> InCallback) { OnMontageNotify = std::move(InCallback); }
	bool EvaluateSlotPose(const FString& SlotName, const USkeleton* Skeleton, TArray<FAnimBoneTransform>& InOutPose);
protected:
	void NativeInitializeAnimation() override;
	void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void BuildGraph();
	int32 AddSequenceState(FAnimNode_StateMachine& Machine, const FString& Name, const FString& AssetPath,
		bool bLoop, FAnimNode_SequencePlayer*& OutPlayer);
private:
	struct FInputBinding
	{
		FAnimNode_BlendSpacePlayer* Player = nullptr;
		EAnimGraphInput X = EAnimGraphInput::Constant;
		EAnimGraphInput Y = EAnimGraphInput::Constant;
		float ConstantX = 0.0f;
		float ConstantY = 0.0f;
	};
	struct FAimOffsetBinding
	{
		FAnimNode_AimOffset1D* Node = nullptr;
		EAnimGraphInput PitchInput = EAnimGraphInput::AimPitch;
		float ConstantPitch = 0.0f;
	};
	struct FAlphaBinding
	{
		float* Alpha = nullptr;
		EAnimGraphInput Input = EAnimGraphInput::Constant;
	};
	struct FActiveMontage
	{
		FString Path;
		FAnimMontageAsset Asset;
		UAnimSequence* Sequence = nullptr;
		float Position = 0.0f;
		float PlayRate = 1.0f;
		float ElapsedTime = 0.0f;
		float BlendWeight = 0.0f;
		int32 CurrentSectionIndex = -1;
		bool bStopping = false;
		float StopElapsed = 0.0f;
		float StopDuration = 0.15f;
		float StopStartWeight = 1.0f;
		bool bPlaying = false;
		bool bNotifyIncludeStart = true;
	};
	FAnimGraphAsset Graph;
	bool bUsePreviewGraph = false;
	std::vector<FInputBinding> Bindings;
	std::vector<FAimOffsetBinding> AimOffsetBindings;
	std::vector<FAlphaBinding> AlphaBindings;
	std::vector<FActiveMontage> ActiveMontages;
	std::function<void(const FString&)> OnMontageNotify;
	FString LastTriggeredNotify;
	std::vector<int32> StateIds;
	std::vector<std::pair<int32, FAnimNode_SequencePlayer*>> SequencePlayers;
	FAnimNode_StateMachine* GraphMachine = nullptr;
	int32 HitStateIndex = -1;
	int32 AttackStateIndex = -1;
	float Speed = 0.0f;
	float Direction = 0.0f;
	float AimPitch = 0.0f;
	float PreviewAimPitch = 0.0f;
	float AimBlendAlpha = 0.0f;
	uint32 LastJumpCounter = 0;
	bool bJumpRequested = false;
	bool bManualJumpRequested = false;
	bool bHitRequested = false;
	bool bAttackRequested = false;
	bool bDeathRequested = false;
	bool bFalling = false;
	bool bGrounded = false;
	bool bMovingOnGround = false;
	bool bPreviewMovementOverride = false;
	bool bPreviewFalling = false;
	bool bPreviewAimPitchOverride = false;
	bool bIsAiming = false;
	float InputValue(EAnimGraphInput Input, float Constant = 0.0f) const;
	void UpdateMontages(float DeltaSeconds);
};
