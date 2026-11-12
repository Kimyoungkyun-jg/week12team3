#pragma once

#include <d3d11.h>
#include "Math/Color.h"

inline constexpr int32 MaxBoneInfluences = 4;


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

// GPU 스킨 정점 버퍼의 한 칸. GPU_SKIN 셰이더의 VS_INPUT과 필드 순서가 같아야 한다
struct FGpuSkinVertex
{
	FVertex Vertex; // 0
	uint32 BoneIndex[MaxBoneInfluences] = {}; // 64  uint4 BlendIndices
	float Weight[MaxBoneInfluences] = {}; // 80  float4 BlendWeights
};
static_assert(sizeof(FGpuSkinVertex) == 96);


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
