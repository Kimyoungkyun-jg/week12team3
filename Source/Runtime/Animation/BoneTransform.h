#pragma once

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Matrix.h"

// 본 하나의 자세 (UE FTransform과 같은 TRS 구성).
// 엔진 FTransform은 회전을 FRotator로 들고 있어 블렌딩에 쓰기 어렵다. 애니메이션은 이 타입으로 Lerp/Slerp한다.
// 행 벡터 규약: 행렬은 Scale * Rotation * Translation 순서이고, 부모 기준 Local을 쓰면 Global = Local * ParentGlobal이다.
struct FBoneTransform
{
	FVector Translation = FVector(0.0f, 0.0f, 0.0f);
	FQuat Rotation = FQuat(0.0f, 0.0f, 0.0f, 1.0f);
	FVector Scale = FVector(1.0f, 1.0f, 1.0f);

	FBoneTransform() = default;
	FBoneTransform(const FVector& InTranslation, const FQuat& InRotation, const FVector& InScale)
		: Translation(InTranslation), Rotation(InRotation), Scale(InScale) {}

	FMatrix ToMatrix() const;

	// 행렬을 TRS로 나눈다. 전단(shear)이 있으면 그 성분은 버려진다. 행렬식이 음수면 X 스케일 부호로 반사를 표현한다.
	static FBoneTransform FromMatrix(const FMatrix& M);

	// 위치·스케일은 Lerp, 회전은 최단 경로 Slerp. Alpha = 0이면 A, 1이면 B
	static FBoneTransform Blend(const FBoneTransform& A, const FBoneTransform& B, float Alpha);

	static const FBoneTransform Identity;
};

// 회전 행렬(스케일 없음)에서 사원수를 만든다. FQuat::ToFMatrix의 역변환
FQuat QuatFromRotationMatrix(const FMatrix& M);
