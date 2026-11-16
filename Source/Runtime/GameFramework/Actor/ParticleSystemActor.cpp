#include "EnginePCH.h"
#include "GameFramework/Actor/ParticleSystemActor.h"

AParticleSystemActor::AParticleSystemActor()
{
	ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>("ParticleSystemComponent");
	SetRootComponent(ParticleSystemComponent);

	UParticleSystem* System = NewObject<UParticleSystem>(ParticleSystemComponent);
	UParticleLODLevel* LOD = System->AddEmitter()->AddLODLevel();
	LOD->AddModule(UParticleModuleLifetime::StaticClass());
	LOD->AddModule(UParticleModuleLocation::StaticClass());
	UParticleModuleVelocity* Velocity = static_cast<UParticleModuleVelocity*>(LOD->AddModule(UParticleModuleVelocity::StaticClass()));
	if (UDistributionVectorUniform* VelDist = Cast<UDistributionVectorUniform>(Velocity ? Velocity->StartVelocity : nullptr))
	{
		VelDist->Min = FVector(-0.5f, -0.5f, 0.5f);
		VelDist->Max = FVector(0.5f, 0.5f, 1.5f);
	}
	LOD->AddModule(UParticleModuleColor::StaticClass());
	LOD->AddModule(UParticleModuleSize::StaticClass());
	ParticleSystemComponent->SetTemplate(System);
}
