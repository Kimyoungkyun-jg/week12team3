#include "EnginePCH.h"
#include "GameFramework/Actor/ParticleHitEffectActor.h"
#include "GameFramework/Actor/ParticleEventManager.h"
#include "Particles/ParticleSystem.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"

AParticleHitEffectActor::AParticleHitEffectActor()
{
	for (int32 i = 0; i < PoolSize; i++)
	{
		const FString Name = "Effect_" + std::to_string(i);
		UParticleSystemComponent* Effect = CreateDefaultSubobject<UParticleSystemComponent>(Name);
		EffectPool.Add(Effect);

		Effect->bPreviewInEditor = false;
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
	if (EffectPool.IsEmpty())
	{
		return;
	}

	UParticleSystemComponent* Chosen = nullptr;

	for (UParticleSystemComponent* Effect : EffectPool)
	{
		if (Effect && (!Effect->IsActive() || Effect->HasCompleted()))
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

	if (!Chosen)
	{
		return;
	}

	if (!Chosen->GetTemplate() && !Chosen->TemplatePath.empty())
	{
		Chosen->SetTemplate(UParticleSystem::LoadFromFile(Chosen->TemplatePath, Chosen));
	}

	// 완료 시 꺼진 가시성을 다시 켜고 즉시 프록시 상태를 갱신
	Chosen->SetWorldLocation(Event.Location, false);
	Chosen->SetVisibility(true);
	Chosen->Activate(true);
	Chosen->PushRenderStateToProxy();
}
