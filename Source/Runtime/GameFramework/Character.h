#pragma once

#include "Pawn.h"
#include <Math/Vector.h>
#include "Character.generated.h"

class USkeletalMeshComponent;
class UCapsuleComponent;
class UCharacterMovementComponent;
struct FHitResult;

UCLASS()
class ACharacter : public APawn
{
	GENERATED_BODY()

public:
	ACharacter();
	virtual void AddControllerYawInput(float Value) override;
	virtual void AddControllerPitchInput(float Value) override;

	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
	UCapsuleComponent* CapsuleComponent = nullptr;
	UCharacterMovementComponent* CharacterMovementComponent = nullptr;

	virtual FVector GetVelocity() const override;

	// UE ACharacter::Jump: 바닥에 있으면 CharacterMovementComponent::JumpZVelocity로 뛴다
	virtual void Jump();
	virtual bool CanJump() const;
	bool IsFalling() const;
	// 공중에서 바닥에 닿은 순간 (점프 착지, 떨어진 뒤 착지 모두)
	virtual void Landed(const FHitResult& Hit) {}
};
