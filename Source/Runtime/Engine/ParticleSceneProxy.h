#pragma once

#include "Components/BillboardComponent.h"
#include "Math/Box.h"

class UParticleSystemComponent;
class UStaticMesh;
class UMaterial;
class FRenderQueue;
struct FViewContext;
struct FParticleInstanceData;
struct FParticleInstancedBatch;
struct FGpuParticleBatch;

struct FParticleSpriteRenderData
{
	FVector Location;
	float Width = 1.0f;
	float Height = 1.0f;
	float Rotation = 0.0f;
	UMaterial* Material = nullptr;
	FSubUVConstants Constants{};
};

class FParticleSceneProxy
{
public:
	explicit FParticleSceneProxy(UParticleSystemComponent* InComponent);
	void UpdateFromComponent();
	void SubmitToRenderQueue(FRenderQueue& Queue, const FViewContext& View) const;
	void SubmitInstancedParticles(TArray<FParticleInstanceData>& OutInstances,
		TArray<FParticleInstancedBatch>& OutBatches, const FViewContext& View) const;
	void SubmitGpuEmitters(TArray<FGpuParticleBatch>& OutBatches, const FViewContext& View) const;
	const FBox& GetBounds() const { return Bounds; }
	bool HasParticles() const { return (!Sprites.IsEmpty() || bHasGpuEmitters) && QuadMesh && Material; }
	bool IsShown(bool bGameView) const { return bGameView ? bShownInGame : bShownInEditor; }
	UParticleSystemComponent* GetComponent() const { return Component; }

private:
	UParticleSystemComponent* Component = nullptr;
	UStaticMesh* QuadMesh = nullptr;
	UMaterial* Material = nullptr;
	TArray<FParticleSpriteRenderData> Sprites;
	FBox Bounds{};
	bool bShownInGame = true;
	bool bShownInEditor = true;
	bool bHasGpuEmitters = false;
};
