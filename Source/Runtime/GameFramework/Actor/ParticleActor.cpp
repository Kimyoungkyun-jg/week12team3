#include "EnginePCH.h"
#include "ParticleActor.h"

// Todo: subuv
AParticleActor::AParticleActor()
{
	ParticleComponent = CreateDefaultSubobject<UParticleSystemComponent>("UParticleSystemComponent");
	SetRootComponent(ParticleComponent);
}

//UParticleSubUVComponent* AParticleActor::GetParticleComponent() const
//{
//	return static_cast<UParticleSubUVComponent*>(RootComponent);
//}


