#pragma once

#include "ParticleLODLevel.h"
#include "ParticleEmitter.generated.h"

UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()

public:
	void CacheEmitterModuleInfo();

	TArray<UParticleLODLevel*> LODLevels;
};