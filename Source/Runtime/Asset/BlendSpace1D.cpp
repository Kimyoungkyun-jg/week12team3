#include "EnginePCH.h"
#include "Asset/BlendSpace1D.h"
#include "Asset/AnimSequence.h"
#include "Animation/BoneTransform.h"

#include <algorithm>
#include <cmath>

bool UBlendSpace1D::AddSample(float X, UAnimSequence* Sequence)
{
	if (!std::isfinite(X) || !Sequence || !Sequence->Skeleton || !Sequence->GetDataModel() ||
		!std::isfinite(Sequence->GetDataModel()->PlayLength) ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f ||
		Sequence->GetDataModel()->PlayLength <= 0.0f || (Skeleton && Sequence->Skeleton != Skeleton)) return false;
	for (const FSample& Sample : Samples)
		if (Sample.X == X) return false;
	if (!Skeleton) Skeleton = Sequence->Skeleton;
	Samples.Add({ X, Sequence });
	std::sort(Samples.begin(), Samples.end(),
		[](const FSample& A, const FSample& B) { return A.X < B.X; });
	return true;
}

bool UBlendSpace1D::ReplaceSamples(const TArray<FSample>& InSamples)
{
	UBlendSpace1D Replacement;
	Replacement.Skeleton = Skeleton;
	for (const FSample& Sample : InSamples)
		if (!Replacement.AddSample(Sample.X, Sample.Sequence)) return false;
	if (Replacement.GetSamples().IsEmpty()) return false;
	Samples = std::move(Replacement.Samples);
	return true;
}

bool UBlendSpace1D::GetBlendSamples(float X, const FSample*& OutA, const FSample*& OutB,
	float& OutAlpha) const
{
	OutA = OutB = nullptr;
	OutAlpha = 0.0f;
	if (Samples.IsEmpty() || !std::isfinite(X)) return false;
	if (X <= Samples[0].X) OutA = OutB = &Samples[0];
	else if (X >= Samples[Samples.Num() - 1].X) OutA = OutB = &Samples[Samples.Num() - 1];
	else
	{
		for (int32 Index = 1; Index < Samples.Num(); ++Index)
			if (X <= Samples[Index].X)
			{
				OutA = &Samples[Index - 1];
				OutB = &Samples[Index];
				OutAlpha = (X - OutA->X) / (OutB->X - OutA->X);
				break;
			}
	}
	return OutA && OutB;
}

float UBlendSpace1D::GetEffectiveLength(float X, float Y) const
{
	const FSample* A;
	const FSample* B;
	float Alpha;
	if (!GetBlendSamples(X, A, B, Alpha)) return 0.0f;
	return FMath::Lerp(A->Sequence->GetDataModel()->PlayLength / A->Sequence->RateScale,
		B->Sequence->GetDataModel()->PlayLength / B->Sequence->RateScale, Alpha);
}

bool UBlendSpace1D::Evaluate(float X, float Y, float NormalizedTime,
	TArray<FAnimBoneTransform>& OutPose) const
{
	const FSample* A;
	const FSample* B;
	float Alpha;
	if (!Skeleton || !std::isfinite(NormalizedTime) ||
		!GetBlendSamples(X, A, B, Alpha)) return false;
	const float TimeA = std::clamp(NormalizedTime, 0.0f, 1.0f) * A->Sequence->GetDataModel()->PlayLength;
	if (A == B || Alpha <= 0.0f)
		return A->Sequence->EvaluateLocalPoseTransforms(TimeA, OutPose);
	const float TimeB = std::clamp(NormalizedTime, 0.0f, 1.0f) * B->Sequence->GetDataModel()->PlayLength;
	if (Alpha >= 1.0f)
		return B->Sequence->EvaluateLocalPoseTransforms(TimeB, OutPose);
	TArray<FAnimBoneTransform> PoseA, PoseB;
	return A->Sequence->EvaluateLocalPoseTransforms(TimeA, PoseA) &&
		B->Sequence->EvaluateLocalPoseTransforms(TimeB, PoseB) &&
		BlendAnimPoses(PoseA, PoseB, Alpha, OutPose);
}

bool UBlendSpace1D::ExtractRootMotion(float X, float Y, float PreviousTime, float CurrentTime,
	int32 LoopCount, FMatrix& OutDelta) const
{
	const FSample* A;
	const FSample* B;
	float Alpha;
	if (!GetBlendSamples(X, A, B, Alpha)) return false;
	FMatrix DeltaA, DeltaB;
	const bool HasA = A->Sequence->ExtractRootMotion(PreviousTime * A->Sequence->GetDataModel()->PlayLength,
		CurrentTime * A->Sequence->GetDataModel()->PlayLength, LoopCount, DeltaA);
	if (A == B || Alpha <= 0.0f)
	{
		if (HasA) OutDelta = DeltaA;
		return HasA;
	}
	const bool HasB = B->Sequence->ExtractRootMotion(PreviousTime * B->Sequence->GetDataModel()->PlayLength,
		CurrentTime * B->Sequence->GetDataModel()->PlayLength, LoopCount, DeltaB);
	if (!HasA && !HasB) return false;
	const FBoneTransform TransformA = HasA ? FBoneTransform::FromMatrix(DeltaA) : FBoneTransform::Identity;
	const FBoneTransform TransformB = HasB ? FBoneTransform::FromMatrix(DeltaB) : FBoneTransform::Identity;
	OutDelta = FBoneTransform::Blend(TransformA, TransformB, Alpha).ToMatrix();
	return true;
}

void UBlendSpace1D::GetNotifies(float X, float Y, float PreviousTime, float CurrentTime, int32 LoopCount,
	bool bIncludeStart, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	const FSample* A;
	const FSample* B;
	float Alpha;
	if (!GetBlendSamples(X, A, B, Alpha)) return;
	const float LengthA = A->Sequence->GetDataModel()->PlayLength;
	// Evaluate와 같은 기준: 양 끝이거나 한쪽 가중치가 0이면 한 샘플만 쓴다.
	if (A == B || Alpha <= 0.0f)
	{
		A->Sequence->GetNotifyQueueItems(PreviousTime * LengthA, CurrentTime * LengthA, LoopCount, bIncludeStart, 1.0f, OutQueue);
		return;
	}
	const float LengthB = B->Sequence->GetDataModel()->PlayLength;
	if (Alpha >= 1.0f)
	{
		B->Sequence->GetNotifyQueueItems(PreviousTime * LengthB, CurrentTime * LengthB, LoopCount, bIncludeStart, 1.0f, OutQueue);
		return;
	}
	A->Sequence->GetNotifyQueueItems(PreviousTime * LengthA, CurrentTime * LengthA, LoopCount, bIncludeStart, 1.0f - Alpha, OutQueue);
	B->Sequence->GetNotifyQueueItems(PreviousTime * LengthB, CurrentTime * LengthB, LoopCount, bIncludeStart, Alpha, OutQueue);
}
