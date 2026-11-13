#pragma once

#include "BillboardComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleRenderData.h"
#include "ParticleSystemComponent.generated.h"

struct FParticleEmitterRuntimeData
{
	FParticleEmitterInstance Instance;
	FDynamicSpriteEmitterData SpriteEmitterData;
	UParticleModuleTypeDataMesh* MeshTypeDataModule = nullptr;
	UMaterial* SpriteMaterial = nullptr;
	FSubUVConstants ParticleParams{};
};

UCLASS()
class UParticleSystemComponent : public UBillboardComponent
{
	GENERATED_BODY()

public:
	UParticleSystemComponent();

	void SetTemplate(UParticleSystem* InTemplate);

	virtual void TickComponent(float DeltaTime) override;
	virtual FBox CalcLocalBounds() const override;
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

private:
	void InitializeParticleSystem();

private:
	TArray<TUniquePtr<FParticleEmitterRuntimeData>> EmitterRuntimeData;

	UPROPERTY(EditAnywhere)
	UParticleSystem* Template = nullptr;


	bool bEmitterInitialized = false;
};