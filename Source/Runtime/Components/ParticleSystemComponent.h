#pragma once

#include "BillboardComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "ParticleSystemComponent.generated.h"

UCLASS()
class UParticleSystemComponent : public UBillboardComponent
{
	GENERATED_BODY()

public:
	UParticleSystemComponent();

	virtual void TickComponent(float DeltaTime) override;
	virtual FBox CalcLocalBounds() const override;
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

private:
	void CreateInitialParticle();

private:
	FParticleEmitterInstance EmitterInstance;
	TArray<FSubUVConstants> ParticleConstants;
};