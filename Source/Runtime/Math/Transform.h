#pragma once

#include "Math/EngineMath.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"

struct FTransform
{
	FTransform() = default;
	FTransform(FVector InL, FVector InS, FRotator InR);
	FTransform(FVector InL, FVector InS, const FQuat& InR);
	FTransform(const FRotator& InR, const FVector& InL, const FVector& InS = FVector::OneVector)
		: Location(InL), Scale(InS), Rotation(InR.Quaternion()) {}
	FTransform(const FQuat& InR, const FVector& InL, const FVector& InS = FVector::OneVector)
		: Location(InL), Scale(InS), Rotation(InR) {}

	static FTransform Identity;

    FMatrix GetLocalMatrix() const;

	FVector GetForward() const;
	FVector GetUp() const;
	FVector GetRight() const;
	FQuat GetOrientation() const;

	// Unreal FTransform::GetRelativeTransform: this를 Other 기준 공간으로 표현한 Transform (= this × Other⁻¹)
	// 예) End.GetRelativeTransform(Start) = Start 시점 기준으로 End까지 이동/회전한 양
	FTransform GetRelativeTransform(const FTransform& Other) const;

	FVector Location = FVector(0, 0, 0);
	FVector Scale = FVector(1, 1, 1);
	FQuat Rotation = FQuat::Identity();
};

// UE FRotationConversionCache: 쿼터니언은 입력한 오일러 값(270도, Pitch 90도 등)을 기억하지 못한다.
// 편집 UI와 씬 컴포넌트가 입력값을 그대로 돌려줄 수 있도록 마지막 변환 쌍을 보관한다.
struct FRotationConversionCache
{
	const FRotator& QuatToRotator(const FQuat& Quat) const
	{
		if (Quat.X != CachedQuat.X || Quat.Y != CachedQuat.Y || Quat.Z != CachedQuat.Z || Quat.W != CachedQuat.W)
		{
			CachedQuat = Quat;
			CachedRotator = Quat.ToFRotator();
		}
		return CachedRotator;
	}

	const FQuat& RotatorToQuat(const FRotator& Rotator) const
	{
		if (Rotator.Pitch != CachedRotator.Pitch || Rotator.Yaw != CachedRotator.Yaw || Rotator.Roll != CachedRotator.Roll)
		{
			CachedRotator = Rotator;
			CachedQuat = Rotator.Quaternion();
		}
		return CachedQuat;
	}

private:
	mutable FQuat CachedQuat = FQuat::Identity();
	mutable FRotator CachedRotator = FRotator(0, 0, 0);
};
