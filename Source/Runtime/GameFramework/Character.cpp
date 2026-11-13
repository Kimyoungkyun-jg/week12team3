#include "EnginePCH.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimGraphInstance.h"

ACharacter::ACharacter()
{
	SetActorEnableCollision(true);
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	SetRootComponent(CapsuleComponent);
	CapsuleComponent->bBlockComponent = true;
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMeshComponent->SetupAttachment(CapsuleComponent);
	SkeletalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -1.0f));
	SkeletalMeshComponent->PrimaryComponentTick.TickGroup = ETickingGroup::PrePhysics;
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(CapsuleComponent);
	SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.7f));
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->SetRelativeLocation(FVector::ZeroVector);
	CameraComponent->SetExternalInputManaged(true);
	CharacterMovement = CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovement");
}

void ACharacter::BeginPlay()
{
	const FRotator SpawnRotation = GetActorRotation();
	CapsuleComponent->SetRelativeRotation(FRotator(0.0f, SpawnRotation.Yaw, 0.0f));
	SpringArmComponent->SetRelativeRotation(FRotator(SpawnRotation.Pitch, 0.0f, 0.0f));
	CameraComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	SpringArmComponent->ResetCameraLag();
	Super::BeginPlay();
}

void ACharacter::AddMovementInput(const FVector& Direction, float ScaleValue)
{
	Super::AddMovementInput(FVector(Direction.X, Direction.Y, 0.0f), ScaleValue);
}

void ACharacter::AddControllerYawInput(float Value)
{
	if (std::isfinite(Value))
	{
		FRotator Rotation = GetActorRotation();
		Rotation.Yaw += Value;
		CapsuleComponent->SetRelativeRotation(Rotation);
	}
}

void ACharacter::AddControllerPitchInput(float Value)
{
	if (std::isfinite(Value))
	{
		FRotator Rotation = SpringArmComponent->GetRelativeRotation();
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
		SpringArmComponent->SetRelativeRotation(Rotation);
	}
}

void ACharacter::Jump() { if (CharacterMovement) CharacterMovement->RequestJump(); }
void ACharacter::StopJumping() { if (CharacterMovement) CharacterMovement->StopJumping(); }

void ACharacter::LaunchCharacter(const FVector& Direction, float Strength)
{
	if (!CharacterMovement || !std::isfinite(Strength) || Strength <= 0.0f ||
		!std::isfinite(Direction.X) || !std::isfinite(Direction.Y) || !std::isfinite(Direction.Z)) return;
	const FVector Normal = Direction.GetSafeNormal();
	if (Normal.SizeSquared() <= SMALL_NUMBER) return;
	CharacterMovement->Launch(Normal * Strength);
}

float ACharacter::PlayAnimMontage(const FString& MontagePath, float InPlayRate, const FString& StartSection)
{
	UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(SkeletalMeshComponent ? SkeletalMeshComponent->GetAnimInstance() : nullptr);
	return AnimGraph ? AnimGraph->PlayMontage(MontagePath, InPlayRate, StartSection) : 0.0f;
}
