#pragma once

#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"
#include "Input/MouseCapture.h"
#include "PlayerController.generated.h"

class APawn;
class APlayerCameraManager;
class UCurveFloat;
class AGameHUD;

UCLASS()
class APlayerController : public AActor
{
	GENERATED_BODY()
public:
	APlayerController();
	void Tick(float DeltaTime) override;
	void ProcessPlayerInput(float DeltaTime);
	void Possess(APawn* InPawn);
	void UnPossess();
	APawn* GetPawn() const { return PossessedPawn; }
	AGameHUD* GetHUD() const { return HUD; }
	void SetHUD(AGameHUD* InHUD) { HUD = InHUD; }
	void InputKey(int32 Key, bool bDown);
	void InputAxis(EGameInputAxis Axis, float Delta);
	void ClearInputState();

	// Locked면 커서를 게임 화면에 가두고 우클릭 없이 시점을 돌린다. Free면 우클릭한 동안만 돌린다.
	// ADefaultPawn을 빙의하면 Locked가 된다 (TEAM5). 에디터의 Shift+F1은 Free로 바꾼다.
	void SetCursorMode(ECursorMode Mode);
	ECursorMode GetCursorMode() const { return CursorMode; }
	void EndPlay() override;

	FRotator ControlRotation;
	//PCM을 만들고 주인을 알려준다.
	void SpawnPlayerCameraManager();
	//Wolrd Tick으로부터 전달
	void UpdateCameraManager(float DeltaTime);
	APlayerCameraManager* GetCameraManager() const { return PlayerCameraManager; }
private:

	APlayerCameraManager* PlayerCameraManager = nullptr;
	APawn* PossessedPawn = nullptr;
	AGameHUD* HUD = nullptr;
	bool bKeyDown[256]{};
	float MouseAxisX = 0.0f;
	float MouseAxisY = 0.0f;
	float LookSensitivity = 0.1f;
	ECursorMode CursorMode = ECursorMode::Free;
};
