#include "EnginePCH.h"
#include "CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Collision/HitResult.h"
#include "Engine/World.h"
#include "UObject/Casts.h"
#include <algorithm>
#include <cmath>

UCharacterMovementComponent::UCharacterMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Velocity = FVector::ZeroVector;
}

void UCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UCharacterMovementComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!PawnOwner || !UpdatedComponent || DeltaTime <= 0.0f) return;

	const FVector InputVector = ConsumeInputVector();
	ControlledCharacterMove(InputVector, DeltaTime);
}

void UCharacterMovementComponent::ControlledCharacterMove(const FVector& InputVector, float DeltaTime)
{
	Acceleration = ScaleInputAcceleration(ConstrainInputAcceleration(InputVector));
	PerformMovement(DeltaTime);
}

FVector UCharacterMovementComponent::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
	return FVector(InputAcceleration.X, InputAcceleration.Y, 0.0f);
}

FVector UCharacterMovementComponent::ScaleInputAcceleration(const FVector& InputAcceleration) const
{
	return InputAcceleration.GetClampedToMaxSize(1.0f) * (MaxAcceleration * CmToWorld);
}

void UCharacterMovementComponent::PerformMovement(float DeltaTime)
{
	if (MovementMode == ECharacterMovementMode::Falling)
	{
		DiscardRootMotion();
		PhysFalling(DeltaTime);
		PhysicsRotation(DeltaTime);
		UpdateComponentVelocity();
		return;
	}

	// 루트 모션은 회전까지 직접 정하므로 PhysicsRotation을 하지 않는다
	const bool bRootMotion = ApplyRootMotion(DeltaTime);
	if (!bRootMotion)
		PhysWalking(DeltaTime);

	MaintainFloor();

	if (!bRootMotion)
		PhysicsRotation(DeltaTime);
	UpdateComponentVelocity();
}

void UCharacterMovementComponent::PhysWalking(float DeltaTime)
{
	CalcVelocity(DeltaTime, GroundFriction, BrakingDecelerationWalking * CmToWorld);
	Velocity.Z = 0.0f;

	const FVector Delta = Velocity * DeltaTime;
	if (Delta.SizeSquared() > SMALL_NUMBER)
		SafeMoveWithSlide(Delta, UpdatedComponent->GetWorldRotation());
}

void UCharacterMovementComponent::PhysFalling(float DeltaTime)
{
	// 수평: 입력은 AirControl 비율만 쓰고, 마찰·제동은 없다 (UE FallingLateralFriction/BrakingDecelerationFalling = 0)
	const float VerticalSpeed = Velocity.Z;
	Velocity.Z = 0.0f;
	const FVector InputAcceleration = Acceleration;
	Acceleration = InputAcceleration * AirControl;
	CalcVelocity(DeltaTime, 0.0f, 0.0f);
	Acceleration = InputAcceleration;   // 회전(bOrientRotationToMovement)은 원래 입력 방향을 쓴다

	const FRotator Rotation = UpdatedComponent->GetWorldRotation();
	const FVector Horizontal = Velocity * DeltaTime;
	if (Horizontal.SizeSquared() > SMALL_NUMBER)
		SafeMoveWithSlide(Horizontal, Rotation);   // 벽에 막히면 미끄러진다 (Velocity.Z는 아직 0)

	// 수직: 중력 적분 (사다리꼴)
	const float GravityZ = -Gravity * GravityScale;   // 월드 단위 m/s²
	const float NewVerticalSpeed = std::max(VerticalSpeed + GravityZ * DeltaTime, -TerminalVelocity * CmToWorld);
	const float DeltaZ = 0.5f * (VerticalSpeed + NewVerticalSpeed) * DeltaTime;
	Velocity.Z = NewVerticalSpeed;

	if (DeltaZ > 0.0f)
	{
		// 올라가는 중: 천장에 막히면 SafeMoveWithSlide가 위쪽 속도를 없앤다
		SafeMoveWithSlide(FVector(0.0f, 0.0f, DeltaZ), Rotation);
		return;
	}

	// 내려가는 중: 이번 프레임에 발이 바닥을 지나치면 바닥 위에 세우고 착지
	FHitResult Floor;
	const float FallDistance = -DeltaZ;
	if (FindFloor(FallDistance + KINDA_SMALL_NUMBER, Floor))
	{
		const float Gap = Floor.Distance - GetCapsuleHalfHeight();   // 발과 바닥 사이 (음수면 이미 묻힘)
		if (Gap <= FallDistance)
		{
			MoveUpdatedComponent(FVector(0.0f, 0.0f, -Gap), Rotation, false, nullptr);
			ProcessLanded(Floor);
			return;
		}
	}

	MoveUpdatedComponent(FVector(0.0f, 0.0f, DeltaZ), Rotation, false, nullptr);
}

void UCharacterMovementComponent::MaintainFloor()
{
	if (!CanUseFloor()) return;

	FHitResult Floor;
	if (FindFloor(MaxStepHeight * CmToWorld, Floor))
	{
		// 경사·계단을 따라 발을 바닥에 붙인다. 묻혀 있으면 위로 올린다
		const float Gap = Floor.Distance - GetCapsuleHalfHeight();
		if (std::abs(Gap) > KINDA_SMALL_NUMBER)
			MoveUpdatedComponent(FVector(0.0f, 0.0f, -Gap), UpdatedComponent->GetWorldRotation(), false, nullptr);
		return;
	}

	// 발밑에 바닥이 없다 (절벽 끝): 떨어진다
	if (GravityScale > 0.0f)
	{
		Velocity.Z = 0.0f;
		SetMovementMode(ECharacterMovementMode::Falling);
	}
}

bool UCharacterMovementComponent::FindFloor(float ExtraDistance, FHitResult& OutHit) const
{
	if (!CanUseFloor()) return false;

	// 반지름 0 Sweep = 아래 방향 레이. 메시는 AABB 윗면, 스킨드 메시는 삼각형에 닿는다
	const FVector Start = UpdatedComponent->GetWorldLocation();
	const float TraceLength = GetCapsuleHalfHeight() + std::max(ExtraDistance, 0.0f);
	return GetOwner()->GetWorld()->SweepSingle(FRay(Start, FVector(0.0f, 0.0f, -1.0f)), TraceLength, 0.0f, OutHit, GetOwner());
}

float UCharacterMovementComponent::GetCapsuleHalfHeight() const
{
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent))
		return Capsule->GetScaledCapsuleHalfHeight();
	return UpdatedComponent ? UpdatedComponent->GetCollisionRadius() : 0.0f;
}

bool UCharacterMovementComponent::CanUseFloor() const
{
	const AActor* Owner = GetOwner();
	return UpdatedComponent && Owner && Owner->GetActorEnableCollision() && Owner->GetWorld();
}

void UCharacterMovementComponent::SetMovementMode(ECharacterMovementMode NewMode)
{
	MovementMode = NewMode;
}

bool UCharacterMovementComponent::DoJump()
{
	if (!IsMovingOnGround() || !CanUseFloor() || JumpZVelocity <= 0.0f)
		return false;

	Velocity.Z = JumpZVelocity * CmToWorld;
	SetMovementMode(ECharacterMovementMode::Falling);
	return true;
}

void UCharacterMovementComponent::ProcessLanded(const FHitResult& Hit)
{
	Velocity.Z = 0.0f;
	SetMovementMode(ECharacterMovementMode::Walking);

	if (ACharacter* Character = Cast<ACharacter>(PawnOwner))
		Character->Landed(Hit);
}

void UCharacterMovementComponent::DiscardRootMotion()
{
	const ACharacter* Character = Cast<ACharacter>(PawnOwner);
	USkeletalMeshComponent* Mesh = Character ? Character->SkeletalMeshComponent : nullptr;
	if (Mesh && Mesh->HasPendingRootMotion())
		Mesh->ConsumeRootMotion();
}

bool UCharacterMovementComponent::ApplyRootMotion(float DeltaTime)
{
	const ACharacter* Character = Cast<ACharacter>(PawnOwner);
	USkeletalMeshComponent* Mesh = Character ? Character->SkeletalMeshComponent : nullptr;
	if (!Mesh || !Mesh->HasPendingRootMotion())
	{
		return false;
	}

	// 월드 공간 delta. 걷기 중이므로 UE처럼 수직 이동은 버린다
	const FTransform Delta = Mesh->ConsumeRootMotion();
	const FVector Move(Delta.Location.X, Delta.Location.Y, 0.0f);
	Velocity = Move * (1.0f / DeltaTime);   // 애니메이션 등에서 속도를 읽을 수 있게 남긴다

	// Root Motion의 전체 회전을 현재 회전 앞에 곱해 누적한다 (Pitch, Yaw, Roll).
	const FQuat NewQuat = (Delta.Rotation * FQuat(UpdatedComponent->GetWorldRotation())).Normalized();
	const FRotator NewRotation = NewQuat.ToFRotator();

	if (Move.SizeSquared() > SMALL_NUMBER)
		SafeMoveWithSlide(Move, NewRotation);   // Sweep: 벽에 막힌다
	else
		MoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, nullptr);

	UpdateComponentVelocity();
	return true;
}

namespace
{
	// UE FMath::FixedTurn: Current에서 Desired로 최단 방향으로 최대 DeltaRate도까지만 돈다
	float FixedTurn(float Current, float Desired, float DeltaRate)
	{
		if (DeltaRate <= 0.0f) return Current;
		const float Delta = FMath::UnwindDegrees(Desired - Current);
		if (std::abs(Delta) <= DeltaRate) return Desired;
		return FMath::NormalizeAxis(Current + (Delta > 0.0f ? DeltaRate : -DeltaRate));
	}
}

void UCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!bOrientRotationToMovement || !UpdatedComponent) return;

	const FRotator CurrentRotation = UpdatedComponent->GetWorldRotation();
	const FRotator DesiredRotation = ComputeOrientToMovementRotation(CurrentRotation);

	// 캐릭터는 수직을 유지한다 (UE ShouldRemainVertical): Pitch/Roll은 0, Yaw만 RotationRate로 돌린다
	FRotator NewRotation(0.0f, FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, RotationRate.Yaw * DeltaTime), 0.0f);
	if (std::abs(FMath::UnwindDegrees(NewRotation.Yaw - CurrentRotation.Yaw)) <= KINDA_SMALL_NUMBER &&
		std::abs(CurrentRotation.Pitch) <= KINDA_SMALL_NUMBER
		&& std::abs(CurrentRotation.Roll) <= KINDA_SMALL_NUMBER)
		return;

	// 위치는 그대로, 회전만 바꾼다
	MoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, nullptr);
}

FRotator UCharacterMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation) const
{
	if (Acceleration.SizeSquared() < KINDA_SMALL_NUMBER * KINDA_SMALL_NUMBER)
		return CurrentRotation;

	FRotator Result;
	Result.LookDirection(Acceleration);
	return Result;
}

void UCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, float BrakingDeceleration)
{
	if (Acceleration.SizeSquared() > SMALL_NUMBER)
	{
		// UE와 같이 가속 방향이 아닌 속도 성분을 Friction만큼 깎아 방향 전환을 빠르게 한다
		const float Speed = Velocity.Size();
		const FVector AccelDir = Acceleration.GetSafeNormal();
		Velocity = Velocity - (Velocity - AccelDir * Speed) * std::min(DeltaTime * Friction, 1.0f);
		Velocity = Velocity + Acceleration * DeltaTime;
		Velocity = Velocity.GetClampedToMaxSize(MaxWalkSpeed * CmToWorld);
		return;
	}

	// 입력이 없으면 Friction 비율 감속 + 일정 감속. 0을 넘어 반대로 가지 않게 멈춘다
	const float Speed = Velocity.Size();
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		Velocity = FVector::ZeroVector;
		return;
	}
	const float NewSpeed = std::max(Speed - (Speed * Friction + BrakingDeceleration) * DeltaTime, 0.0f);
	Velocity = Velocity * (NewSpeed / Speed);
}

void UCharacterMovementComponent::SafeMoveWithSlide(const FVector& Delta, const FRotator& NewRotation)
{
	const FVector Start = UpdatedComponent->GetWorldLocation();
	FHitResult Hit(1.0f);
	if (MoveUpdatedComponent(Delta, NewRotation, true, &Hit)) return;

	// 시작부터 겹쳐 있던 표면(예: 캡슐이 묻힌 바닥)이 이동 방향을 막지 않으면 Sweep 없이 원래 Delta만큼 간다.
	// MoveComponent가 막힌 위치에 법선 방향으로 살짝 밀어 두므로 그만큼을 빼서 Start + Delta에 맞춘다.
	if (Hit.Time <= KINDA_SMALL_NUMBER && Hit.ImpactNormal.Dot(Delta) >= 0.0f)
	{
		MoveUpdatedComponent(Start + Delta - UpdatedComponent->GetWorldLocation(), NewRotation, false, nullptr);
		return;
	}

	HandleImpact(Hit, 0.0f, Delta);

	// 벽으로 들어가는 속도 성분을 없애고 남은 이동을 표면을 따라 한 번 더 시도한다
	const FVector Normal = Hit.ImpactNormal;
	const float VDotN = Velocity.Dot(Normal);
	if (VDotN < 0.0f) Velocity = Velocity - Normal * VDotN;

	const FVector Slide = ComputeSlideVector(Delta, 1.0f - Hit.Time, Normal, Hit);
	if (Slide.SizeSquared() > SMALL_NUMBER)
		MoveUpdatedComponent(Slide, NewRotation, true, &Hit);
}
