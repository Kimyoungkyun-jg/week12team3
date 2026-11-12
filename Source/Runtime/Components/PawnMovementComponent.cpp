#include "EnginePCH.h"
#include "PawnMovementComponent.h"
#include "GameFramework/Pawn.h"

void UPawnMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	PawnOwner = Cast<APawn>(GetOwner());
}

void UPawnMovementComponent::AddInputVector(const FVector& WorldVector)
{
	if (PawnOwner) PawnOwner->Internal_AddMovementInput(WorldVector);
}

FVector UPawnMovementComponent::GetPendingInputVector() const
{
	return PawnOwner ? PawnOwner->Internal_GetPendingMovementInputVector() : FVector::ZeroVector;
}

FVector UPawnMovementComponent::GetLastInputVector() const
{
	return PawnOwner ? PawnOwner->Internal_GetLastMovementInputVector() : FVector::ZeroVector;
}

FVector UPawnMovementComponent::ConsumeInputVector()
{
	return PawnOwner ? PawnOwner->Internal_ConsumeMovementInputVector() : FVector::ZeroVector;
}
