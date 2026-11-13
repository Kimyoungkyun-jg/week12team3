#include "EnginePCH.h"
#include "GameFramework/GameHUD.h"
#include "UI/UserWidget.h"

void AGameHUD::BeginPlay()
{
	Super::BeginPlay();
	if (!WidgetAssetPath.empty()) ShowWidget(WidgetAssetPath);
}

bool AGameHUD::ShowWidget(const FString& Path)
{
	HideWidget();
	Widget = CreateWidget(GetWorld(), Path);
	if (!Widget) return false;
	Widget->OnDestroyed = [this] { Widget = nullptr; };
	Widget->AddToViewport();
	if (Widget->IsInViewport()) return true;
	HideWidget();
	return false;
}

void AGameHUD::HideWidget()
{
	if (Widget) Widget->Destroy();
}

void AGameHUD::EndPlay()
{
	HideWidget();
	OwningPlayerController = nullptr;
	Super::EndPlay();
}
