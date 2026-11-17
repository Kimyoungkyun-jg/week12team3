#pragma once

#include "GameFramework/Actor.h"
#include "Components/ParticleSystemComponent.h"
#include "ParticleSystemActor.generated.h"

UCLASS()
class AParticleSystemActor : public AActor
{
	GENERATED_BODY()
public:
	AParticleSystemActor();
	void BeginPlay() override;
	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }

	void HandleCollision(const FParticleEventCollideData& Event);

private:
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
};
