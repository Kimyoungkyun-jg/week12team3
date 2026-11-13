#include "EnginePCH.h"
#include "GameFramework/Pawn.h"

APawn::APawn()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APawn::AddMovementInput(const FVector& WorldDirection, float ScaleValue)
{
	if (std::isfinite(ScaleValue) && std::isfinite(WorldDirection.X) &&
		std::isfinite(WorldDirection.Y) && std::isfinite(WorldDirection.Z))
		PendingMovementInput += WorldDirection.GetSafeNormal() * ScaleValue;
}

FVector APawn::ConsumeMovementInputVector()
{
	const FVector Input = PendingMovementInput;
	PendingMovementInput = FVector::ZeroVector;
	return Input;
}
