#pragma once

#include "GameFramework/Actor.h"
#include "Pawn.generated.h"

class APlayerController;
class UPawnMovementComponent;

UCLASS()
class APawn : public AActor
{
	GENERATED_BODY()
public:
	APawn();

	// UE 규칙: MovementComponent가 있으면 그쪽(AddInputVector)으로, 없으면 Pawn의 ControlInputVector에 누적한다.
	// 누적값은 MovementComponent(또는 Pawn 자신)가 Tick에서 Consume해서 쓴다.
	virtual void AddMovementInput(const FVector& WorldDirection, float ScaleValue = 1.0f);
	virtual void AddControllerYawInput(float Value) {}
	virtual void AddControllerPitchInput(float Value) {}

	// 첫 번째 UPawnMovementComponent. 없으면 nullptr
	virtual UPawnMovementComponent* GetMovementComponent() const;

	FVector GetPendingMovementInputVector() const;
	FVector GetLastMovementInputVector() const;
	FVector ConsumeMovementInputVector();

	// UPawnMovementComponent 전용. 실제 누적값은 항상 Pawn이 들고 있다
	void Internal_AddMovementInput(const FVector& WorldAccel);
	FVector Internal_GetPendingMovementInputVector() const { return ControlInputVector; }
	FVector Internal_GetLastMovementInputVector() const { return LastControlInputVector; }
	FVector Internal_ConsumeMovementInputVector();

	void PossessedBy(APlayerController* NewController) { Controller = NewController; }
	void UnPossessed() { Controller = nullptr; }
	APlayerController* GetController() const { return Controller; }

	virtual FVector GetVelocity() const { return FVector::ZeroVector; }

	// PlayerController가 WASD를 월드 방향으로 바꿀 때 쓰는 기준 회전. 기본은 액터 회전
	virtual FRotator GetMovementInputRotation() const { return GetActorRotation(); }

protected:
	// 이번 프레임에 누적 중인 입력
	FVector ControlInputVector;
	// 직전에 Consume된 입력
	FVector LastControlInputVector;
private:
	APlayerController* Controller = nullptr;
};
