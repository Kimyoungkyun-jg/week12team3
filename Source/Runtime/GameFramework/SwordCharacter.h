#pragma once

#include "GameFramework/Character.h"
#include "SwordCharacter.generated.h"

class AZombieActor;
class UParticleSystemComponent;
class UStaticMeshComponent;
struct FParticleEventCollideData;

UCLASS()
class ASwordCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ASwordCharacter();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void Jump() override;
	void Attack() override;
	void CastBeam();
	void ReactToHit();
	void OnParticleCollide(const FParticleEventCollideData& Event) override;
	bool IsDead() const { return Health <= 0; }
	int32 GetHealth() const { return Health; }

private:
	bool IsPlayingFullBodyMontage() const;
	UStaticMeshComponent* SwordMeshComponent = nullptr;
	UParticleSystemComponent* SwordRibbonComponent = nullptr;
	TMap<AZombieActor*, float> ZombieHitCooldowns;
	float ElapsedTime = 0.0f;
	UPROPERTY(EditAnywhere)
	int32 Health = 5;
	bool bBeamMontageActive = false;
};
