#pragma once

#include "Asset/AnimDataModel.h"
#include "Math/Matrix.h"
#include "Math/EngineMath.h"

#include <algorithm>
#include <cmath>

struct FAnimBoneTransform
{
	FVector3f Position = FVector3f::ZeroVector;
	FQuat4f Rotation = FQuat4f::Identity();
	FVector3f Scale = FVector3f::OneVector;
};

inline bool BlendAnimPoses(const TArray<FAnimBoneTransform>& A, const TArray<FAnimBoneTransform>& B,
	float Alpha, TArray<FAnimBoneTransform>& OutPose)
{
	if (A.Num() != B.Num() || !std::isfinite(Alpha)) return false;
	Alpha = std::clamp(Alpha, 0.0f, 1.0f);
	OutPose.SetNum(A.Num());
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		OutPose[Index].Position = FMath::Lerp(A[Index].Position, B[Index].Position, Alpha);
		OutPose[Index].Rotation = FMath::Slerp(A[Index].Rotation, B[Index].Rotation, Alpha);
		OutPose[Index].Scale = FMath::Lerp(A[Index].Scale, B[Index].Scale, Alpha);
	}
	return true;
}

inline FMatrix MakeAnimMatrix(const FVector3f& Position, const FQuat4f& Rotation, const FVector3f& Scale)
{
	FMatrix Result = Rotation.ToFMatrix();
	for (int32 Column = 0; Column < 3; ++Column)
	{
		Result.M[0][Column] *= Scale.X;
		Result.M[1][Column] *= Scale.Y;
		Result.M[2][Column] *= Scale.Z;
	}
	Result.M[3][0] = Position.X;
	Result.M[3][1] = Position.Y;
	Result.M[3][2] = Position.Z;
	return Result;
}

inline bool DecomposeAnimMatrix(const FMatrix& Matrix, FVector3f& OutPosition,
	FQuat4f& OutRotation, FVector3f& OutScale)
{
	for (int32 Row = 0; Row < 4; ++Row)
		for (int32 Column = 0; Column < 4; ++Column)
			if (!std::isfinite(Matrix.M[Row][Column])) return false;
	OutPosition = FVector3f(Matrix.M[3][0], Matrix.M[3][1], Matrix.M[3][2]);
	float Axis[3][3];
	float Scale[3];
	for (int32 Row = 0; Row < 3; ++Row)
	{
		Scale[Row] = std::sqrt(Matrix.M[Row][0] * Matrix.M[Row][0] +
			Matrix.M[Row][1] * Matrix.M[Row][1] + Matrix.M[Row][2] * Matrix.M[Row][2]);
		if (Scale[Row] < 1e-8f) return false;
		for (int32 Column = 0; Column < 3; ++Column)
			Axis[Row][Column] = Matrix.M[Row][Column] / Scale[Row];
	}
	if (Matrix.Determinant() < 0.0f)
	{
		Scale[0] = -Scale[0];
		for (float& Value : Axis[0]) Value = -Value;
	}
	OutScale = FVector3f(Scale[0], Scale[1], Scale[2]);
	const float Trace = Axis[0][0] + Axis[1][1] + Axis[2][2];
	float X, Y, Z, W;
	if (Trace > 0.0f)
	{
		const float S = 2.0f * std::sqrt(Trace + 1.0f);
		W = 0.25f * S;
		X = (Axis[1][2] - Axis[2][1]) / S;
		Y = (Axis[2][0] - Axis[0][2]) / S;
		Z = (Axis[0][1] - Axis[1][0]) / S;
	}
	else if (Axis[0][0] > Axis[1][1] && Axis[0][0] > Axis[2][2])
	{
		const float S = 2.0f * std::sqrt(1.0f + Axis[0][0] - Axis[1][1] - Axis[2][2]);
		W = (Axis[1][2] - Axis[2][1]) / S;
		X = 0.25f * S;
		Y = (Axis[0][1] + Axis[1][0]) / S;
		Z = (Axis[0][2] + Axis[2][0]) / S;
	}
	else if (Axis[1][1] > Axis[2][2])
	{
		const float S = 2.0f * std::sqrt(1.0f + Axis[1][1] - Axis[0][0] - Axis[2][2]);
		W = (Axis[2][0] - Axis[0][2]) / S;
		X = (Axis[0][1] + Axis[1][0]) / S;
		Y = 0.25f * S;
		Z = (Axis[1][2] + Axis[2][1]) / S;
	}
	else
	{
		const float S = 2.0f * std::sqrt(1.0f + Axis[2][2] - Axis[0][0] - Axis[1][1]);
		W = (Axis[0][1] - Axis[1][0]) / S;
		X = (Axis[0][2] + Axis[2][0]) / S;
		Y = (Axis[1][2] + Axis[2][1]) / S;
		Z = 0.25f * S;
	}
	if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Z) || !std::isfinite(W)) return false;
	OutRotation = FQuat4f(X, Y, Z, W).Normalized();
	if (!std::isfinite(OutRotation.X) || !std::isfinite(OutRotation.Y) ||
		!std::isfinite(OutRotation.Z) || !std::isfinite(OutRotation.W)) return false;
	const FMatrix Rebuilt = MakeAnimMatrix(OutPosition, OutRotation, OutScale);
	for (int32 Row = 0; Row < 4; ++Row)
		for (int32 Column = 0; Column < 4; ++Column)
			if (!std::isfinite(Rebuilt.M[Row][Column]) ||
				std::abs(Rebuilt.M[Row][Column] - Matrix.M[Row][Column]) >
				0.001f * std::max(1.0f, std::abs(Matrix.M[Row][Column]))) return false;
	return true;
}
