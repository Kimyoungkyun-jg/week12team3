#pragma once

#include "Containers/Array.h"
#include "Particles/Particle.h"
#include "Particles/ParticleModule.h"

struct FParticleDataContainer
{
	int32 MemBlockSize = 0;
	int32 ParticleDataNumBytes = 0;
	int32 ParticleIndicesNumShorts = 0;

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;

	FParticleDataContainer() = default;
	~FParticleDataContainer();

	FParticleDataContainer(const FParticleDataContainer&) = delete;
	FParticleDataContainer& operator=(const FParticleDataContainer&) = delete;

	bool Allocate(int32 MaxActiveParticles, int32 ParticleStride);
	void Reset();
};

class FParticleEmitterInstance
{
public:
	bool Initialize(uint32 InMaxActiveParticles);
	void Reset();

	FBaseParticle* SpawnParticle(const FVector& EmitterOrigin = FVector::ZeroVector);
	void KillParticle(int32 ActiveIndex);

	FBaseParticle* GetParticle(int32 ActiveIndex);
	const FBaseParticle* GetParticle(int32 ActiveIndex) const;

	void Tick(float DeltaTime, const FVector& EmitterOrigin = FVector::ZeroVector);

	int32 GetActiveParticleCount() const { return ActiveParticles; }
	int32 GetMaxActiveParticleCount() const { return MaxActiveParticles; }

	void SetModules(const TArray<UParticleModule*>& InModules);

	void SetSpawnModule(UParticleModuleSpawn* InSpawnModule);

private:
	FParticleDataContainer DataContainer;

	int32 ParticleSize = sizeof(FBaseParticle);
	int32 ParticleStride = sizeof(FBaseParticle);

	int32 ActiveParticles = 0;
	int32 MaxActiveParticles = 0;

	uint32 ParticleCounter = 0;

	TArray<UParticleModule*> Modules;

	UParticleModuleSpawn* SpawnModule = nullptr;
	float SpawnAccumulator = 0.0f;
	bool bHasSpawnedInitialBurst = false;
};