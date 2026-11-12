#pragma once

#include "AnimNodeBase.h"
#include "AnimationRuntime.h"

struct FAnimNode_TwoWayBlend : public FAnimNode_Base
{
	FPoseLink A;
	FPoseLink B;

	float Alpha = 0.0f;

    void Initialize(const FAnimationInitializeContext& Context) override
    {
        A.Initialize(Context);
        B.Initialize(Context);
    }

	virtual void Update(const FAnimationUpdateContext& Context) override
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		A.Update(Context.FractionalWeight(1.f - ClampedAlpha));
		B.Update(Context.FractionalWeight(ClampedAlpha));
	}

	virtual void Evaluate(FPoseContext& Output) override
	{
		FPoseContext PoseA(Output);
		FPoseContext PoseB(Output);

		A.Evaluate(PoseA);
		B.Evaluate(PoseB);

		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

		FAnimationRuntime::BlendTwoPosesTogether(
			PoseA.Pose, PoseB.Pose, PoseA.Curve, PoseB.Curve,
			1.0f - ClampedAlpha,
			Output.Pose, Output.Curve);
	}
};
