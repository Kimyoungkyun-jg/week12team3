#pragma once

#include "MovementComponent.h"
#include <Math/Vector.h>
#include "PawnMovementComponent.generated.h"

class APawn;

// Pawn과 입력을 잇는 계층. 이동 자체는 하지 않고, 파생 클래스가 Tick에서 ConsumeInputVector로 꺼내 쓴다.
UCLASS()
class UPawnMovementComponent : public UMovementComponent
{
	GENERATED_BODY()

public:
	virtual void InitializeComponent() override;
	APawn* GetPawnOwner() const { return PawnOwner; }

	// 입력 누적값은 PawnOwner가 들고 있다. 여기서는 전달만 한다
	void AddInputVector(const FVector& WorldVector);
	FVector GetPendingInputVector() const;
	FVector GetLastInputVector() const;
	FVector ConsumeInputVector();

protected:
	APawn* PawnOwner = nullptr;
};
