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
	auto* Velocity = static_cast<UParticleModuleVelocity*>(LOD->AddModule(UParticleModuleVelocity::StaticClass()));
	Velocity->Min = FVector(-0.5f, -0.5f, 0.5f);
	Velocity->Max = FVector(0.5f, 0.5f, 1.5f);
	LOD->AddModule(UParticleModuleColor::StaticClass());
	LOD->AddModule(UParticleModuleSize::StaticClass());
	ParticleSystemComponent->SetTemplate(System);
}

void AParticleSystemActor::BeginPlay()
{
	Super::BeginPlay();
	ParticleSystemComponent->OnParticleCollision.AddDynamic(this, &AParticleSystemActor::HandleCollision);
}


void AParticleSystemActor::HandleCollision(const FParticleEventCollideData& Event)
{
	HTR_LOG(Warning, "[Particle Collider] loc=({},{},{})", Event.Location.X, Event.Location.Y, Event.Location.Z);
}