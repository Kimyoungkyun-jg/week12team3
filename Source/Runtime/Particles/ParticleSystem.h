#pragma once

#include "ParticleEmitter.h"
#include "ParticleSystem.generated.h"

UCLASS()
class UParticleSystem : public UObject
{
	GENERATED_BODY()

public:
	TArray<UParticleEmitter*> Emitters;
};