#pragma once

#include "Containers/Array.h"
#include "Particle.h"

class FParticleEmitterInstance
{
public:
	void Initialize(uint32 InMaxActiveParticles);
	void Reset();
	FBaseParticle* SpawnParticle();
	void Tick(float DeltaTime);
	const TArray<FBaseParticle>& GetActiveParticles() const { return ActiveParticles; }

private:
	TArray<FBaseParticle> ActiveParticles;
	uint32 MaxActiveParticles = 0;
};