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
	void Attack() override;
	void CastBeam();

private:
	UStaticMeshComponent* SwordMeshComponent = nullptr;
	UParticleSystemComponent* SwordRibbonComponent = nullptr;
};
