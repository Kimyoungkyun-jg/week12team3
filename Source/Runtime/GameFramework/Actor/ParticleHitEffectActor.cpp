#include "EnginePCH.h"
#include "GameFramework/Actor/ParticleHitEffectActor.h"
#include "GameFramework/Actor/ParticleEventManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"

AParticleHitEffectActor::AParticleHitEffectActor()
{
	for (int32 i = 0; i < PoolSize; i++)
	{
		const FString Name = "Effect_" + std::to_string(i);
		UParticleSystemComponent* Effect = CreateDefaultSubobject<UParticleSystemComponent>(Name);
		EffectPool.Add(Effect);

		Effect->SetAutoActivate(false);
		Effect->TemplatePath = "Assets/Particles/HitParticles.particle";
		
		if (i == 0)
		{
			SetRootComponent(Effect);
		}
		else
		{
			Effect->SetupAttachment(EffectPool[0]);
		}
	}
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
	UParticleSystemComponent* Chosen = nullptr;

	for (auto& Effect : EffectPool)
	{
		if (!Effect->IsActive() || Effect->HasCompleted())
		{
			Chosen = Effect;
			break;
		}
	}

	if (Chosen == nullptr)
	{
	
	Chosen = EffectPool[NextIndex];
		NextIndex = (NextIndex + 1) % PoolSize;
	}
	
	Chosen->SetWorldLocation(Event.Location, false);
	Chosen->ResetParticles();
	Chosen->Activate(true);
	
}
