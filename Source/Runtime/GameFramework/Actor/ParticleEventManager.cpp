#include "EnginePCH.h"
#include "GameFramework/Actor/ParticleEventManager.h"
#include <UObject/UObjectIterator.h>

AParticleEventManager::AParticleEventManager()
{
}

void AParticleEventManager::BeginPlay()
{
	Super::BeginPlay();
	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld())
		{
			continue;
		}
		It->OnParticleCollision.AddDynamic(this, &AParticleEventManager::HandleCollision);
	
	}
}


void AParticleEventManager::HandleCollision(const FParticleEventCollideData& Event)
{
	HTR_LOG(Warning, "[Manager] event={}", Event.EventName);
	FParticleCollisionSignature * Listener = Listeners.Find(Event.EventName);
	if (Listener == nullptr)
	{
		return;
	}
	Listener->Broadcast(Event);
}

void AParticleEventManager::UnregisterListener(const FString& Name, FDelegateHandle Handle)
{
	FParticleCollisionSignature* Listener = Listeners.Find(Name);
	if (Listener == nullptr)
	{
		return;
	}
	Listener->Remove(Handle);
}

