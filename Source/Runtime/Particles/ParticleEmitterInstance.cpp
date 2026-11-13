#include "EnginePCH.h"
#include "ParticleEmitterInstance.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

FParticleDataContainer::~FParticleDataContainer()
{
	Reset();
}

bool FParticleDataContainer::Allocate(int32 InMaxActiveParticles, int32 InParticleStride)
{
	Reset();
	constexpr int32 MaxParticleCapacity = static_cast<int32>(std::numeric_limits<uint16>::max()) + 1;
	if (InMaxActiveParticles <= 0 || InMaxActiveParticles > MaxParticleCapacity || InParticleStride < static_cast<int32>(sizeof(FBaseParticle)))
	{
		return false;
	}

	const int64 DataBytes = static_cast<int64>(InMaxActiveParticles) * InParticleStride;
	const int64 IndexBytes = static_cast<int64>(InMaxActiveParticles) * sizeof(uint16);
	const int64 TotalBytes = DataBytes + IndexBytes;
	if (TotalBytes > std::numeric_limits<int32>::max())
	{
		return false;
	}
	MemBlockSize = static_cast<int32>(TotalBytes);
	ParticleDataNumBytes = static_cast<int32>(DataBytes);
	ParticleIndicesNumShorts = InMaxActiveParticles;
	ParticleData = new (std::nothrow) uint8[static_cast<size_t>(MemBlockSize)];
	if (!ParticleData)
	{
		Reset();
		return false;
	}

	std::memset(ParticleData, 0, static_cast<size_t>(MemBlockSize));
	ParticleIndices = reinterpret_cast<uint16*>(ParticleData + ParticleDataNumBytes);
	for (int32 Index = 0; Index < InMaxActiveParticles; ++Index)
	{
		ParticleIndices[Index] = static_cast<uint16>(Index);
	}
	return true;
}

void FParticleDataContainer::Reset()
{
	delete[] ParticleData;
	ParticleData = nullptr;
	ParticleIndices = nullptr;
	MemBlockSize = 0;
	ParticleDataNumBytes = 0;
	ParticleIndicesNumShorts = 0;
}

bool FParticleEmitterInstance::Initialize(uint32 InMaxActiveParticles)
{
	Reset();
	ParticleSize = sizeof(FBaseParticle);
	ParticleStride = ParticleSize;
	if (!DataContainer.Allocate(InMaxActiveParticles, ParticleStride))
	{
		return false;
	}
	MaxActiveParticles = InMaxActiveParticles;
	return true;
}

void FParticleEmitterInstance::Reset()
{
	DataContainer.Reset();
	ActiveParticles = 0;
	MaxActiveParticles = 0;
	ParticleCounter = 0;
	ParticleSize = sizeof(FBaseParticle);
	ParticleStride = ParticleSize;
}

FBaseParticle* FParticleEmitterInstance::SpawnParticle()
{
	if (!DataContainer.ParticleIndices || ActiveParticles >= MaxActiveParticles)
	{
		return nullptr;
	}
	const uint16 DataIndex = DataContainer.ParticleIndices[ActiveParticles++];
	uint8* ParticleAddress = DataContainer.ParticleData + static_cast<size_t>(DataIndex) * ParticleStride;
	FBaseParticle* Particle = ::new (static_cast<void*>(ParticleAddress)) FBaseParticle();
	++ParticleCounter;
	return Particle;
}

void FParticleEmitterInstance::KillParticle(int32 ActiveIndex)
{
	if (!DataContainer.ParticleIndices || ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return;
	}
	const int32 LastActiveIndex = ActiveParticles - 1;
	std::swap(DataContainer.ParticleIndices[ActiveIndex], DataContainer.ParticleIndices[LastActiveIndex]);
	--ActiveParticles;
}

FBaseParticle* FParticleEmitterInstance::GetParticle(int32 ActiveIndex)
{
	if (!DataContainer.ParticleIndices || ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return nullptr;
	}
	const uint16 DataIndex = DataContainer.ParticleIndices[ActiveIndex];
	uint8* ParticleAddress = DataContainer.ParticleData + static_cast<size_t>(DataIndex) * ParticleStride;
	return reinterpret_cast<FBaseParticle*>(ParticleAddress);
}

const FBaseParticle* FParticleEmitterInstance::GetParticle(int ActiveIndex) const
{
	if (!DataContainer.ParticleIndices || ActiveIndex < 0 || ActiveIndex >= ActiveParticles)
	{
		return nullptr;
	}
	const uint16 DataIndex = DataContainer.ParticleIndices[ActiveIndex];
	const uint8* ParticleAddress = DataContainer.ParticleData + static_cast<size_t>(DataIndex) * ParticleStride;
	return reinterpret_cast<const FBaseParticle*>(ParticleAddress);
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (!std::isfinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}
	int32 ActiveIndex = 0;
	while (ActiveIndex < ActiveParticles)
	{
		FBaseParticle* Particle = GetParticle(ActiveIndex);
		if (!Particle)
		{
			++ActiveIndex;
			continue;
		}
		Particle->Location += Particle->Velocity * DeltaTime;
		Particle->Rotation += Particle->RotationRate * DeltaTime;
		if (Particle->Lifetime > 0.0f)
		{
			Particle->RelativeTime += DeltaTime / Particle->Lifetime;
			if (Particle->RelativeTime >= 1.0f)
			{
				KillParticle(ActiveIndex);
				continue;
			}
		}
		++ActiveIndex;
	}
}
