#pragma once

#include "Particles/ParticleTypes.h"

class UParticleEmitter;
class UParticleLODLevel;
class UParticleSystemComponent;

struct FParticleEmitterInstance
{
	FParticleEmitterInstance() = default;
	~FParticleEmitterInstance();

	FParticleEmitterInstance(const FParticleEmitterInstance&) = delete;
	FParticleEmitterInstance& operator=(const FParticleEmitterInstance&) = delete;

	UParticleEmitter* SpriteTemplate = nullptr;
	UParticleSystemComponent* Component = nullptr;
	int32 CurrentLODLevelIndex = 0;
	UParticleLODLevel* CurrentLODLevel = nullptr;

	int32 ParticleSize = static_cast<int32>(sizeof(FBaseParticle));
	int32 ParticleStride = static_cast<int32>(sizeof(FBaseParticle));
	int32 PayloadOffset = 0;
	uint8* InstanceData = nullptr;
	int32 InstancePayloadSize = 0;

	int32 ActiveParticles = 0;
	int32 MaxActiveParticles = 0;
	uint32 ParticleCounter = 0;
	float SpawnRemainder = 0.0f;

	void InitStorage(int32 Capacity);
	bool ResizeStorage(int32 NewCapacity);
	void Tick(float DeltaTime, const FVector& EmitterLocation);

	void SpawnParticles(int32 Count, const FVector& InitialLocation, const FVector& InitialVelocity);
	void KillParticle(int32 ActiveIndex);

	const FBaseParticle& GetActiveParticle(int32 ActiveIndex) const;

private:
	FBaseParticle* GetParticleSlot(int32 SlotIndex);

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
};