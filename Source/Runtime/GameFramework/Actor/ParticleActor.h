#pragma once

#include "GameFramework/Actor.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "ParticleActor.generated.h"

// Todo: subuv
UCLASS()
class AParticleActor : public AActor
{
	GENERATED_BODY()


public:
	AParticleActor();
	void RegisterAllComponents() override;

private:
	UParticleSystem* MakeTestParticleSystem(UObject* Outer);

	//UParticleSubUVComponent* ParticleComponent;
	UParticleSystemComponent* ParticleComponent = nullptr;
};
