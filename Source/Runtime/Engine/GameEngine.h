#pragma once

#include "Engine.h"
#include "Engine/GameSettings.h"

#include "UI/UIRenderer.h"
#include "UI/UIDrawList.h"
#include "GameEngine.generated.h"

class FGameViewportClient;

UCLASS()
class UGameEngine : public UEngine
{
	GENERATED_BODY()
public:
	FEngineConfig GetConfig() const override;

	// 설정을 읽고 UI 렌더러를 만든다. 월드는 아직 없다.
	bool Init() override;
	// 시작 맵을 열고 BeginPlay를 호출한다.
	bool Start() override;
	void Tick(float DeltaTime) override;
	void PreExit() override;

	// 첫 맵: 컨텍스트와 게임 인스턴스를 만들고 씬 파일로 Game 월드를 연다.
	// 이후 전환은 UGameplayStatics::OpenLevel → UEngine::LoadMap으로 간다.
	bool OpenStartupMap(const FString& MapPath);
private:
	void DispatchInput(FGameViewportClient& Viewport, float DeltaTime);
	// 게임 뷰포트를 다시 그린다. UE의 UGameEngine::RedrawViewports에 해당한다.
	void RedrawViewports(FGameViewportClient& Viewport);

	// GetConfig(클래스 기본 객체에서 const로 불린다)와 Init이 각각 읽는다.
	mutable FGameSettings Settings;

	TUniquePtr<FUIRenderer> UIRenderer;
};