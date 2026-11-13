#include "EnginePCH.h"
#include "Particles/ParticleModule.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace
{
	float RandomRange(float Min, float Max)
	{
		if (!std::isfinite(Min))
		{
			Min = 0.0f;
		}

		if (!std::isfinite(Max))
		{
			Max = Min;
		}
		
		if (Min > Max)
		{
			std::swap(Min, Max);
		}

		const float Alpha = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		return Min + (Max - Min) * Alpha;
	}

	FVector RandomVector(const FVector& Min, const FVector& Max)
	{
		return FVector(RandomRange(Min.X, Max.X), RandomRange(Min.Y, Max.Y), RandomRange(Min.Z, Max.Z));
	}
}

void UParticleModule::Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const
{
}

void UParticleModule::Update(FBaseParticle& Particle, float DeltaTime) const
{
}

void UParticleModuleLifetime::Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const
{
	Particle.Lifetime = std::max(0.001f, RandomRange(MinLifetime, MaxLifetime));
	Particle.RelativeTime = 0.0f;
}

void UParticleModuleLocation::Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const
{
	Particle.Location = EmitterOrigin + RandomVector(MinOffset, MaxOffset);
}

void UParticleModuleVelocity::Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const
{
	Particle.BaseVelocity = RandomVector(MinVelocity, MaxVelocity);
	Particle.Velocity = Particle.BaseVelocity;
}

void UParticleModuleColor::Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const
{
	Particle.Color = StartColor;
}

void UParticleModuleColor::Update(FBaseParticle& Particle, float DeltaTime) const
{
	if (!bUseColorOverLife)
	{
		return;
	}
	const float Alpha = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	Particle.Color.R = StartColor.R + (EndColor.R - StartColor.R) * Alpha;
	Particle.Color.G = StartColor.G + (EndColor.G - StartColor.G) * Alpha;
	Particle.Color.B = StartColor.B + (EndColor.B - StartColor.B) * Alpha;
	Particle.Color.A = StartColor.A + (EndColor.A - StartColor.A) * Alpha;
}

void UParticleModuleSize::Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const
{
	Particle.Size = StartSize;
}

void UParticleModuleSize::Update(FBaseParticle& Particle, float DeltaTime) const
{
	if (!bUseSizeOverLife)
	{
		return;
	}
	const float Alpha = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	Particle.Size = StartSize + (EndSize - StartSize) * Alpha;
}
