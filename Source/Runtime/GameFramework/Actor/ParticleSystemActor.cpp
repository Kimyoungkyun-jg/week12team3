#include "EnginePCH.h"
#include "ParticleSystemActor.h"
#include "Components/ParticleSystemComponent.h"

AParticleSystemActor::AParticleSystemActor()
{
	ParticleComponent = CreateDefaultSubobject<UParticleSystemComponent>("UParticleSystemComponent");
	SetRootComponent(ParticleComponent);
}
