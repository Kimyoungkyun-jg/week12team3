#include "EnginePCH.h"
#include "ParticleActor.h"

#include "Asset/AssetManager.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleEmitterInstance.h"

// Todo: subuv
AParticleActor::AParticleActor()
{
	/*ParticleComponent = CreateDefaultSubobject<UParticleSubUVComponent>("UParticleSubUVComponent");
	SetRootComponent(ParticleComponent);

	ParticleComponent->SetSubUVSize(8, 8);
	ParticleComponent->SetFrameRate(12.0f);*/

	ParticleComponent = CreateDefaultSubobject<UParticleSystemComponent>("UParticleSystemComponent");
	SetRootComponent(ParticleComponent);
}

void AParticleActor::RegisterAllComponents()
{
	Super::RegisterAllComponents();

	auto* PSC = Cast<UParticleSystemComponent>(FindComponentByName(FName("UParticleSystemComponent")));

	if (PSC && PSC->IsRegistered() && !PSC->HasTemplate())
	{
		PSC->SetTemplate(MakeTestParticleSystem(this));
	}
}

UParticleSystem* AParticleActor::MakeTestParticleSystem(UObject* Outer)
{
	auto* System = NewObject<UParticleSystem>(Outer);

	auto* Emitter = NewObject<UParticleEmitter>(System);
	System->Emitters.Add(Emitter);

	auto* LOD = NewObject<UParticleLODLevel>(Emitter);
	Emitter->LODLevels.Add(LOD);

	LOD->RequiredModule = NewObject<UParticleModuleRequired>(LOD);
	LOD->RequiredModule->Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");

	auto* Spawn = NewObject<UParticleModuleSpawn>(LOD);
	Spawn->Rate = 10.0f;
	LOD->Modules.Add(Spawn);

	auto* Lifetime = NewObject<UParticleModuleLifetime>(LOD);
	Lifetime->LifetimeSeconds = 2.0f;
	LOD->Modules.Add(Lifetime);

	LOD->Modules.Add(NewObject<UParticleModuleColorOverLife>(LOD));

	return System;
}

