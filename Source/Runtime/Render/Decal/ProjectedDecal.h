#pragma once

#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Transform.h"
#include "Render/RenderStates.h"

#include <cmath>
#include <cstddef>

class FTexture2D;

// 컴포넌트에서 복사한 이번 뷰의 렌더입력
// 컴포넌트 자체의 포인터는 보관하지 않는다.
struct FProjectedDecalRenderData
{
	FMatrix DecalToWorld = FMatrix::Identity;

	// Material.BaseColor
	FVector4 TintOpacity = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// 에셋이 소유하는 텍스처의 비소유 포인터.
	FTexture2D* ColorTexture = nullptr;
	FTexture2D* NormalTexture = nullptr;

	float NormalStrength = 1.0f;
	float NormalOpacity = 1.0f;
	float NormalEncoding = 0.0f; // 0: 없음/사용 불가, 1: RGB UNorm, 2: BC5 UNorm, 3: BC5 SNorm

	bool bFlipNormalY = false;

	ESamplerState Sampler = ESamplerState::LinearClamp;

	// UNORM 색상 텍스처는 셰이더에서 선형화.
	// SRGB SRV는 하드웨어가 선형화하므로 false
	bool bDecodeSRGB = true;
	bool bUnlit = false;

	// 작은값부터 그린다. 큰 값이 나중에 덮인다.
	int32 SortOrder = 0;
	// 같은 SortOrder에서 실행 중 순서를 고정하는 용도.
	uint32 ComponentID = 0;
};

struct alignas(16) FProjectedDecalConstants
{
	FMatrix WorldToDecal;
	FVector4 TintOpacity; // rgb: 색상, a: 불투명도



	// X: 수동 sRGB 디코딩
	// Y: 노말 인코딩
	// Z: 노말 Y 반전
	// W: 노말 Strength
	FVector4 TextureParams;

	FVector4 NormalTangent;
	FVector4 NormalBitangent;
	FVector4 NormalDirection;

	// X: NormalOpacity
	// YZW: 예약
	FVector4 NormalParams;
};

struct alignas(16) FProjectedDecalDepthViewConstants
{
	FMatrix InverseViewProjection;
	FVector4 ViewportSize;
};

struct alignas(16) FProjectedDecalVolumeConstants
{
	FMatrix DecalToClip;
};

// 양수 스케일만 허용한다.
// 0에 가까운 스케일은 역행렬 계산 문제를 피하기 위해 제외한다.
inline bool IsValidProjectedDecalRenderData(
	const FProjectedDecalRenderData& Data)
{
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Column = 0; Column < 4; ++Column)
		{
			if (!std::isfinite(
				Data.DecalToWorld.M[Row][Column]))
			{
				return false;
			}
		}
	}

	const float Determinant = Data.DecalToWorld.Determinant();

	return
		std::isfinite(Determinant) &&
		std::abs(Determinant) > 1e-12f;
}

// 호출전에 valid 검사 필요
inline FProjectedDecalConstants MakeProjectedDecalConstants(
	const FProjectedDecalRenderData& Data)
{
	FProjectedDecalConstants Constants{};
	Constants.WorldToDecal = Data.DecalToWorld.Inverse();
	Constants.TintOpacity = Data.TintOpacity;
	Constants.TextureParams = FVector4(
		Data.bDecodeSRGB ? 1.0f : 0.0f,
		Data.NormalEncoding, 
		Data.bFlipNormalY ? 1.0f : 0.0f, 
		Data.NormalStrength);

	// UV의 U 증가 방향.
	FVector T = Data.DecalToWorld.TransformVector(
		FVector(0.0f, 1.0f, 0.0f));

	// UV의 V 증가 방향.
	FVector B = Data.DecalToWorld.TransformVector(
		FVector(0.0f, 0.0f, -1.0f));

	bool bValidFrame = false;

	if (FVector::Dot(T, T) > 1e-12f)
	{
		T = T.Normalized();

		// 비균등 스케일·부모 변환의 영향을 받더라도
		// 직교하는 투영 프레임을 만든다.
		B = B - T * FVector::Dot(T, B);

		if (FVector::Dot(B, B) > 1e-12f)
		{
			B = B.Normalized();

			const FVector N =
				FVector::Cross(T, B).Normalized();

			Constants.NormalTangent =
				FVector4(T.X, T.Y, T.Z, 0.0f);

			Constants.NormalBitangent =
				FVector4(B.X, B.Y, B.Z, 0.0f);

			Constants.NormalDirection =
				FVector4(N.X, N.Y, N.Z, 0.0f);

			bValidFrame = true;
		}
	}

	const bool bUseNormal =
		bValidFrame &&
		!Data.bUnlit &&
		Data.NormalTexture &&
		Data.NormalEncoding > 0.0f;

	Constants.NormalParams = FVector4(
		bUseNormal ? Data.NormalOpacity : 0.0f,
		0.0f,
		0.0f,
		0.0f);

	return Constants;
}