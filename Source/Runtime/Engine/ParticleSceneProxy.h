#pragma once

#include "Components/BillboardComponent.h"
#include "Math/Box.h"

class UParticleSystemComponent;
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
	FSubUVConstants Constants{};
};

class FParticleSceneProxy
{
public:
	explicit FParticleSceneProxy(UParticleSystemComponent* InComponent);
	void UpdateFromComponent();
	void SubmitToRenderQueue(FRenderQueue& Queue, const FViewContext& View) const;
	const FBox& GetBounds() const { return Bounds; }
	bool HasParticles() const { return !Sprites.IsEmpty() && QuadMesh && Material; }
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
};
