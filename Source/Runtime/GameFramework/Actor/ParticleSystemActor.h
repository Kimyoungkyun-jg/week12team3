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
	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }

private:
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
};
