#pragma once

#include "GameFramework/Character.h"
#include "SwordCharacter.generated.h"

class UParticleSystemComponent;
class UStaticMeshComponent;

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

private:
	bool IsPlayingFullBodyMontage() const;
	UStaticMeshComponent* SwordMeshComponent = nullptr;
	UParticleSystemComponent* SwordRibbonComponent = nullptr;
	bool bBeamMontageActive = false;
};
