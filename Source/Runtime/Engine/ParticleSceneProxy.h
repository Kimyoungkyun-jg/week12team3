#pragma once

#include "Components/BillboardComponent.h"
#include "Math/Box.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Render/MeshBatch.h"
#include "Render/SceneRenderData.h"

class UStaticMesh;
class UMaterial;
class FMeshElementCollector;
class FParticleSceneProxy;
struct FViewContext;

struct FParticleRenderData
{
	FVector Location;
	float Width = 1.0f;
	float Height = 1.0f;
	float Rotation = 0.0f;
	UMaterialInterface* Material = nullptr;
	FSubUVConstants Constants{};
	UStaticMesh* Mesh = nullptr;
	FVector MeshScale = FVector::OneVector;
	bool bIsMesh = false;
	bool bUseLocalSpace = false;
};

// 이미터 타입별 렌더 스냅샷 기반 클래스
class FDynamicEmitterDataBase
{
public:
	virtual ~FDynamicEmitterDataBase() = default;
	virtual bool HasParticles() const = 0;
	virtual void GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
		UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const = 0;
};

class FDynamicSpriteEmitterData : public FDynamicEmitterDataBase
{
public:
	TArray<FParticleRenderData> Sprites;

	bool HasParticles() const override { return !Sprites.IsEmpty(); }
	void GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
		UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const override;
};

class FDynamicMeshEmitterData : public FDynamicEmitterDataBase
{
public:
	UStaticMesh* Mesh = nullptr;
	UMaterialInterface* MaterialOverride = nullptr;
	bool bUseLocalSpace = false;
	FMatrix LocalToWorld = FMatrix::Identity;
	TArray<FParticleRenderData> Particles;

	bool HasParticles() const override { return Mesh != nullptr && !Particles.IsEmpty(); }
	void GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
		UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const override;
};

class FDynamicGpuSpriteEmitterData : public FDynamicEmitterDataBase
{
public:
	TSharedPtr<FGpuParticleEmitterResources> GpuResources;
	UMaterialInterface* Material = nullptr;
	mutable FGpuParticleSimParams SimParams{};
	mutable TArray<uint8> PendingSpawnBytes;
	TArray<FVector4> CurveLUT;
	uint32 MaxParticles = 0;
	uint32 ParticleStride = 0;
	mutable bool bResetRequested = false;
	bool bUseLocalSpace = false;
	FMatrix LocalToWorld = FMatrix::Identity;
	FVector WorldLocation = FVector::ZeroVector;
	FVector WorldScale = FVector::OneVector;
	FVector4 AtlasParams = FVector4(1.0f, 1.0f, 0.0f, 0.0f);

	bool HasParticles() const override { return MaxParticles > 0 && GpuResources != nullptr; }
	void GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
		UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const override;
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
	TArray<TUniquePtr<FDynamicEmitterDataBase>> DynamicEmitters;
};

class FParticleSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSceneProxy(FParticleDynamicData&& InitialData);
	virtual ~FParticleSceneProxy() override = default;

	void SetDynamicData(FParticleDynamicData&& InData);
	void ApplyParticleTransform(const FPrimitiveTransformData& Data);
	virtual void GetDynamicMeshElements(const FViewContext& View, FMeshElementCollector& Collector, uint32 LODIndex) const override;
	virtual const FParticleSceneProxy* AsParticleSceneProxy() const override { return this; }
	bool HasParticles() const;

private:
	void SyncPrimitiveState();

	UStaticMesh* QuadMesh = nullptr;
	UMaterialInterface* Material = nullptr;
	FParticleDynamicData DynamicData;
};
