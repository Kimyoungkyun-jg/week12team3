#pragma once

#include "PawnMovementComponent.h"
#include "CharacterMovementComponent.generated.h"

// UE EMovementMode의 일부
enum class ECharacterMovementMode : uint8
{
	Walking,   // 바닥 위. 수평으로만 움직이고 매 프레임 바닥에 발을 붙인다
	Falling,   // 공중. 중력을 받고, 바닥에 닿으면 Walking으로 돌아간다 (점프 포함)
};

// 입력 → 가속도 → 속도 → 이동 순으로 처리한다.
// 바닥 판정은 캡슐 중심에서 아래로 쏘는 레이로 한다 (발 = 중심 - CapsuleHalfHeight).
// Actor 충돌이 꺼져 있으면 바닥 판정과 중력을 쓰지 않는다 (예전처럼 수평 이동만).
UCLASS()
class UCharacterMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()
public:
	UCharacterMovementComponent();
	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime) override;

	// 아래 속도·가속도는 UE와 같은 cm 단위(cm/s, cm/s²)다. 계산할 때 CmToWorld로 월드 단위(m)로 바꾼다.
	// Velocity는 월드 단위(m/s) 그대로다.
	UPROPERTY(EditAnywhere)
	float MaxWalkSpeed = 600.0f;
	UPROPERTY(EditAnywhere)
	float MaxAcceleration = 2048.0f;
	// 입력이 없을 때 일정하게 줄이는 감속도
	UPROPERTY(EditAnywhere)
	float BrakingDecelerationWalking = 2048.0f;
	// 클수록 방향 전환과 정지가 빠르다 (1/s, 단위 없음)
	UPROPERTY(EditAnywhere)
	float GroundFriction = 8.0f;

	// UE: 가속(입력) 방향으로 캐릭터를 RotationRate만큼씩 돌린다.
	// Controller 입력으로 몸을 직접 돌리는 Pawn(ACharacter 기본 Yaw 입력)과 같이 켜면 서로 싸운다
	UPROPERTY(EditAnywhere)
	bool bOrientRotationToMovement = false;
	// 초당 최대 회전 각도(deg/s). 캐릭터는 수직을 유지하므로 Yaw만 쓴다
	UPROPERTY(EditAnywhere)
	FRotator RotationRate = FRotator(0.0f, 360.0f, 0.0f);

	// 점프 시작 시 위쪽 속도 (cm/s, UE 기본 420)
	UPROPERTY(EditAnywhere)
	float JumpZVelocity = 420.0f;
	// UMovementComponent::Gravity(월드 단위 m/s²)에 곱한다
	UPROPERTY(EditAnywhere)
	float GravityScale = 1.0f;
	// 공중에서 입력 가속을 얼마나 쓸지 (0~1, UE 3인칭 템플릿 0.35)
	UPROPERTY(EditAnywhere)
	float AirControl = 0.35f;
	// 걷는 중 이 높이(cm)까지는 바닥을 따라 내려가고, 더 떨어지면 Falling이 된다
	UPROPERTY(EditAnywhere)
	float MaxStepHeight = 45.0f;
	// 낙하 속도 상한 (cm/s)
	UPROPERTY(EditAnywhere)
	float TerminalVelocity = 4000.0f;

	FVector GetCurrentAcceleration() const { return Acceleration; }

	ECharacterMovementMode GetMovementMode() const { return MovementMode; }
	void SetMovementMode(ECharacterMovementMode NewMode);
	bool IsFalling() const { return MovementMode == ECharacterMovementMode::Falling; }
	bool IsMovingOnGround() const { return MovementMode == ECharacterMovementMode::Walking; }

	// 바닥에 있을 때만 JumpZVelocity로 위로 띄우고 Falling으로 바꾼다. 뛰었으면 true
	bool DoJump();

protected:
	// UE: 입력 벡터를 가속도로 바꾼 뒤 PerformMovement
	void ControlledCharacterMove(const FVector& InputVector, float DeltaTime);
	// 걷기 중에는 Z 입력을 버린다
	FVector ConstrainInputAcceleration(const FVector& InputAcceleration) const;
	// 크기 1 이하로 자른 입력 × MaxAcceleration
	FVector ScaleInputAcceleration(const FVector& InputAcceleration) const;

	void PerformMovement(float DeltaTime);
	// UE: 메시에 루트 모션이 있으면 입력 대신 그 이동량으로 Actor를 옮긴다. 적용했으면 true
	bool ApplyRootMotion(float DeltaTime);
	void CalcVelocity(float DeltaTime, float Friction, float BrakingDeceleration);

	// Walking: 수평 이동
	void PhysWalking(float DeltaTime);
	// Falling: 공중 제어 + 중력. 바닥에 닿으면 착지
	void PhysFalling(float DeltaTime);
	// 걷는 중 이동이 끝난 뒤 바닥에 발을 붙인다. 바닥이 없으면 Falling
	void MaintainFloor();
	// 캡슐 중심에서 아래로 (반높이 + ExtraDistance)만큼 바닥을 찾는다. Hit.Distance는 중심~바닥 거리
	bool FindFloor(float ExtraDistance, FHitResult& OutHit) const;
	float GetCapsuleHalfHeight() const;
	// 바닥 판정을 쓸 수 있는지 (Actor 충돌이 켜져 있고 World가 있을 때)
	bool CanUseFloor() const;
	void ProcessLanded(const FHitResult& Hit);
	// 공중에서는 루트 모션을 버린다 (착지할 때 한꺼번에 적용되지 않게)
	void DiscardRootMotion();
	// Sweep 이동. 막히면 표면을 따라 한 번 미끄러진다
	void SafeMoveWithSlide(const FVector& Delta, const FRotator& NewRotation);

	// UE: 이동이 끝난 뒤 회전을 갱신한다 (bOrientRotationToMovement)
	void PhysicsRotation(float DeltaTime);
	// 가속 방향의 회전. 가속이 없으면 현재 회전 유지
	FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation) const;

	FVector Acceleration;
	ECharacterMovementMode MovementMode = ECharacterMovementMode::Walking;
};
