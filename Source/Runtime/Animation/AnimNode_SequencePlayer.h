#pragma once

#include "AnimNodeBase.h"
#include "AnimSequence.h"

#include <cmath>
#include "AnimInstance.h"
#include "AnimNotifyQueue.h"

struct FAnimNode_SequencePlayer : public FAnimNode_Base
{
	UAnimSequence* Sequence = nullptr;

	float CurrentTime = 0.0f;
	float PrevTime = 0.0f;
	float PlayRate = 1.0f;

	bool bLooping = false;

	// 상태 재진입(bResetOnEntry) 시 처음부터 다시 재생한다
	void Initialize(const FAnimationInitializeContext& Context) override
	{
		CurrentTime = PrevTime = 0.0f;
	}

	// 루프가 아닌 시퀀스가 끝까지 재생됐는지
	bool IsFinished() const
	{
		const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
		return !bLooping && (!Model || CurrentTime >= Model->GetPlayLength());
	}

	void Update(const FAnimationUpdateContext& Context) override
	{
		if (!Sequence)
		{
			return;
		}

		const UAnimDataModel* Model = Sequence->GetDataModel();
		const float Length = Model ? Model->GetPlayLength() : 0.0f;
		if (Length <= 0.0f)
		{
			CurrentTime = PrevTime = 0.0f;
			return;
		}

		PrevTime = CurrentTime;
		CurrentTime += Context.GetDeltaTime() * PlayRate;

		if (bLooping)
		{
			CurrentTime = std::fmod(CurrentTime, Length);
			if (CurrentTime < 0.0f)
			{
				CurrentTime += Length;
			}
		}
		else
		{
			CurrentTime = FMath::Clamp(CurrentTime, 0.0f, Length);
		}


		FAnimNotifyContext NotifyContext(bLooping, Sequence);
		Sequence->GetAnimNotifies(PrevTime, Context.GetDeltaTime() * PlayRate, NotifyContext);
		UAnimInstance* AnimInstance = Context.GetAnimInstance<UAnimInstance>();
		AnimInstance->AddNotifies(NotifyContext.ActiveNotifies, Context.GetFinalBlendWeight());

		// 루트 모션: Notify와 같은 구간(PrevTime부터 이번 프레임 이동량)에서 root delta를 뽑아 AnimInstance에 모은다
		// 시퀀스 설정(에셋)과 AnimInstance 모드(캐릭터)가 둘 다 허락할 때만 뽑는다 (Unreal과 같음)
		if (Sequence->GetEnableRootMotion() && AnimInstance->ShouldExtractRootMotion())
		{
			AnimInstance->AddRootMotion(Sequence->ExtractRootMotion(PrevTime, Context.GetDeltaTime() * PlayRate, bLooping));
		}
	}

	void Evaluate(FPoseContext& Output) override
	{
		if (!Sequence)
		{
			Output.ResetToRefPose();
			return;
		}

		// 추출하지 않는 모드면 root도 고정하지 않는다 (Update에서 뽑은 것과 짝을 맞춘다)
		const UAnimInstance* AnimInstance = Output.GetAnimInstance<UAnimInstance>();
		const bool bExtractRootMotion = !AnimInstance || AnimInstance->ShouldExtractRootMotion();
		Sequence->GetAnimationPose(CurrentTime, Output.Pose, Output.GetRefSkeleton(), bExtractRootMotion);
	}
};