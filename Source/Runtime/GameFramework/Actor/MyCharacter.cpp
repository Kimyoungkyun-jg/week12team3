#include "EnginePCH.h"
#include "GameFramework/Actor/MyCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Animation/MyAnimInstance.h"
#include "Components/CharacterMovementComponent.h"
#include "Collision/HitResult.h"
#include "Animation/AnimGraphInstance.h"

AMyCharacter::AMyCharacter()
{
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(CapsuleComponent);
	// UE 3인칭 템플릿: 팔이 ControlRotation(Yaw + Pitch)을 그대로 따라 캐릭터 주위를 구면으로 공전한다
	// (템플릿 400cm / 캡슐 반높이 96cm 비율 ≈ 4, 이 엔진 캡슐 반높이 1)
	SpringArmComponent->TargetArmLength = 4.0f;
	SpringArmComponent->TargetOffset = FVector(0.0f, 0.0f, 0.0f);
	SpringArmComponent->SocketOffset = FVector(0.0f, 0.0f, 0.0f);
	SpringArmComponent->bUsePawnControlRotation = true;
	SpringArmComponent->bInheritPitch = true;
	SpringArmComponent->bInheritYaw = true;
	SpringArmComponent->bInheritRoll = true;
	SpringArmComponent->bEnableCameraLag = false;
	SpringArmComponent->bEnableCameraRotationLag = false;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->SetExternalInputManaged(true);                  // 카메라 자체 자유 비행 입력 끔
	CameraComponent->bUsePawnControlRotation = false;                // 템플릿: 카메라는 팔 끝 회전을 그대로 쓴다

	// UE 3인칭 템플릿 값: 몸은 마우스가 아니라 이동(가속) 방향을 향해 초당 500도까지 돈다
	CharacterMovementComponent->bOrientRotationToMovement = true;
	CharacterMovementComponent->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
}

void AMyCharacter::BeginPlay()
{
	// Super::BeginPlay 안에서 Lua BeginPlay가 돈다. 스크립트가 AnimGraph를 만들었으면 그대로 쓴다.
	Super::BeginPlay();

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		UMyAnimInstance* Instance = NewObject<UMyAnimInstance>(SkeletalMeshComponent);
		SkeletalMeshComponent->SetAnimInstance(Instance);
	}
}

void AMyCharacter::Attack()
{
	UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
	if (UMyAnimInstance* MyInstance = Cast<UMyAnimInstance>(AnimInstance))
	{
		MyInstance->RequestAttack();
	}
	// Lua AnimGraph: 공격 중이면 무시, false 복귀는 그래프(Attack 상태 OnExit)가 맡는다
	else if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(AnimInstance))
	{
		if (!Graph->GetBool(AttackingVariable))
			Graph->SetBool(AttackingVariable, true);
	}
}

void AMyCharacter::Jump()
{
	// 공중이면 무시 (이중 점프 없음)
	if (!CanJump())
		return;

	Super::Jump();

	// C++ MyAnimInstance에는 점프 상태가 없다. AnimGraph(AnimBlueprint/Lua)만 변수를 켠다
	if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
		Graph->SetBool(JumpingVariable, true);
}

void AMyCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
		Graph->SetBool(JumpingVariable, false);
}

void AMyCharacter::DetectedHit(const FHitResult& Hit)
{
	AActor* HitActor = Hit.HitComponent ? Hit.HitComponent->GetOwner() : nullptr;
	if (AMyCharacter* HitCharacter = Cast<AMyCharacter>(HitActor))
	{
		HTR_LOG(Info, "[Attack] {} -> {}", GetName(), HitCharacter->GetName());
		HitCharacter->SetIsHit();
	}
}

void AMyCharacter::SetIsHit()
{
	UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
	if (UMyAnimInstance* MyInstance = Cast<UMyAnimInstance>(AnimInstance))
	{
		MyInstance->SetIsHit(true);
	}
	else if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(AnimInstance))
	{
		Graph->SetBool(HitVariable, true);
	}
}

FRotator AMyCharacter::GetMovementInputRotation() const
{
	// UE 3인칭 템플릿처럼 컨트롤 회전의 Yaw만 써서 수평 이동 방향을 만든다
	if (const APlayerController* PC = GetController())
		return FRotator(0.0f, PC->ControlRotation.Yaw, 0.0f);
	return Super::GetMovementInputRotation();
}
