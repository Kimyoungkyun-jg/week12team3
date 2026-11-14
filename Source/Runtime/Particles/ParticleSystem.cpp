#include "EnginePCH.h"
#include "ParticleSystem.h"
#include "UObject/UObjectGlobals.h"

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

UParticleModule* UParticleLODLevel::AddModule(UClass* ModuleClass, const FName& InName)
{
	if (!ModuleClass || !ModuleClass->IsChildOf(UParticleModule::StaticClass()) || !ModuleClass->Constructor)
	{
		return nullptr;
	}

	for (UParticleModule* Module : Modules)
	{
		if (Module && Module->GetClass() == ModuleClass)
		{
			return Module;
		}
	}
	const FName UniqueName = MakeUniqueObjectName(this, ModuleClass, InName);
	UParticleModule* Module = NewObject<UParticleModule>(this, ModuleClass, UniqueName);
	if (Module)
	{
		Modules.Add(Module);
	}

	return Module;
}

bool UParticleLODLevel::RemoveModule(UParticleModule* Module)
{
	if (!Module)
	{
		return false;
	}
	for (int32 Index = 0; Index < Modules.Num(); ++Index)
	{
		if (Modules[Index] != Module)
		{
			continue;
		}
		Modules.RemoveAt(Index);
		Module->MarkAsGarbage();
		return true;
	}
	return false;
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

void UParticleEmitter::SetMeshEmitterEnabled(bool bEnabled)
{
	if (bEnabled)
	{
		CreateMeshTypeData();
		return;
	}
	if (MeshTypeDataModule)
	{
		MeshTypeDataModule->MarkAsGarbage();
		MeshTypeDataModule = nullptr;
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

bool UParticleSystem::RemoveEmitter(int32 Index)
{
	if (!Emitters.IsValidIndex(Index))
	{
		return false;
	}
	UParticleEmitter* Emitter = Emitters[Index];
	Emitters.RemoveAt(Index);

	if (Emitter)
	{
		Emitter->MarkAsGarbage();
	}
	return true;
}
