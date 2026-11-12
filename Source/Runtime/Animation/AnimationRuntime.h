#pragma once

#include "BonePose.h"
#include "Math/EngineMath.h"
#include "AnimTypes.h"

struct FAnimationRuntime
{
	static inline void BlendTwoPosesTogether(
		const FCompactPose& PoseA,
		const FCompactPose& PoseB,
		const FBlendedCurve& CurveA,
		const FBlendedCurve& CurveB,
		float WeightA,
		FCompactPose& OutPose,
		FBlendedCurve& OutCurve
	)
	{
		if (PoseA.GetNumBones() != PoseB.GetNumBones())
		{
			return;
		}

		WeightA = FMath::Clamp(WeightA, 0.0f, 1.0f);
		const float Alpha = 1.0f - WeightA;

		OutPose.SetNumBones(PoseA.GetNumBones());

		for (int32 BoneIndex = 0; BoneIndex < PoseA.GetNumBones(); ++BoneIndex)
		{
			const FTransform& A = PoseA[BoneIndex];
			const FTransform& B = PoseB[BoneIndex];
			FTransform& Result = OutPose[BoneIndex];

			Result.Location = FMath::Lerp(A.Location, B.Location, Alpha);
			Result.Rotation = FMath::Slerp(A.Rotation, B.Rotation, Alpha);
			Result.Scale = FMath::Lerp(A.Scale, B.Scale, Alpha);
		}
		OutCurve = FBlendedCurve();
	}

	static inline ETypeAdvanceAnim AdvanceTime(const bool bAllowLooping, const float MoveDelta, float& InOutTime, const float EndTime)
	{
		float NewTime = InOutTime + MoveDelta;

		if (NewTime < 0.f || NewTime > EndTime)
		{
			if (bAllowLooping)
			{
				if (EndTime != 0.f)
				{
					NewTime = FMath::Fmod(NewTime, EndTime);
					// Fmod doesn't give result that falls into (0, EndTime), but one that falls into (-EndTime, EndTime). Negative values need to be handled in custom way
					if (NewTime < 0.f)
					{
						NewTime += EndTime;
					}
				}
				else
				{
					// end time is 0.f
					NewTime = 0.f;
				}

				// it has been looped
				InOutTime = NewTime;
				return ETypeAdvanceAnim::ETAA_Looped;
			}
			else
			{
				// If not, snap time to end of sequence and stop playing.
				InOutTime = FMath::Clamp(NewTime, 0.f, EndTime);
				return ETypeAdvanceAnim::ETAA_Finished;
			}
		}

		InOutTime = NewTime;
		return ETypeAdvanceAnim::ETAA_Default;
	}
};