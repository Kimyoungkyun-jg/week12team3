#pragma once

#include "AnimNodeBase.h"
#include "BlendSpace1D.h"
#include "AnimationRuntime.h"
#include "AnimInstance.h"
#include "AnimNotifyQueue.h"

#include <cmath>

struct FAnimNode_BlendSpacePlayer : public FAnimNode_Base
{
	UBlendSpace1D* BlendSpace = nullptr;

	float X = 0.0f;
	float PlayRate = 1.0f;
	float Phase = 0.0f;
	bool bLooping = true;

	void Update(const FAnimationUpdateContext& Context) override
	{
		if (!BlendSpace)
		{
			return;
		}

		FBlendSample SampleA;
		FBlendSample SampleB;
		float Alpha = 0.0f;

		if (!BlendSpace->GetSamples(X, SampleA, SampleB, Alpha)
			|| !SampleA.Animation || !SampleB.Animation
			|| !SampleA.Animation->GetDataModel() || !SampleB.Animation->GetDataModel())
		{
			return;
		}

		const float LengthA = SampleA.Animation->GetDataModel()->GetPlayLength();
		const float LengthB = SampleB.Animation->GetDataModel()->GetPlayLength();
		const float EffectiveLength = FMath::Lerp(LengthA, LengthB, Alpha);

		if (EffectiveLength <= 0.0f)
		{
			Phase = 0.0f;
			return;
		}

		const float PrevPhase = Phase;
		const float DeltaPhase = Context.GetDeltaTime() * PlayRate / EffectiveLength;

		Phase += DeltaPhase;
		if (!bLooping)
		{
			Phase = FMath::Clamp(Phase, 0.0f, 1.0f);
			return;
		}
		Phase = std::fmod(Phase, 1.0f);

		if (Phase < 0.0f)
		{
			Phase += 1.0f;
		}

		// 샘플들은 Phase로 동기화되므로, 각 샘플 길이로 환산한 (이전 시간, 이동량) 구간의 Notify를 모은다.
		// UE BlendSpace 기본값(NotifyTriggerMode::AllAnimations)처럼 모든 샘플에서 수집하고,
		// 샘플 가중치를 넘겨 FAnimNotifyEvent::TriggerWeightThreshold로 걸러지게 한다.
		UAnimInstance* AnimInstance = Context.GetAnimInstance<UAnimInstance>();
		if (!AnimInstance)
		{
			return;
		}

		const float FinalWeight = Context.GetFinalBlendWeight();
		auto CollectNotifies = [&](const UAnimSequence* Sequence, float Length, float SampleWeight)
		{
			if (SampleWeight <= 0.0f)
			{
				return;
			}

			FAnimNotifyContext NotifyContext(true, Sequence);
			Sequence->GetAnimNotifies(PrevPhase * Length, DeltaPhase * Length, NotifyContext);
			AnimInstance->AddNotifies(NotifyContext.ActiveNotifies, FinalWeight * SampleWeight);
		};

		if (SampleA.Animation == SampleB.Animation)
		{
			// 범위 밖으로 클램프되어 같은 샘플이 두 번 들어온 경우 한 번만 수집한다
			CollectNotifies(SampleA.Animation, LengthA, 1.0f);
		}
		else
		{
			CollectNotifies(SampleA.Animation, LengthA, 1.0f - Alpha);
			CollectNotifies(SampleB.Animation, LengthB, Alpha);
		}
	}

	void Evaluate(FPoseContext& Output) override
	{
        if (!BlendSpace)
        {
            Output.ResetToRefPose();
            return;
        }

        FBlendSample SampleA;
        FBlendSample SampleB;
        float Alpha = 0.0f;

        if (!BlendSpace->GetSamples(X, SampleA, SampleB, Alpha) 
            || !SampleA.Animation || !SampleB.Animation
            || !SampleA.Animation->GetDataModel() || !SampleB.Animation->GetDataModel())
        {
            Output.ResetToRefPose();
            return;
        }

        FPoseContext PoseA(Output);
        FPoseContext PoseB(Output);

        const float TimeA = Phase * SampleA.Animation->GetDataModel()->GetPlayLength();
        const float TimeB = Phase * SampleB.Animation->GetDataModel()->GetPlayLength();

        SampleA.Animation->GetAnimationPose(
            TimeA,
            PoseA.Pose,
            Output.GetRefSkeleton());

        SampleB.Animation->GetAnimationPose(
            TimeB,
            PoseB.Pose,
            Output.GetRefSkeleton());

        FAnimationRuntime::BlendTwoPosesTogether(
            PoseA.Pose, PoseB.Pose, PoseA.Curve, PoseB.Curve,
            1.0f - Alpha, Output.Pose, Output.Curve);
    }
};
