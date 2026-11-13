#include "EnginePCH.h"
#include "Asset/BlendSpace2D.h"
#include "Asset/AnimSequence.h"
#include "Animation/BoneTransform.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
	struct FPoint2D { double X, Y; };
	struct FEdge2D { int32 A, B; };

	double Cross(const FPoint2D& A, const FPoint2D& B, const FPoint2D& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	}

	bool ContainsInCircle(const FPoint2D& A, const FPoint2D& B, const FPoint2D& C,
		const FPoint2D& P)
	{
		const double AX = A.X - P.X, AY = A.Y - P.Y;
		const double BX = B.X - P.X, BY = B.Y - P.Y;
		const double CX = C.X - P.X, CY = C.Y - P.Y;
		const double Determinant = (AX * AX + AY * AY) * (BX * CY - BY * CX)
			- (BX * BX + BY * BY) * (AX * CY - AY * CX)
			+ (CX * CX + CY * CY) * (AX * BY - AY * BX);
		const double Orientation = Cross(A, B, C);
		return Orientation > 0.0 ? Determinant > 0.0 : Determinant < 0.0;
	}
}

bool UBlendSpace2D::AddSample(float X, float Y, UAnimSequence* Sequence)
{
	if (!std::isfinite(X) || !std::isfinite(Y) || !Sequence || !Sequence->Skeleton ||
		!Sequence->GetDataModel() || !std::isfinite(Sequence->GetDataModel()->PlayLength) ||
		Sequence->GetDataModel()->PlayLength <= 0.0f || !std::isfinite(Sequence->RateScale) ||
		Sequence->RateScale <= 0.0f || (Skeleton && Sequence->Skeleton != Skeleton)) return false;
	for (const FSample& Sample : Samples)
		if (Sample.X == X && Sample.Y == Y) return false;
	if (!Skeleton) Skeleton = Sequence->Skeleton;
	Samples.Add({ X, Y, Sequence });
	RebuildTriangles();
	return true;
}

bool UBlendSpace2D::ReplaceSamples(const TArray<FSample>& InSamples)
{
	UBlendSpace2D Replacement;
	Replacement.Skeleton = Skeleton;
	for (const FSample& Sample : InSamples)
		if (!Replacement.AddSample(Sample.X, Sample.Y, Sample.Sequence)) return false;
	if (Replacement.GetSamples().IsEmpty()) return false;
	Samples = std::move(Replacement.Samples);
	Triangles = std::move(Replacement.Triangles);
	return true;
}

void UBlendSpace2D::RebuildTriangles()
{
	Triangles.Reset();
	const int32 Count = Samples.Num();
	if (Count < 3) return;
	std::vector<FPoint2D> Points;
	Points.reserve(Count + 3);
	double MinX = Samples[0].X, MaxX = MinX, MinY = Samples[0].Y, MaxY = MinY;
	for (const FSample& Sample : Samples)
	{
		Points.push_back({ Sample.X, Sample.Y });
		MinX = std::min(MinX, static_cast<double>(Sample.X));
		MaxX = std::max(MaxX, static_cast<double>(Sample.X));
		MinY = std::min(MinY, static_cast<double>(Sample.Y));
		MaxY = std::max(MaxY, static_cast<double>(Sample.Y));
	}
	const double Span = std::max(MaxX - MinX, MaxY - MinY);
	if (Span <= 0.0) return;
	const double CenterX = (MinX + MaxX) * 0.5, CenterY = (MinY + MaxY) * 0.5;
	Points.push_back({ CenterX - 20.0 * Span, CenterY - Span });
	Points.push_back({ CenterX, CenterY + 20.0 * Span });
	Points.push_back({ CenterX + 20.0 * Span, CenterY - Span });
	std::vector<FTriangle> Working{ { Count, Count + 1, Count + 2 } };
	for (int32 Index = 0; Index < Count; ++Index)
	{
		std::vector<FEdge2D> Boundary;
		std::vector<FTriangle> Remaining;
		for (const FTriangle& Triangle : Working)
		{
			if (!ContainsInCircle(Points[Triangle.A], Points[Triangle.B], Points[Triangle.C], Points[Index]))
			{
				Remaining.push_back(Triangle);
				continue;
			}
			for (FEdge2D Edge : { FEdge2D{ Triangle.A, Triangle.B },
				FEdge2D{ Triangle.B, Triangle.C }, FEdge2D{ Triangle.C, Triangle.A } })
			{
				if (Edge.A > Edge.B) std::swap(Edge.A, Edge.B);
				const auto It = std::find_if(Boundary.begin(), Boundary.end(),
					[&](const FEdge2D& Other) { return Other.A == Edge.A && Other.B == Edge.B; });
				if (It == Boundary.end()) Boundary.push_back(Edge);
				else Boundary.erase(It);
			}
		}
		for (const FEdge2D& Edge : Boundary)
			if (Cross(Points[Edge.A], Points[Edge.B], Points[Index]) != 0.0)
				Remaining.push_back({ Edge.A, Edge.B, Index });
		Working = std::move(Remaining);
	}
	for (const FTriangle& Triangle : Working)
		if (Triangle.A < Count && Triangle.B < Count && Triangle.C < Count)
			Triangles.Add(Triangle);
}

bool UBlendSpace2D::GetBlendSamples(float X, float Y,
	std::array<FWeightedSample, 3>& Out, int32& OutCount) const
{
	OutCount = 0;
	if (Samples.IsEmpty() || !std::isfinite(X) || !std::isfinite(Y)) return false;
	if (Samples.Num() == 1)
	{
		Out[0] = { &Samples[0], 1.0f };
		OutCount = 1;
		return true;
	}
	for (const FSample& Sample : Samples)
		if (Sample.X == X && Sample.Y == Y)
		{
			Out[0] = { &Sample, 1.0f };
			OutCount = 1;
			return true;
		}
	const FPoint2D P{ X, Y };
	for (const FTriangle& Triangle : Triangles)
	{
		const FSample& A = Samples[Triangle.A], &B = Samples[Triangle.B], &C = Samples[Triangle.C];
		const FPoint2D PA{ A.X, A.Y }, PB{ B.X, B.Y }, PC{ C.X, C.Y };
		const double Denominator = Cross(PA, PB, PC);
		if (Denominator == 0.0) continue;
		const double WA = Cross(PB, PC, P) / Denominator;
		const double WB = Cross(PC, PA, P) / Denominator;
		const double WC = 1.0 - WA - WB;
		if (WA < -1e-6 || WB < -1e-6 || WC < -1e-6) continue;
		const double Sum = std::max(0.0, WA) + std::max(0.0, WB) + std::max(0.0, WC);
		Out[0] = { &A, static_cast<float>(std::max(0.0, WA) / Sum) };
		Out[1] = { &B, static_cast<float>(std::max(0.0, WB) / Sum) };
		Out[2] = { &C, static_cast<float>(std::max(0.0, WC) / Sum) };
		OutCount = 3;
		return true;
	}
	// 삼각형 밖이나 모든 샘플이 일직선이면 가장 가까운 두 샘플 사이로 투영한다.
	double BestDistance = std::numeric_limits<double>::infinity();
	double BestLength = std::numeric_limits<double>::infinity();
	for (int32 I = 0; I < Samples.Num(); ++I)
		for (int32 J = I + 1; J < Samples.Num(); ++J)
		{
			const double DX = static_cast<double>(Samples[J].X) - Samples[I].X;
			const double DY = static_cast<double>(Samples[J].Y) - Samples[I].Y;
			const double LengthSquared = DX * DX + DY * DY;
			if (LengthSquared <= 0.0) continue;
			const double T = std::clamp(((X - Samples[I].X) * DX + (Y - Samples[I].Y) * DY) /
				LengthSquared, 0.0, 1.0);
			const double ErrorX = X - Samples[I].X - T * DX;
			const double ErrorY = Y - Samples[I].Y - T * DY;
			const double Distance = ErrorX * ErrorX + ErrorY * ErrorY;
			if (Distance > BestDistance + 1e-9 ||
				(std::abs(Distance - BestDistance) <= 1e-9 && LengthSquared >= BestLength)) continue;
			BestDistance = Distance;
			BestLength = LengthSquared;
			Out[0] = { &Samples[I], static_cast<float>(1.0 - T) };
			Out[1] = { &Samples[J], static_cast<float>(T) };
			OutCount = 2;
		}
	return OutCount > 0;
}

float UBlendSpace2D::GetEffectiveLength(float X, float Y) const
{
	std::array<FWeightedSample, 3> Blend;
	int32 Count;
	if (!GetBlendSamples(X, Y, Blend, Count)) return 0.0f;
	float Length = 0.0f;
	for (int32 I = 0; I < Count; ++I)
		Length += Blend[I].Weight * Blend[I].Sample->Sequence->GetDataModel()->PlayLength /
			Blend[I].Sample->Sequence->RateScale;
	return Length;
}

bool UBlendSpace2D::Evaluate(float X, float Y, float NormalizedTime,
	TArray<FAnimBoneTransform>& OutPose) const
{
	std::array<FWeightedSample, 3> Blend;
	int32 Count;
	if (!Skeleton || !std::isfinite(NormalizedTime) || !GetBlendSamples(X, Y, Blend, Count)) return false;
	float TotalWeight = 0.0f;
	for (int32 I = 0; I < Count; ++I)
	{
		if (Blend[I].Weight <= 0.0f) continue;
		UAnimSequence* Sequence = Blend[I].Sample->Sequence;
		TArray<FAnimBoneTransform> Pose;
		const float Time = std::clamp(NormalizedTime, 0.0f, 1.0f) * Sequence->GetDataModel()->PlayLength;
		if (!Sequence->EvaluateLocalPoseTransforms(Time, Pose)) return false;
		if (TotalWeight == 0.0f) OutPose = std::move(Pose);
		else if (!BlendAnimPoses(OutPose, Pose, Blend[I].Weight / (TotalWeight + Blend[I].Weight), OutPose)) return false;
		TotalWeight += Blend[I].Weight;
	}
	return TotalWeight > 0.0f;
}

bool UBlendSpace2D::ExtractRootMotion(float X, float Y, float PreviousTime, float CurrentTime,
	int32 LoopCount, FMatrix& OutDelta) const
{
	std::array<FWeightedSample, 3> Blend;
	int32 Count;
	if (!GetBlendSamples(X, Y, Blend, Count)) return false;
	FBoneTransform Result;
	float TotalWeight = 0.0f;
	bool bHasRootMotion = false;
	for (int32 I = 0; I < Count; ++I)
	{
		if (Blend[I].Weight <= 0.0f) continue;
		UAnimSequence* Sequence = Blend[I].Sample->Sequence;
		const float Length = Sequence->GetDataModel()->PlayLength;
		FMatrix Delta;
		const bool bHasSampleRootMotion = Sequence->ExtractRootMotion(
			PreviousTime * Length, CurrentTime * Length, LoopCount, Delta);
		const FBoneTransform Transform = bHasSampleRootMotion ? FBoneTransform::FromMatrix(Delta) : FBoneTransform::Identity;
		if (TotalWeight == 0.0f) Result = Transform;
		else Result = FBoneTransform::Blend(Result, Transform,
			Blend[I].Weight / (TotalWeight + Blend[I].Weight));
		TotalWeight += Blend[I].Weight;
		bHasRootMotion |= bHasSampleRootMotion;
	}
	if (bHasRootMotion) OutDelta = Result.ToMatrix();
	return bHasRootMotion;
}

void UBlendSpace2D::GetNotifies(float X, float Y, float PreviousTime, float CurrentTime, int32 LoopCount,
	bool bIncludeStart, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	std::array<FWeightedSample, 3> Blend;
	int32 Count;
	if (!GetBlendSamples(X, Y, Blend, Count)) return;
	for (int32 I = 0; I < Count; ++I)
	{
		if (Blend[I].Weight <= 0.0f) continue;
		const UAnimSequence* Sequence = Blend[I].Sample->Sequence;
		const float Length = Sequence->GetDataModel()->PlayLength;
		Sequence->GetNotifyQueueItems(PreviousTime * Length, CurrentTime * Length, LoopCount, bIncludeStart, Blend[I].Weight, OutQueue);
	}
}
