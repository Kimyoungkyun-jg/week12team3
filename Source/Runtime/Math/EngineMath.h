#pragma once
#include <cmath>
#include "Vector.h"
#include "Vector2.h"
#include "Vector4.h"
//#include "TVector.h"
#include "Matrix.h"
#include "Quat.h"
#include "Rotator.h"
#include "Collision/Ray.h"
#include "VectorRegister.h"
#include "MatrixRegister.h"

#define PI 3.141592f
#define INDEX_NONE -1
#define WORLD_MAX 8796093022208.0f

// 월드 단위는 m(1유닛 = 1m)다. 게임플레이 수치(속도·가속도, BlendSpace 축 등)는 UE와 같은 cm 기준으로
// 다루고 경계에서 이 상수로 변환한다. 엔진 전체를 cm로 바꾸게 되면 CmToWorld를 1로 바꾸면 된다.
inline constexpr float CmToWorld = 0.01f;
inline constexpr float WorldToCm = 1.0f / CmToWorld;

namespace FMath
{
	//static float PI = std::acosf(-1);

	template <typename T>
	static inline T Min(const T& A, const T& B)
	{
		return (A >= B ? B : A);
	}

	template <>
	static inline FVector Min(const FVector& A, const FVector& B)
	{
		return FVector(Min(A.X, B.X), Min(A.Y, B.Y), Min(A.Z, B.Z));
	}

	template <typename T>
	static inline T Max(const T& A, const T& B)
	{
		return (A >= B ? A: B);
	}

	template <>
	static inline FVector Max(const FVector& A, const FVector& B)
	{
		return FVector(Max(A.X, B.X), Max(A.Y, B.Y), Max(A.Z, B.Z));
	}

	template <typename T>
	static inline T Clamp(T InValue, T InMin, T InMax)
	{
		return Max(InMin, Min(InValue, InMax));
	}

	template <typename T>
	static inline T Log2(T InValue)
	{
		return static_cast<T>(std::log2(InValue));
	}

	static inline bool IsNearlyZero(float Value, float ErrorTolerance = 1e-20f)
	{
		return std::abs(Value) <= ErrorTolerance;
	}

	static inline bool IsNearlyEqual(float Value1, float Value2, float ErrorTolerance = 1e-4f)
	{
		return std::abs(Value1 - Value2) <= ErrorTolerance;
	}

	//static inline float Clamp(float Value, float Min, float Max)
	//{
	//	if (Value < Min) return Min;
	//	if (Value > Max) return Max;
	//	return Value;
	//}

	//static inline uint32 Clamp(uint32 Value, uint32 Min, uint32 Max)
	//{
	//	if (Value < Min) return Min;
	//	if (Value > Max) return Max;
	//	return Value;
	//}

	inline FQuat Slerp(const FQuat& Current, const FQuat& Target, float Alpha)
	{
		Alpha = Clamp(Alpha, 0.0f, 1.0f);

		const FQuat A = Current.Normalized();
		FQuat B = Target.Normalized();

		float Dot = A.X * B.X + A.Y * B.Y
			+ A.Z * B.Z + A.W * B.W;

		// q와 -q는 같은 회전이다.
		// 가까운 쪽 표현을 선택해 최단 경로로 보간한다.
		if (Dot < 0.0f)
		{
			B = FQuat(-B.X, -B.Y, -B.Z, -B.W);
			Dot = -Dot;
		}

		Dot = Clamp(Dot, 0.0f, 1.0f);

		float WeightA;
		float WeightB;

		if (Dot > 0.9995f)
		{
			// 매우 가까우면 sin(Theta)가 0에 가까워지므로
			// 선형 보간 후 정규화한다.
			WeightA = 1.0f - Alpha;
			WeightB = Alpha;
		}
		else
		{
			const float Theta = std::acos(Dot);
			const float SinTheta = std::sin(Theta);

			WeightA = std::sin((1.0f - Alpha) * Theta) / SinTheta;
			WeightB = std::sin(Alpha * Theta) / SinTheta;
		}

		return FQuat(
			A.X * WeightA + B.X * WeightB,
			A.Y * WeightA + B.Y * WeightB,
			A.Z * WeightA + B.Z * WeightB,
			A.W * WeightA + B.W * WeightB
		).Normalized();
	}

	// 각도를 (-180, 180] 범위로 변환
	inline float NormalizeAxis(float Angle)
	{
		Angle = std::fmod(Angle, 360.0f);
		if (Angle > 180.0f)
			Angle -= 360.0f;
		else if (Angle <= -180.0f)
			Angle += 360.0f;

		return Angle;
	}

	template<class T>
	inline T Lerp(const T& A, const T& B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	template<class T>
	inline float InverseLerp(const T& A, const T& B, const T& X)
	{
		return (X - A) / (B - A);
	}

	template<>
	inline float InverseLerp(const FVector& A, const FVector& B, const FVector& X)
	{
		const FVector AB = B - A;
		const float LengthSq = AB.Dot(AB);
		return LengthSq > 0.0f ? (X - A).Dot(AB) / LengthSq : 0.0f;
	}


	// Hermite 큐빅: P0, P1: 위치, T0, T1: 접선(속도), A: 0~1
	// 접선 0이면 smoothstep(3t^2 - 2t^3)와 동일
	template<class T>
	inline T CubicInterp(const T& P0, const T& T0, const T& P1, const T& T1, float A)
	{
		const float A2 = A * A;		// A^2
		const float A3 = A2 * A;	// A^3

		return P0 * (2.0f * A3 - 3.0f * A2 + 1.0f)
			+ T0 * (A3 - 2.0f * A2 + A)
			+ T1 * (A3 - A2)
			+ P1 * (-2.0f * A3 + 3.0f * A2);
	}

	// 앞 절반은 EasIn, 뒤 절반은 EaseOut
	template<class T>
	inline T InterpEaseInOut(const T& A, const T& B, float Alpha, float Exp)
	{
		const float K = (Alpha < 0.5f)
			? 0.5f * std::pow(2.0f * Alpha, Exp)
			: 1.0f - 0.5f * std::pow(2.0f * (1.0f - Alpha), Exp);
		return Lerp(A, B, K);
	}

	// 3차 Bezier: P0, P3: 끝점, P1, P2: 제어점
	template<class T>
	inline T BezierInterp(const T& P0, const T& P1, const T& P2, const T& P3, float t)
	{
		const float u = 1.0f - t;
		return P0 * (u * u * u)
			+ P1 * (3.0f * u * u * t)
			+ P2 * (3.0f * u * t * t)
			+ P3 * (t * t * t);
	}

	// 남은 거리의 (dt * Speed) 비율만큼 다가간다. 멀면 빠르고 가까우면 느림
	inline float FInterpTo(float Current, float Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.0f)
			return Target;

		const float Dist = Target - Current;
		if (Dist * Dist < 1.e-8f)
			return Target;

		return Current + Dist * Clamp(DeltaTime * InterpSpeed, 0.0f, 1.0f);
	}

	inline FVector VInterpTo(FVector Current, FVector Target, float DeltaTime, float InterpSpeed)
	{
		if (InterpSpeed <= 0.0f)
			return Target;
		const FVector Dist = Target - Current;
		if (Dist.SizeSquared() < 1.e-8f)
			return Target;
		return Current + Dist * Clamp(DeltaTime * InterpSpeed, 0.0f, 1.0f);
	}

	inline FRotator RInterpTo(const FRotator& Current, const FRotator& Target, float DeltaTime, float InterpSpeed)
	{
		// 시간이 멈추면 속도 설정과 관계없이 현재 회전 유지
		if (DeltaTime == 0.0f)
			return Current;

		if (InterpSpeed <= 0.0f)
			return Target;

		const FRotator Delta(
			NormalizeAxis(Target.Pitch - Current.Pitch), 
			NormalizeAxis(Target.Yaw - Current.Yaw), 
			NormalizeAxis(Target.Roll - Current.Roll)
		);

		constexpr float Tolerance = 1.e-4f; // 0.01도 정도
		if (std::abs(Delta.Pitch) < Tolerance &&
			std::abs(Delta.Yaw) < Tolerance &&
			std::abs(Delta.Roll) < Tolerance)
		{
			return Target;
		}

		const float Alpha = Clamp(InterpSpeed * DeltaTime, 0.0f, 1.0f);

		return FRotator(
			NormalizeAxis(Current.Pitch + Delta.Pitch * Alpha),
			NormalizeAxis(Current.Yaw + Delta.Yaw * Alpha),
			NormalizeAxis(Current.Roll + Delta.Roll * Alpha)
		);
	}

	inline FQuat QInterpTo(
		const FQuat& Current,
		const FQuat& Target,
		float DeltaTime,
		float InterpSpeed)
	{
		if (DeltaTime <= 0.0f)
			return Current;

		if (InterpSpeed <= 0.0f)
			return Target;

		return Slerp(
			Current,
			Target,
			Clamp(InterpSpeed * DeltaTime, 0.0f, 1.0f)
		);
	}

	static inline float RadiansToDegrees(float Radian)
	{
		return Radian * (180.0f / PI);
	}

	static inline float DegreesToRadians(float Degree)
	{
		return Degree * (PI / 180.0f);
	}
	static inline float UnwindDegrees(float Degrees)
	{
		Degrees = std::fmod(Degrees, 360.0f);

		if (Degrees > 180.0f)
		{
			Degrees -= 360.0f;
		}
		else if (Degrees < -180.0f)
		{
			Degrees += 360.0f;
		}

		return Degrees;
	}
	static inline FRotator LerpRotator(const FRotator& A, const FRotator& B, float Alpha)
	{
		return FRotator(
			A.Pitch + FMath::UnwindDegrees(B.Pitch - A.Pitch) * Alpha,
			A.Yaw + FMath::UnwindDegrees(B.Yaw - A.Yaw) * Alpha,
			A.Roll + FMath::UnwindDegrees(B.Roll - A.Roll) * Alpha);
	}

	// 헤더에 정의하는 함수는 inline이어야 여러 .cpp에서 include해도 중복 정의(LNK2005)가 나지 않는다
	inline void FmodReportError(double X, double Y)
	{
		// assert는 인자를 하나만 받는다
		assert(Y != 0 && "FMath::Fmod : Y is zero, this is invalid and would result in NaN!");
	}

	inline float Fmod(float X, float Y)
	{
		const float AbsY = std::abs(Y);
		if (AbsY <= SMALL_NUMBER) // Note: this constant should match that used by VectorMod() implementations
		{
			FmodReportError(X, Y);
			return 0.0;
		}

		return fmodf(X, Y);
	}

	inline double Fmod(double X, double Y)
	{
		const double AbsY = std::abs(Y);
		if (AbsY <= DOUBLE_SMALL_NUMBER) // Note: this constant should match that used by VectorMod() implementations
		{
			FmodReportError(X, Y);
			return 0.0;
		}

		return fmod(X, Y);
	}

}
