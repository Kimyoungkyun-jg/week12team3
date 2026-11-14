#include "EnginePCH.h"
#include "Particles/ParticleEmitterInstance.h"

#include "Core/HAL/UnrealMemory.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"

constexpr int32 MaxParticleCount = 65536;

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	FMemory::Free(ParticleData);
	FMemory::Free(ParticleIndices);
}

bool FParticleEmitterInstance::ResizeStorage(int32 NewCapacity)
{
	static_assert(std::is_trivially_copyable_v<FBaseParticle>);
	static_assert(std::is_trivially_destructible_v<FBaseParticle>);
	static_assert(alignof(FBaseParticle) <= alignof(std::max_align_t));

	if (NewCapacity < 0 || NewCapacity > MaxParticleCount)
	{
		return false;
	}

	if (NewCapacity <= MaxActiveParticles)
	{
		return true;
	}

	assert(ParticleStride == sizeof(FBaseParticle));

	auto* NewData = static_cast<uint8*>(FMemory::Malloc(static_cast<uint64>(NewCapacity) * ParticleStride));
	auto* NewIndices = static_cast<uint16*>(FMemory::Malloc(static_cast<uint64>(NewCapacity) * sizeof(uint16)));

	if (!NewData || !NewIndices)
	{
		FMemory::Free(NewData);
		FMemory::Free(NewIndices);
		return false;
	}

	// 이전 슬롯에 들어 있던 객체를 같은 슬롯 번호로 옮깁니다.
	for (int32 Slot = 0; Slot < MaxActiveParticles; ++Slot)
	{
		void* Destination = NewData + static_cast<size_t>(Slot) * ParticleStride;
		::new (Destination) FBaseParticle(*GetParticleSlot(Slot));
	}

	if (MaxActiveParticles > 0)
	{
		FMemory::Memcpy(NewIndices, ParticleIndices,
			static_cast<uint64>(MaxActiveParticles) * sizeof(uint16));
	}

	for (int32 Slot = MaxActiveParticles; Slot < NewCapacity; ++Slot)
	{
		void* Destination = NewData + static_cast<size_t>(Slot) * ParticleStride;
		::new (Destination) FBaseParticle{};
		NewIndices[Slot] = static_cast<uint16>(Slot);
	}

	FMemory::Free(ParticleData);
	FMemory::Free(ParticleIndices);

	ParticleData = NewData;
	ParticleIndices = NewIndices;
	MaxActiveParticles = NewCapacity;
	return true;
}

FBaseParticle* FParticleEmitterInstance::GetParticleSlot(int32 SlotIndex)
{
	assert(SlotIndex >= 0 && SlotIndex < MaxActiveParticles);
	return reinterpret_cast<FBaseParticle*>(ParticleData + static_cast<size_t>(SlotIndex) * ParticleStride);
}

const FBaseParticle& FParticleEmitterInstance::GetActiveParticle(int32 ActiveIndex) const
{
	assert(ActiveIndex >= 0 && ActiveIndex < ActiveParticles);
	const uint16 SlotIndex = ParticleIndices[ActiveIndex];
	return *reinterpret_cast<const FBaseParticle*>(ParticleData + static_cast<size_t>(SlotIndex) * ParticleStride);
}

void FParticleEmitterInstance::InitStorage(int32 Capacity)
{
	if (!ResizeStorage(std::clamp(Capacity, 0, MaxParticleCount)))
	{
		return;
	}

	ActiveParticles = 0;
	ParticleCounter = 0;
	SpawnRemainder = 0.0f;
}

void FParticleEmitterInstance::KillParticle(int32 ActiveIndex)
{
	if (ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return;
	}

	const int32 LastActiveIndex = ActiveParticles - 1;
	std::swap(ParticleIndices[ActiveIndex], ParticleIndices[LastActiveIndex]);
	--ActiveParticles;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	const int32 SpawnCount = std::min(Count, MaxActiveParticles - ActiveParticles);
	if (SpawnCount <= 0 || !CurrentLODLevel->bEnabled)
	{
		return;
	}

	float LifetimeSeconds = 2.0f;

	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		const uint16 SlotIndex = ParticleIndices[ActiveParticles];
		FBaseParticle& Particle = *GetParticleSlot(ParticleIndices[ActiveParticles]);

		// 재사용 슬롯에 기본값
		Particle = FBaseParticle{};
		Particle.Location = InitialLocation;
		Particle.Velocity = InitialVelocity;
		Particle.BaseVelocity = InitialVelocity;

		for (const UParticleModule* Module : CurrentLODLevel->SpawnModules)
		{
			Module->Spawn(Particle);
		}

		Particle.BaseAlpha = Particle.Color.A;
		++ActiveParticles;
		++ParticleCounter;
	}
}

void FParticleEmitterInstance::Tick(float DeltaTime, const FVector& EmitterLocation)
{
	for (int32 ActiveIndex = ActiveParticles - 1; ActiveIndex >= 0; --ActiveIndex)
	{
		FBaseParticle& Particle = *GetParticleSlot(ParticleIndices[ActiveIndex]);
		Particle.RelativeTime += DeltaTime / Particle.Lifetime;

		if (Particle.RelativeTime >= 1.0f)
		{
			KillParticle(ActiveIndex);
			continue;
		}

		Particle.Location = Particle.Location + Particle.Velocity * DeltaTime;
		Particle.Rotation += Particle.RotationRate * DeltaTime;

		for (const UParticleModule* Module : CurrentLODLevel->UpdateModules)
		{
			Module->Update(Particle);
		}
	}

	if (!CurrentLODLevel->bEnabled)
	{
		return;
	}

	float SpawnRate = 0.0f;
	for (const UParticleModule* Module : CurrentLODLevel->Modules)
	{
		if (Module->IsA<UParticleModuleSpawn>())
		{
			SpawnRate = static_cast<const UParticleModuleSpawn*>(Module)->Rate;
			break;
		}
	}

	const float RequestCount = SpawnRemainder + SpawnRate * DeltaTime;
	const float WholeCount = std::floor(RequestCount);
	const float NextRemainder = RequestCount - WholeCount;

	const int32 AllowedCount = MaxParticleCount - ActiveParticles;
	const int32 SpawnCount = static_cast<int32>(std::min(WholeCount, static_cast<float>(AllowedCount)));
	const int32 RequiredCapacity = ActiveParticles + SpawnCount;

	if (RequiredCapacity > MaxActiveParticles)
	{
		const int32 ExtraSlots = static_cast<int32>(std::sqrt(std::sqrt(static_cast<float>(RequiredCapacity))) + 1.0f);
		const int32 NewCapacity = std::min(RequiredCapacity + ExtraSlots, MaxParticleCount);

		if (!ResizeStorage(NewCapacity))
		{
			return;
		}
	}

	const FVector InitialVelocity{ 0.0f, 0.0f, 1.0f };
	SpawnParticles(SpawnCount, EmitterLocation, InitialVelocity);
	SpawnRemainder = NextRemainder;
}