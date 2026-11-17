#pragma once

#include "GameFramework/Actor.h"
#include "Components/ParticleSystemComponent.h"
#include "ParticleEventManager.generated.h"

UCLASS()
class AParticleEventManager: public AActor
{
	GENERATED_BODY()
public:
	AParticleEventManager();
	void BeginPlay() override;
	void HandleCollision(const FParticleEventCollideData& Event);

	template <typename T>
	FDelegateHandle RegisterListener(const FString& Name, T* Instance, void (T::* Func)(const FParticleEventCollideData& Data)) {
		return Listeners.FindOrAdd(Name).AddDynamic(Instance, Func);
	}
	void UnregisterListener(const FString& Name, FDelegateHandle Handle);

private:
	TMap<FString, FParticleCollisionSignature> Listeners;
};
