#pragma once

#include "Math/Matrix.h"
#include "Math/Vector4.h"

constexpr uint32 ObjectSlotBytes = 256;

struct FPerObjectConstants
{
	FMatrix World;
	FMatrix NormalMatrix;
	// x: bReceiveShadow (GBuffer가 NormalRoughness.w 부호로 넘긴다)
	// y: bReceiveDecal
	FVector4 ShadowFlags; 
};

inline FPerObjectConstants MakePerObjectConstants(
	const FMatrix& World, 
	bool bReceiveShadow = true,
	bool bReceiveDecal = true)
{
	FPerObjectConstants Result{};
	Result.World = World;
	Result.NormalMatrix = World.Inverse().GetTransposed();
	Result.ShadowFlags.X = bReceiveShadow ? 1.0f : 0.0f;
	Result.ShadowFlags.Y = bReceiveDecal ? 1.0f : 0.0f;
	return Result;
}

static_assert(sizeof(FPerObjectConstants) == sizeof(FMatrix) * 2 + sizeof(FVector4));
static_assert(sizeof(FPerObjectConstants) <= ObjectSlotBytes);

struct FViewConstants
{
	FMatrix VP;
	FMatrix View;
	FMatrix Projection;
	FVector4 ViewTimeParams;
};
static_assert(sizeof(FViewConstants) == 208);

struct FPrimitiveSceneData
{
	FMatrix LocalToWorld;
	FMatrix NormalMatrix;
	FVector4 Flags;
};
static_assert(sizeof(FPrimitiveSceneData) == 144);

inline FPrimitiveSceneData MakePrimitiveSceneData(
	const FMatrix& World,
	bool bReceiveShadow = true,
	bool bReceiveDecal = true)
{
	FPrimitiveSceneData Result{};
	Result.LocalToWorld = World;
	Result.NormalMatrix = World.Inverse().GetTransposed();
	Result.Flags.X = bReceiveShadow ? 1.0f : 0.0f;
	Result.Flags.Y = bReceiveDecal ? 1.0f : 0.0f;
	return Result;
}

struct FDrawBatchConstants
{
	uint32 StartInstance = 0;
	uint32 Pad[3]{};
};
static_assert(sizeof(FDrawBatchConstants) == 16);

struct FForwardMaterialConstants
{
	FVector4 BaseColor;
	FVector4 Emissive;
	FVector4 SurfaceParams;
	FVector4 UVTransform;
	FVector4 SpecularColorShininess;
	FVector4 WorldUVParams;
	FVector4 UVScrollAndFlags;
};

static_assert(sizeof(FForwardMaterialConstants) == 112);
static_assert(offsetof(FForwardMaterialConstants, SpecularColorShininess) == 64);

