#pragma once

#include "Components/SceneComponent.h"
#include "Particles/ParticleSystem.h"
#include "ParticleSystemComponent.generated.h"

class FScene;
class FParticleSceneProxy;

struct FParticleEmitterInstance
{
	UParticleEmitter* SpriteTemplate = nullptr;
	UParticleLODLevel* CurrentLODLevel = nullptr;
	int32 CurrentLODLevelIndex = 0;
	TArray<FBaseParticle> Particles;
	int32 ActiveParticles = 0;
	uint32 ParticleCounter = 0;
	int32 MaxActiveParticles = 0;
	double EmitterTime = 0.0;
	float SpawnFraction = 0.0f;
	FParticleRandom Random;

	void Initialize(UParticleEmitter* Emitter, uint32 Seed);
	void Tick(float DeltaTime, const FVector& Origin);
	void Reset();
};

UCLASS()
class UParticleSystemComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UParticleSystemComponent();
	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime) override;
	void Activate(bool bReset = false) override;
	void SetTemplate(UParticleSystem* InTemplate);
	UParticleSystem* GetTemplate() const { return Template; }
	const TArray<FParticleEmitterInstance>& GetEmitterInstances() const { return EmitterInstances; }
	void ResetParticles();
	void MarkRenderStateDirty() override;
	void OnTransformDirty() override;

	UPROPERTY(EditAnywhere)
	FString TemplatePath;
	UPROPERTY(EditAnywhere)
	bool bPreviewInEditor = true;

private:
	friend class FScene;
	bool ShouldCreateRenderState() const override { return true; }
	void CreateRenderState() override;
	void DestroyRenderState() override;
	void LoadTemplateIfNeeded();
	FParticleSceneProxy* SceneProxy = nullptr;
	UParticleSystem* Template = nullptr;
	TArray<FParticleEmitterInstance> EmitterInstances;
	FString LoadedTemplatePath;
};
