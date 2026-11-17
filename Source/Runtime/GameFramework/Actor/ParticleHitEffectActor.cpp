#include "EnginePCH.h"
#include "GameFramework/Actor/ParticleHitEffectActor.h"
#include "GameFramework/Actor/ParticleEventManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"

AParticleHitEffectActor::AParticleHitEffectActor()
{
	EffectComponent = CreateDefaultSubobject<UParticleSystemComponent>("EffectComponent");
	SetRootComponent(EffectComponent);
	EffectComponent->SetAutoActivate(false);
	EffectComponent->TemplatePath = "Assets/Particles/HitParticles.particle";
}

void AParticleHitEffectActor::BeginPlay()
{
	Super::BeginPlay();

	for (TObjectIterator<AParticleEventManager> Iter; Iter; ++Iter)
	{
		if (Iter->GetWorld() != GetWorld())
		{
			continue;
		}

		Iter->RegisterListener("Hit", this, &AParticleHitEffectActor::OnHit);
	}
}

void AParticleHitEffectActor::OnHit(const FParticleEventCollideData& Event)
{
	if (EffectComponent->IsActive() && !EffectComponent->HasCompleted())
	{
		return;
	}

	EffectComponent->SetWorldLocation(Event.Location, false);
	EffectComponent->ResetParticles();
	EffectComponent->Activate(true);
}
