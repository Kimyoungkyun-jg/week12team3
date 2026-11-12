#include "EnginePCH.h"
#include "GameFramework/DefaultPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/AssetManager.h"

ADefaultPawn::ADefaultPawn()
{
	SetActorEnableCollision(false);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	SetRootComponent(StaticMeshComponent);
	StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Models/Hitori/Hitori.obj"));
	CameraComponent->SetupAttachment(StaticMeshComponent);
	CameraComponent->SetRelativeLocation({-5.0f, 0.0f, 8.0f});
	CameraComponent->SetRelativeRotation({45.0f, 0.0f, 0.0f});
	CameraComponent->SetExternalInputManaged(true);
}

void ADefaultPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const FVector Input = ConsumeMovementInputVector();
	if (GetRootComponent() && Input.SizeSquared() > 0.0f)
		GetRootComponent()->SetRelativeLocation(GetActorLocation() + Input * (MoveSpeed * DeltaTime));
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
