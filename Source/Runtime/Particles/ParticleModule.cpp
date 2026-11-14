#include "EnginePCH.h"
#include "ParticleModule.h"
#include "Particles/ParticleTypes.h"

void UParticleModuleLifetime::Spawn(FBaseParticle& Particle) const
{
	Particle.Lifetime = LifetimeSeconds;
}

void UParticleModuleColorOverLife::Update(FBaseParticle& Particle) const
{
	const float AlphaScale = 1.0f - std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	Particle.Color.A = static_cast<uint8>(std::lround(Particle.BaseAlpha * AlphaScale));
}