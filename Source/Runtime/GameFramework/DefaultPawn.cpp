#include "EnginePCH.h"
#include "GameFramework/DefaultPawn.h"
#include "Camera/CameraComponent.h"

ADefaultPawn::ADefaultPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::PostPhysics;
	SetActorEnableCollision(false);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	SetRootComponent(CameraComponent);
	CameraComponent->SetExternalInputManaged(true);
}

void ADefaultPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const FVector Input = ConsumeMovementInputVector().GetClampedToMaxSize(1.0f);
	if (GetRootComponent() && Input.SizeSquared() > SMALL_NUMBER)
		GetRootComponent()->SetRelativeLocation(GetActorLocation() + Input * (MoveSpeed * DeltaTime));
}

void ADefaultPawn::AddMovementInput(const FVector& WorldDirection, float ScaleValue)
{
	Super::AddMovementInput(WorldDirection, ScaleValue);
}

void ADefaultPawn::AddControllerYawInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += Value;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void ADefaultPawn::AddControllerPitchInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
	GetRootComponent()->SetRelativeRotation(Rotation);
}
