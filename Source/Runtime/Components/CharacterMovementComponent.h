#pragma once

#include "Components/MovementComponent.h"
#include "CharacterMovementComponent.generated.h"

enum class ECharacterMovementMode : uint8 { Walking, Falling };

UCLASS()
class UCharacterMovementComponent : public UMovementComponent
{
	GENERATED_BODY()
public:
	UCharacterMovementComponent();
	void TickComponent(float DeltaTime) override;
	void RequestJump() { if (CanJump()) bJumpRequested = true; }
	void StopJumping() { bJumpRequested = false; }
	bool CanJump() const { return IsMovingOnGround() && !bJumpRequested; }
	bool IsMovingOnGround() const { return MovementMode == ECharacterMovementMode::Walking; }
	ECharacterMovementMode GetMovementMode() const { return MovementMode; }
	uint32 GetJumpCounter() const { return JumpCounter; }
	void StopMovementImmediately() override;
	void AddRootMotion(const FMatrix& Delta);
	void Launch(const FVector& LaunchVelocity);
	bool IsLaunchActive() const { return bHasPendingLaunch || bLaunchActive; }

	UPROPERTY(EditAnywhere)
	float MaxWalkSpeed = 5.0f;
	UPROPERTY(EditAnywhere)
	float MaxAcceleration = 40.0f;
	UPROPERTY(EditAnywhere)
	float BrakingDecelerationWalking = 40.0f;
	UPROPERTY(EditAnywhere)
	float AirControl = 0.35f;
	UPROPERTY(EditAnywhere)
	float GravityScale = 1.0f;
	UPROPERTY(EditAnywhere)
	float JumpZVelocity = 4.2f;
	UPROPERTY(EditAnywhere)
	float WalkableFloorAngle = 45.0f;

private:
	bool FindFloor(float Distance, FHitResult& OutHit) const;
	bool SweepCapsule(const FVector& Delta, FHitResult& OutHit) const;
	ECharacterMovementMode MovementMode = ECharacterMovementMode::Falling;
	bool bJumpRequested = false;
	uint32 JumpCounter = 0;
	FMatrix PendingRootMotion = FMatrix::Identity;
	bool bHasPendingRootMotion = false;
	FVector PendingLaunchVelocity = FVector::ZeroVector;
	bool bHasPendingLaunch = false;
	bool bLaunchActive = false;
};
