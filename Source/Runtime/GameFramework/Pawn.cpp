#include "EnginePCH.h"
#include "GameFramework/Pawn.h"
#include "Components/PawnMovementComponent.h"

APawn::APawn()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APawn::AddMovementInput(const FVector& WorldDirection, float ScaleValue)
{
	if (UPawnMovementComponent* MovementComponent = GetMovementComponent())
	{
		MovementComponent->AddInputVector(WorldDirection * ScaleValue);
	}
	else
	{
		Internal_AddMovementInput(WorldDirection * ScaleValue);
	}
}

UPawnMovementComponent* APawn::GetMovementComponent() const
{
	for (UActorComponent* Component : GetComponents())
	{
		if (UPawnMovementComponent* MovementComponent = Cast<UPawnMovementComponent>(Component))
			return MovementComponent;
	}
	return nullptr;
}

FVector APawn::GetPendingMovementInputVector() const
{
	return ControlInputVector;
}

FVector APawn::GetLastMovementInputVector() const
{
	return LastControlInputVector;
}

FVector APawn::ConsumeMovementInputVector()
{
	if (UPawnMovementComponent* MovementComponent = GetMovementComponent())
	{
		return MovementComponent->ConsumeInputVector();
	}
	return Internal_ConsumeMovementInputVector();
}

void APawn::Internal_AddMovementInput(const FVector& WorldAccel)
{
	ControlInputVector += WorldAccel;
}

FVector APawn::Internal_ConsumeMovementInputVector()
{
	LastControlInputVector = ControlInputVector;
	ControlInputVector = FVector::ZeroVector;
	return LastControlInputVector;
}
