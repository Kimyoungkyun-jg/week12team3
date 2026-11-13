#include "EnginePCH.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/TestCharacter.h"
#include "GameFramework/DefaultPawn.h"
#include "Camera/PlayerCameraManager.h" 
#include "Engine/World.h"               
#include "GameFramework/GameMode.h"
#include "Camera/CameraShakeBase.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include <cstring>

APlayerController::APlayerController()
	: ControlRotation(FRotator::Identitiy)
{
	PrimaryActorTick.bCanEverTick = true;
}

void APlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessPlayerInput(DeltaTime);
}

void APlayerController::ProcessPlayerInput(float DeltaTime)
{
	if (!PossessedPawn) return;
	if (GetWorld() && (GetWorld()->IsPaused() ||
		(GetWorld()->GetGameMode() && !GetWorld()->GetGameMode()->AllowsPlayerControllerInput())))
	{
		MouseAxisX = MouseAxisY = 0.0f;
		return;
	}
	const float Forward = static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::W)]) -
		static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::S)]);
	const float Right = static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::D)]) -
		static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::A)]);
	if (CursorMode == ECursorMode::Locked || bKeyDown[static_cast<int32>(EKeyCode::RButton)])
	{
		PossessedPawn->AddControllerYawInput(MouseAxisX * LookSensitivity);
		PossessedPawn->AddControllerPitchInput(MouseAxisY * LookSensitivity);

		// ControlRotation 업데이트
		ControlRotation.Yaw += MouseAxisX * LookSensitivity;
		ControlRotation.Pitch = FMath::Clamp(
			ControlRotation.Pitch + MouseAxisY * LookSensitivity,
			-89.9f, 89.9f);
	}
	MouseAxisX = MouseAxisY = 0.0f;
	const FQuat Rotation = PossessedPawn->GetActorRotation().Quaternion();
	if (Forward != 0.0f) PossessedPawn->AddMovementInput(Rotation.GetForwardVector(), Forward);
	if (Right != 0.0f) PossessedPawn->AddMovementInput(Rotation.GetRightVector(), Right);
	if (ADefaultPawn* DefaultPawn = Cast<ADefaultPawn>(PossessedPawn))
	{
		const float Up = static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::E)]) -
			static_cast<float>(bKeyDown[static_cast<int32>(EKeyCode::Q)]);
		if (Up != 0.0f) DefaultPawn->AddMovementInput(FVector(0.0f, 0.0f, 1.0f), Up);
	}
}

void APlayerController::Possess(APawn* InPawn)
{
	if (PossessedPawn == InPawn) return;
	UnPossess();
	PossessedPawn = InPawn;
	if (PossessedPawn)
	{
		PossessedPawn->PossessedBy(this);
		ControlRotation = PossessedPawn->GetActorRotation();
		// 기본 폰과 테스트 캐릭터는 UE PIE처럼 시작하자마자 마우스로 시점을 돌린다.
		if (Cast<ADefaultPawn>(PossessedPawn) || Cast<ATestCharacter>(PossessedPawn))
			SetCursorMode(ECursorMode::Locked);
	}
	if (PlayerCameraManager) PlayerCameraManager->SetViewTarget(PossessedPawn);
}

void APlayerController::UnPossess()
{
	if (ATestCharacter* Character = Cast<ATestCharacter>(PossessedPawn)) Character->StopAim();
	if (PossessedPawn) PossessedPawn->UnPossessed();
	PossessedPawn = nullptr;
	if (PlayerCameraManager) PlayerCameraManager->SetViewTarget(nullptr);
}

void APlayerController::InputKey(int32 Key, bool bDown)
{
	if (Key < 0 || Key >= 256) return;
	const bool bJustPressed = bDown && !bKeyDown[Key];
	bKeyDown[Key] = bDown;
	ACharacter* Character = Cast<ACharacter>(PossessedPawn);
	if (Key == static_cast<int32>(EKeyCode::RButton))
	{
		if (ATestCharacter* TestCharacter = Cast<ATestCharacter>(PossessedPawn))
		{
			if (!bDown) TestCharacter->StopAim();
			else if (bJustPressed && GetWorld() && !GetWorld()->IsPaused() &&
				(!GetWorld()->GetGameMode() || GetWorld()->GetGameMode()->AllowsPlayerControllerInput()))
				TestCharacter->StartAim();
		}
	}
	if (Key == static_cast<int32>(EKeyCode::Space) && Character)
	{
		if (!bDown) Character->StopJumping();
		else if (bJustPressed && GetWorld() && !GetWorld()->IsPaused() &&
			(!GetWorld()->GetGameMode() || GetWorld()->GetGameMode()->AllowsPlayerControllerInput()))
			Character->Jump();
	}
	if (Key == static_cast<int32>(EKeyCode::F) && bJustPressed && Character &&
		GetWorld() && !GetWorld()->IsPaused() &&
		(!GetWorld()->GetGameMode() || GetWorld()->GetGameMode()->AllowsPlayerControllerInput()))
		Character->Attack();
	if (Key == static_cast<int32>(EKeyCode::LButton) && bJustPressed &&
		Cast<ATestCharacter>(PossessedPawn) && GetWorld() && !GetWorld()->IsPaused() &&
		(!GetWorld()->GetGameMode() || GetWorld()->GetGameMode()->AllowsPlayerControllerInput()))
		Character->Attack();
	if (!bJustPressed || !PlayerCameraManager) return;

	if (Key == static_cast<int32>(EKeyCode::F6))
	{
		UClass* ShakeClass = UCameraShakeBase::StaticClass();
		auto* Settings = static_cast<UCameraShakeBase*>(
			ShakeClass->GetDefaultObject());

		UCurveFloat* TestFOVCurve = nullptr;


		if (!TestFOVCurve)
		{
			TestFOVCurve = NewObject<UCurveFloat>(this);
			TestFOVCurve->AddKey(0.0f, 0.0f);
			TestFOVCurve->AddKey(0.15f, 1.0f);
			TestFOVCurve->AddKey(1.0f, 0.0f);
		}

		if (!Settings) return;

		// 새 Shake를 생성할 때 복사할 테스트용 기본 설정.
		Settings->Duration = 1.0f;
		Settings->BlendInTime = 0.05f;
		Settings->BlendOutTime = 0.3f;

		Settings->RotationPitch.Amplitude = 2.0f;
		Settings->RotationPitch.Frequency = 10.0f;

		Settings->RotationYaw.Amplitude = 1.0f;
		Settings->RotationYaw.Frequency = 13.0f;

		// 이전 테스트를 끊고 새로 실행한다.
		PlayerCameraManager->StopAllCameraShakes(true);
		PlayerCameraManager->PlayFOVPunch(TestFOVCurve, 1.0f, 15.0f);
		PlayerCameraManager->StartCameraShake(ShakeClass, 1.0f);

	}
	else if (Key == static_cast<int32>(EKeyCode::F7))
	{
		PlayerCameraManager->StopAllCameraShakes(true);
	}
}

void APlayerController::InputAxis(EGameInputAxis Axis, float Delta)
{
	if (Axis == EGameInputAxis::MouseX) MouseAxisX += Delta;
	else if (Axis == EGameInputAxis::MouseY) MouseAxisY += Delta;
}

void APlayerController::SetCursorMode(ECursorMode Mode)
{
	CursorMode = Mode;
	MouseAxisX = MouseAxisY = 0.0f;
	if (GEngine)
		GEngine->SetGameMouseCapture(GetWorld(), Mode == ECursorMode::Locked);
}

void APlayerController::EndPlay()
{
	if (CursorMode == ECursorMode::Locked)
		SetCursorMode(ECursorMode::Free);
	Super::EndPlay();
}

void APlayerController::ClearInputState()
{
	if (PossessedPawn)
	{
		if (ATestCharacter* Character = Cast<ATestCharacter>(PossessedPawn)) Character->StopAim();
		if (ACharacter* Character = Cast<ACharacter>(PossessedPawn)) Character->StopJumping();
		PossessedPawn->ConsumeMovementInputVector();
	}
	std::memset(bKeyDown, 0, sizeof(bKeyDown));
	MouseAxisX = MouseAxisY = 0.0f;
}
void APlayerController::SpawnPlayerCameraManager()
{
	// 중복생성방지
	if (PlayerCameraManager || !GetWorld()) return;

	PlayerCameraManager = GetWorld()->SpawnActor<APlayerCameraManager>();
	PlayerCameraManager->InitializeFor(this);
}
void APlayerController::UpdateCameraManager(float DeltaTime)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->UpdateCamera(DeltaTime);
	}
}
