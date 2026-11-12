#pragma once

#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

UCLASS()
class AMyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void InputKey(int32 Key, bool bDown) override;
};
