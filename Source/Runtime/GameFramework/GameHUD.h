#pragma once

#include "GameFramework/Actor.h"
#include "GameHUD.generated.h"

class APlayerController;
class UUserWidget;

UCLASS()
class AGameHUD : public AActor
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void EndPlay() override;
	APlayerController* GetOwningPlayerController() const { return OwningPlayerController; }
	UUserWidget* GetWidget() const { return Widget; }
	void SetOwningPlayerController(APlayerController* Controller) { OwningPlayerController = Controller; }
	bool ShowWidget(const FString& Path);
	void HideWidget();

	UPROPERTY(EditAnywhere)
	FString WidgetAssetPath;

private:
	APlayerController* OwningPlayerController = nullptr;
	UUserWidget* Widget = nullptr;
};
