#include "EnginePCH.h"
#include "ParticleModule.h"

#include <random>

#include "Particles/ParticleTypes.h"

namespace
{
float RandomInRange(float Min, float Max)
{
	static thread_local std::mt19937 Generator(std::random_device{}());
	std::uniform_real_distribution<float> Distribution(
		std::min(Min, Max), std::max(Min, Max));
	return Distribution(Generator);
}
}

void UParticleModuleLifetime::Spawn(FBaseParticle& Particle) const
{
	Particle.Lifetime = LifetimeSeconds;
}

void UParticleModuleColorOverLife::Update(FBaseParticle& Particle) const
{
	const float AlphaScale = 1.0f - std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	Particle.Color.A = static_cast<uint8>(std::lround(Particle.BaseAlpha * AlphaScale));
}

void UParticleModuleInitialSize::Spawn(FBaseParticle& Particle) const
{
	const float Scale = RandomInRange(MinSize, MaxSize);
	Particle.Size = FVector(Scale, Scale, Scale);
}

void UParticleModuleInitialVelocity::Spawn(FBaseParticle& Particle) const
{
	Particle.Velocity = FVector(
		RandomInRange(MinVelocity.X, MaxVelocity.X),
		RandomInRange(MinVelocity.Y, MaxVelocity.Y),
		RandomInRange(MinVelocity.Z, MaxVelocity.Z));

	Particle.BaseVelocity = Particle.Velocity;
}