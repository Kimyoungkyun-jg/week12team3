#pragma once

#include "GameFramework/Pawn.h"
#include "Character.generated.h"

class UCapsuleComponent;
class UCameraComponent;
class UCharacterMovementComponent;
class USkeletalMeshComponent;
class USpringArmComponent;

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
private:
	UCapsuleComponent* CapsuleComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;
	UCharacterMovementComponent* CharacterMovement = nullptr;
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
	USpringArmComponent* SpringArmComponent = nullptr;
};
