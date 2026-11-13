#include "EnginePCH.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameHUD.h"
#include "GameFramework/Actor/PlayerStart.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Camera/CameraActor.h"

UClass* AGameMode::GetDefaultPawnClass() const { return ADefaultPawn::StaticClass(); }
UClass* AGameMode::GetPlayerControllerClass() const { return APlayerController::StaticClass(); }
UClass* AGameMode::GetHUDClass() const { return AGameHUD::StaticClass(); }

void AGameMode::SpawnPlayer(APawn*& OutPawn, APlayerController*& OutController)
{
	UWorld* World = GetWorld();
	if (!World) return;
	APlayerStart* PlayerStart = nullptr;
	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		if (APlayerStart* Start = Cast<APlayerStart>(Actor)) { PlayerStart = Start; break; }
	FTransform Transform = World->GetMainCamera() ? World->GetMainCamera()->GetActorTransform() : FTransform::Identity;
	if (PlayerStart)
	{
		Transform = FTransform(PlayerStart->GetActorRotation(), PlayerStart->GetActorLocation());
		PlayerStart->Destroy();
	}
	if (!OutPawn)
	{
		OutPawn = Cast<APawn>(World->SpawnActor(World->GetDefaultPawnClass(), NAME_None, &Transform));
	}
	else if (PlayerStart && OutPawn->GetRootComponent())
		OutPawn->GetRootComponent()->SetWorldLocationAndRotation(Transform.Location, Transform.Rotation, false);
	if (!OutController)
		OutController = Cast<APlayerController>(World->SpawnActor(World->GetPlayerControllerClass()));
	if (OutController)
	{
		OutController->SpawnPlayerCameraManager();
		if (OutPawn) OutController->Possess(OutPawn);
	}
}
