#pragma once

#include "GameFramework/Character.h"
#include "MyCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
struct FHitResult;

UCLASS()
class AMyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMyCharacter();

	// ACharacter는 루트를 직접 돌리지만, 여기서는 PlayerController가 갱신한 ControlRotation을
	// SpringArm/Camera가 읽으므로 몸은 돌리지 않는다
	virtual void AddControllerYawInput(float Value) override {}
	virtual void AddControllerPitchInput(float Value) override {}
	virtual void BeginPlay() override;

	virtual FRotator GetMovementInputRotation() const override;

	// Lua AnimGraph를 쓸 때 Attack/SetIsHit가 켜는 Bool 변수 이름
	static constexpr const char* AttackingVariable = "IsAttacking";
	static constexpr const char* JumpingVariable = "IsJumping";
	static constexpr const char* HitVariable = "IsHit";

	void Attack();
	// 바닥에 있을 때만 뛰고 AnimGraph의 IsJumping을 켠다. 착지(Landed)에서 끈다
	virtual void Jump() override;
	virtual void Landed(const FHitResult& Hit) override;
	// 공격 Sweep 결과를 받는다. 맞은 액터가 AMyCharacter면 피격 처리한다
	void DetectedHit(const FHitResult& Hit);
	void SetIsHit();

	USpringArmComponent* SpringArmComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;
};
