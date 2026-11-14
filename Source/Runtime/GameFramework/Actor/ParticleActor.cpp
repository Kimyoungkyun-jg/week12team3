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

	{
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

		auto* Velocity = NewObject<UParticleModuleInitialVelocity>(LOD);
		Velocity->MinVelocity = FVector(-1.0f, -1.0f, 1.0f);
		Velocity->MaxVelocity = FVector(1.0f, 1.0f, 2.0f);
		LOD->Modules.Add(Velocity);
	}

	// Mesh Emitter
	{
		auto* MeshEmitter = NewObject<UParticleEmitter>(System);
		System->Emitters.Add(MeshEmitter);

		auto* MeshLOD = NewObject<UParticleLODLevel>(MeshEmitter);
		MeshEmitter->LODLevels.Add(MeshLOD);

		MeshLOD->RequiredModule = NewObject<UParticleModuleRequired>(MeshLOD);

		auto* MeshTypeData = NewObject<UParticleModuleTypeDataMesh>(MeshLOD);
		MeshTypeData->Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
		MeshLOD->TypeDataModule = MeshTypeData;

		auto* MeshSpawn = NewObject<UParticleModuleSpawn>(MeshLOD);
		MeshSpawn->Rate = 10.0f;
		MeshLOD->Modules.Add(MeshSpawn);

		auto* MeshLifetime = NewObject<UParticleModuleLifetime>(MeshLOD);
		MeshLifetime->LifetimeSeconds = 1.0f;
		MeshLOD->Modules.Add(MeshLifetime);

		auto* MeshSize = NewObject<UParticleModuleInitialSize>(MeshLOD);
		MeshSize->MinSize = 0.1f;
		MeshSize->MaxSize = 0.25f;
		MeshLOD->Modules.Add(MeshSize);

		auto* MeshVelocity = NewObject<UParticleModuleInitialVelocity>(MeshLOD);
		MeshVelocity->MinVelocity = FVector(-2.0f, -2.0f, 1.0f);
		MeshVelocity->MaxVelocity = FVector(2.0f, 2.0f, 3.0f);
		MeshLOD->Modules.Add(MeshVelocity);
	}

	return System;
}

