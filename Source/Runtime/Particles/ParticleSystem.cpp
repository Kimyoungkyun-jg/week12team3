#include "EnginePCH.h"
#include "ParticleSystem.h"

void UParticleLODLevel::CreateDefaultSetup()
{
	if (!RequiredModule)
	{
		RequiredModule = CreateDefaultSubobject<UParticleModuleRequired>(FName("RequiredModule"));
	}

	if (!SpawnModule)
	{
		SpawnModule = CreateDefaultSubobject<UParticleModuleSpawn>(FName("SpawnModule"));
	}

	if (Modules.IsEmpty())
	{
		Modules.Add(CreateDefaultSubobject<UParticleModuleLifetime>(FName("Lifetime")));
		Modules.Add(CreateDefaultSubobject<UParticleModuleLocation>(FName("Location")));
		Modules.Add(CreateDefaultSubobject<UParticleModuleVelocity>(FName("Velocity")));
		Modules.Add(CreateDefaultSubobject<UParticleModuleColor>(FName("Color")));
		Modules.Add(CreateDefaultSubobject<UParticleModuleSize>(FName("Size")));
	}
}

void UParticleEmitter::CreateDefaultSetup()
{
	if (LODLevels.IsEmpty())
	{
		LODLevels.Add(CreateDefaultSubobject<UParticleLODLevel>(FName("LOD0")));
	}
	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (LODLevel)
		{
			LODLevel->CreateDefaultSetup();
		}
	}
}

void UParticleSystem::CreateDefaultSetup()
{
	if (Emitters.IsEmpty())
	{
		Emitters.Add(CreateDefaultSubobject<UParticleEmitter>(FName("Emitter0")));
	}

	for (UParticleEmitter* Emitter : Emitters)
	{
		Emitter->CreateDefaultSetup();
	}
}
