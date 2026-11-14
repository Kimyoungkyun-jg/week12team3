#include "EnginePCH.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleModule.h"

void UParticleEmitter::CacheEmitterModuleInfo()
{
	for (UParticleLODLevel* LOD : LODLevels)
	{  
		LOD->SpawnModule = nullptr;
		LOD->SpawnModules.Reset();
		LOD->UpdateModules.Reset();

		for (UParticleModule* Module : LOD->Modules)
		{
			if (!Module)
			{
				continue;
			}

			if (!LOD->SpawnModule && Module->IsA<UParticleModuleSpawn>())
			{
				LOD->SpawnModule = static_cast<UParticleModuleSpawn*>(Module);
			}

			if (Module->RunsOnSpawn())
			{
				LOD->SpawnModules.Add(Module);
			}

			if (Module->RunsOnUpdate())
			{
				LOD->UpdateModules.Add(Module);
			}
		}
	}
}