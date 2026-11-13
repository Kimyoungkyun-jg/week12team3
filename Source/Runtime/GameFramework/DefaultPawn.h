#pragma once

#include "GameFramework/Pawn.h"
#include "DefaultPawn.generated.h"

class UCameraComponent;

UCLASS()
class ADefaultPawn : public APawn
{
	GENERATED_BODY()
public:
	ADefaultPawn();
	void Tick(float DeltaTime) override;
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
private:
	UCameraComponent* CameraComponent = nullptr;
	float MoveSpeed = 20.0f;
};
