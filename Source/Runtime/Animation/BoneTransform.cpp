#include "EnginePCH.h"
#include "Animation/BoneTransform.h"
#include <cmath>

const FBoneTransform FBoneTransform::Identity{};

FMatrix FBoneTransform::ToMatrix() const
{
	// 회전 행렬의 각 행(축)에 스케일을 곱하고 마지막 행에 위치를 둔다.
	FMatrix Result = Rotation.Normalized().ToFMatrix();
	const float S[3] = { Scale.X, Scale.Y, Scale.Z };
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Col = 0; Col < 3; ++Col)
		{
			Result.M[Row][Col] *= S[Row];
		}
	}
	Result.M[3][0] = Translation.X;
	Result.M[3][1] = Translation.Y;
	Result.M[3][2] = Translation.Z;
	Result.M[3][3] = 1.0f;
	return Result;
}

FQuat QuatFromRotationMatrix(const FMatrix& M)
{
	// UE FQuat(const FMatrix&)와 같은 방법. 대각합이 작으면 가장 큰 대각 성분을 기준으로 풀어 정밀도를 지킨다.
	FQuat Q;
	const float Trace = M.M[0][0] + M.M[1][1] + M.M[2][2];
	if (Trace > 0.0f)
	{
		const float InvS = 1.0f / std::sqrt(Trace + 1.0f);
		Q.W = 0.5f / InvS;
		const float S = 0.5f * InvS;
		Q.X = (M.M[1][2] - M.M[2][1]) * S;
		Q.Y = (M.M[2][0] - M.M[0][2]) * S;
		Q.Z = (M.M[0][1] - M.M[1][0]) * S;
	}
	else
	{
		int32 I = 0;
		if (M.M[1][1] > M.M[0][0]) I = 1;
		if (M.M[2][2] > M.M[I][I]) I = 2;
		static constexpr int32 Next[3] = { 1, 2, 0 };
		const int32 J = Next[I];
		const int32 K = Next[J];

		const float S0 = M.M[I][I] - M.M[J][J] - M.M[K][K] + 1.0f;
		const float InvS = 1.0f / std::sqrt(S0);
		float Qt[4];
		Qt[I] = 0.5f / InvS;
		const float S = 0.5f * InvS;
		Qt[3] = (M.M[J][K] - M.M[K][J]) * S;
		Qt[J] = (M.M[I][J] + M.M[J][I]) * S;
		Qt[K] = (M.M[I][K] + M.M[K][I]) * S;
		Q = FQuat(Qt[0], Qt[1], Qt[2], Qt[3]);
	}
	return Q.Normalized();
}

FBoneTransform FBoneTransform::FromMatrix(const FMatrix& M)
{
	FBoneTransform Result;
	Result.Translation = FVector(M.M[3][0], M.M[3][1], M.M[3][2]);

	FVector Axes[3];
	float Scales[3];
	for (int32 Row = 0; Row < 3; ++Row)
	{
		Axes[Row] = FVector(M.M[Row][0], M.M[Row][1], M.M[Row][2]);
		Scales[Row] = Axes[Row].Length();
	}

	// 반사가 있으면 회전 행렬이 되도록 X축을 뒤집고 그 부호를 스케일에 남긴다.
	if (M.Determinant() < 0.0f)
	{
		Scales[0] = -Scales[0];
	}

	FMatrix Rotation = FMatrix::Identity;
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float S = std::abs(Scales[Row]) > 1.e-8f ? Scales[Row] : 1.0f;
		Rotation.M[Row][0] = Axes[Row].X / S;
		Rotation.M[Row][1] = Axes[Row].Y / S;
		Rotation.M[Row][2] = Axes[Row].Z / S;
	}

	Result.Rotation = QuatFromRotationMatrix(Rotation);
	Result.Scale = FVector(Scales[0], Scales[1], Scales[2]);
	return Result;
}

FBoneTransform FBoneTransform::Blend(const FBoneTransform& A, const FBoneTransform& B, float Alpha)
{
	FBoneTransform Result;
	Result.Translation = A.Translation + (B.Translation - A.Translation) * Alpha;
	Result.Scale = A.Scale + (B.Scale - A.Scale) * Alpha;
	Result.Rotation = FQuat::Slerp(A.Rotation, B.Rotation, Alpha);
	return Result;
}
