#pragma once

#include "Particle.h"
#include "Render/Buffer.h"
#include "Render/Vertex.h"
#include "Math/Matrix.h"

class UMaterial;
struct FViewContext;

struct FDynamicEmitterReplayDataBase
{
	virtual ~FDynamicEmitterReplayDataBase() = default;
	TArray<FBaseParticle> ActiveParticles;
	FMatrix ComponentToWorld = FMatrix::Identity;
	FVector ComponentScale = FVector(1.0f, 1.0f, 1.0f);
	UMaterial* Material = nullptr;
};

struct FDynamicSpriteEmitterReplayData : public FDynamicEmitterReplayDataBase
{
};

class FDynamicSpriteEmitterData
{
public:
	bool Initialize(uint32 InMaxParticles);
	bool BuildVertexData(const FDynamicSpriteEmitterReplayData& ReplayData, const FViewContext& ViewContext);
	FVertexBuffer* GetVertexBuffer() const { return VertexBuffer.get(); }
	FIndexBuffer* GetIndexBuffer() const { return IndexBuffer.get(); }
	uint32 GetIndexCount() const { return ActiveIndexCount; }
	float GetSortDistance() const { return SortDistance; }

private:
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
	TArray<FParticleVertex> Vertices;
	TArray<uint32> Indices;
	uint32 MaxParticles = 0;
	uint32 ActiveIndexCount = 0;
	float SortDistance = 0.0f;
};