#pragma once

#include "Components/BillboardComponent.h"
#include "Math/Box.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Render/SceneRenderData.h"

class UStaticMesh;
class UMaterial;
class FRenderQueue;
struct FViewContext;

struct FParticleSpriteRenderData
{
	FVector Location;
	float Width = 1.0f;
	float Height = 1.0f;
	float Rotation = 0.0f;
	UMaterial* Material = nullptr;
	FSubUVConstants Constants{};
};

struct FParticleDynamicGpuEmitterData
{
	TSharedPtr<FGpuParticleEmitterResources> GpuResources;
	UMaterial* Material = nullptr;
	mutable FGpuParticleSimParams SimParams{};
	mutable TArray<uint8> PendingSpawnBytes;
	TArray<FVector4> CurveLUT;
	uint32 MaxParticles = 0;
	uint32 ParticleStride = 0;
	mutable bool bResetRequested = false;
	bool bUseLocalSpace = false;
	FVector4 AtlasParams = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
};

struct FParticleDynamicData
{
	FMatrix LocalToWorld = FMatrix::Identity;
	FVector WorldLocation = FVector::ZeroVector;
	FVector WorldScale = FVector::OneVector;
	FBox Bounds{};
	FPrimitiveRenderStateData BaseState{};
	bool bFixedBounds = false;
	bool bLocalBoundsOnly = false;
	TArray<FParticleSpriteRenderData> Sprites;
	TArray<FParticleDynamicGpuEmitterData> GpuEmitters;
};

class FParticleSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSceneProxy(FParticleDynamicData&& InitialData);
	virtual ~FParticleSceneProxy() override = default;

	void SetDynamicData(FParticleDynamicData&& InData);
	void ApplyParticleTransform(const FPrimitiveTransformData& Data);
	virtual void SubmitToRenderQueue(FRenderQueue& Queue, const FViewContext& View, uint32 LODIndex) const override;
	void SubmitInstancedParticles(TArray<FParticleInstanceData>& OutInstances,
		TArray<FParticleInstancedBatch>& OutBatches, const FViewContext& View) const;
	void SubmitGpuEmitters(TArray<FGpuParticleBatch>& OutBatches, const FViewContext& View) const;
	virtual const FParticleSceneProxy* AsParticleSceneProxy() const override { return this; }
	bool HasParticles() const
	{
		return (!DynamicData.Sprites.IsEmpty() || !DynamicData.GpuEmitters.IsEmpty()) && QuadMesh && Material;
	}

private:
	void SyncPrimitiveState();

	UStaticMesh* QuadMesh = nullptr;
	UMaterial* Material = nullptr;
	FParticleDynamicData DynamicData;
};
