#pragma once

#include "BillboardComponent.h"
#include "Particles/ParticleHelper.h"
#include "ParticleSystemComponent.generated.h"


UCLASS()
class UParticleSystemComponent : public UBillboardComponent
{
	GENERATED_BODY()

public:
	UParticleSystemComponent();
	virtual ~UParticleSystemComponent() override;
	
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	virtual FBox CalcLocalBounds() const override;

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;
	
	void SubmitParticleToRenderQueue(FRenderQueue& RenderQueue, int32 ParticleIndex, const FMatrix& WorldMatrix, float CameraDistanceSquared);

private:
	TArray<FSubUVConstants> Constants;

	TArray<FBaseParticle> Particles;
};
