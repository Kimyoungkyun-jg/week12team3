#pragma once

#include "GameFramework/Actor.h"
#include "GameMode.generated.h"

class APawn;
class APlayerController;
class AGameHUD;

UCLASS()
class AGameMode : public AActor
{
	GENERATED_BODY()
public:
	virtual UClass* GetDefaultPawnClass() const;
	virtual UClass* GetPlayerControllerClass() const;
	virtual UClass* GetHUDClass() const;
	virtual bool AllowsPlayerControllerInput() const { return true; }
	void SpawnPlayer(APawn*& OutPawn, APlayerController*& OutController);
};
