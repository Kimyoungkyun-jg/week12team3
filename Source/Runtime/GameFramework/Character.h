#pragma once

#include "GameFramework/Pawn.h"
#include "Character.generated.h"

class UCapsuleComponent;
class UCameraComponent;
class UCharacterMovementComponent;
class UMaterialInstanceDynamic;
class UParticleSystemComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
struct FParticleEventCollideData;

UCLASS()
class ACharacter : public APawn
{
	GENERATED_BODY()
public:
	ACharacter();
	void BeginPlay() override;
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	virtual void Jump();
	virtual void StopJumping();
	void LaunchCharacter(const FVector& Direction, float Strength);
	virtual void Attack() {}
	float PlayAnimMontage(const FString& MontagePath, float InPlayRate = 1.0f, const FString& StartSection = "");
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
	UCharacterMovementComponent* GetCharacterMovement() const { return CharacterMovement; }
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }
	USpringArmComponent* GetSpringArmComponent() const { return SpringArmComponent; }
	void SetDeathDissolveParams(float InDuration, const FVector4& InEdgeColor);
	void StartDeathDissolve();
	void UpdateDeathDissolve(float DeltaTime);
	bool HasStartedDeathDissolve() const { return bDissolveStarted; }
	bool IsDeathDissolveFinished() const;
	virtual void OnParticleCollide(const FParticleEventCollideData& Event) {}
protected:
	void SetDeathParticlePath(const FString& InPath);
	FString DeathParticlePath = "Assets/Particles/ZombieDeathParticles.particle";
	FVector4 DissolveEdgeColor = FVector4(0.75f, 0.78f, 0.85f, 1.0f);
	float DissolveDuration = 1.45f;
private:
	void EnsureDeathDissolveSetup();
	UCapsuleComponent* CapsuleComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;
	UCharacterMovementComponent* CharacterMovement = nullptr;
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
	USpringArmComponent* SpringArmComponent = nullptr;
	UParticleSystemComponent* DeathParticleComponent = nullptr;
	TArray<UMaterialInstanceDynamic*> DissolveMaterials;
	bool bDissolveStarted = false;
	bool bDissolveCompleted = false;
	float DissolveElapsed = 0.0f;
};
