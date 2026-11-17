#pragma once

#include "GameFramework/Actor.h"
#include "Components/ParticleSystemComponent.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "ParticleHitEffectActor.generated.h"

UCLASS()
class AParticleHitEffectActor : public AActor
{
	GENERATED_BODY()
public:
	AParticleHitEffectActor();
	void BeginPlay() override;
	void OnHit(const FParticleEventCollideData& Event);

private:
	UParticleSystemComponent* EffectComponent;
};
