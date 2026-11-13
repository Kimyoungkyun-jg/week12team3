#pragma once

#include "BillboardComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleSystem.h"
#include "ParticleSystemComponent.generated.h"

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
	FParticleEmitterInstance EmitterInstance;
	TArray<FSubUVConstants> ParticleConstants;

	UPROPERTY(EditAnywhere)
	UParticleSystem* Template = nullptr;

	bool bEmitterInitialized = false;
};