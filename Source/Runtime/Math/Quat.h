#pragma once

#include "VectorRegister.h"

using FQuatRegister = FVectorRegister;

//template<typename T>
//struct TVector;
struct FVector;
struct FMatrix;
struct FRotator;

struct FQuat
{
	union
	{
		float V[4];
		struct {
			float X;
			float Y;
			float Z;
			float W;
		};
	};

	FQuat();
	FQuat(float InX, float InY, float InZ, float InW = 1.0f);
	FQuat(FRotator InRotator);

	/* Function */
	FQuat operator * (const FQuat& Other) const;
	FQuat Conjugate() const;
	FQuat UnitInverse() const;
	FQuat Inverse() const;
	FQuat Normalized() const;
	FVector RotateVector(const FVector& V) const;
	FMatrix ToFMatrix() const;
	FRotator ToFRotator() const;

	FVector GetForwardVector() const;
	FVector GetRightVector() const;
	FVector GetUpVector() const;


	/* Static */
	static FQuat Identity();
	static FQuat MakeFromAxisAngle(const FVector& Axis, float AngleRadians);
	static FQuat MakeFromEuler(float Roll, float Pitch, float Yaw);

	// 구면 선형 보간 (UE FQuat::Slerp와 같은 식, TEAM1에서 가져옴). Alpha를 자르지 않는다.
	// 내적이 음수면 반대 부호 쪽으로 돌아 최단 경로로 보간한다. 애니메이션 블렌딩에서 본 회전을 섞을 때 쓴다.
	static FQuat Slerp_NotNormalized(const FQuat& Quat1, const FQuat& Quat2, float Alpha);
	static FQuat Slerp(const FQuat& Quat1, const FQuat& Quat2, float Alpha) { return Slerp_NotNormalized(Quat1, Quat2, Alpha).Normalized(); }
};


