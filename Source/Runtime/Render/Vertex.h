#pragma once

#include <d3d11.h>
#include "Math/Color.h"

// StaticMesh용 정점. Input Layout을 셰이더 리플렉션(APPEND_ALIGNED)으로 만들므로
// 이 메시를 그리는 셰이더의 VS_INPUT은 이 필드 순서와 같아야 한다.
struct FVertex
{
	FVector Position = FVector(); // 0
	FVector Normal = FVector(); // 12
	FVector4 Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f); // 24  w = B 부호
	FVector2 UV = FVector2(); // 40
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f); // 48
};

struct FLineBatchVertex
{
	FVector Position = FVector();
	FVector2 UV = FVector2();
	FVector4 Color = FVector4();
};

struct FTextVertex
{
	FVector Position;
	FVector2 UV;
};

// Todo: subuv
struct FParticleVertex
{
	FVector Position;
	FVector2 UV;
};

struct FSkeletalVertex
{
	FVector Position = FVector();
	FVector Normal = FVector();
	FVector4 Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
	FVector2 UV = FVector2();
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);	// 48
	int32 BoneIndices[4] = { 0, 0, 0, 0 };				// 64
	float BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };	// 80
};