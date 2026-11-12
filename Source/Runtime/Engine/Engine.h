#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

#include "Containers/IndirectArray.h"

#include "Engine/World.h"
#include "EngineTypes.h"

#include "Asset/AssetManager.h"
#include "Engine.generated.h"

class UGameInstance;
class FGameViewportClient;
class FAudioDevice;

struct FWorldContext
{
	EWorldType WorldType;
	FName ContextHandle;
	UGameInstance* OwningGameInstance = nullptr;
	int32 PIEInstance = 0;
	TSharedPtr<FGameViewportClient> GameViewport;

	FString TravelURL;

	void SetCurrentWorld(UWorld* World);

	inline UWorld* World() const
	{
		return ThisCurrentWorld;
	}

	FWorldContext()
		: WorldType(EWorldType::None)
		, ContextHandle(NAME_None)
		, ThisCurrentWorld(nullptr)
	{
	}

private:
	UWorld* ThisCurrentWorld = nullptr;
};

class FEngineLoop;

// 엔진 모드별 실행 설정. UE에서는 ini가 맡는 부분이다.
// Width·Height가 0이면 주 모니터 해상도를 쓴다.
struct FEngineConfig
{
	const wchar_t* Title = L"Hitori";
	uint32 Width = 0;
	uint32 Height = 0;
	bool bBorderless = true;
	uint32 SyncInterval = 0;   // 0 = VSync 끔 (FPS 측정용)
	bool bCreateDepthBuffer = true;   // 백버퍼에 3D를 직접 그리지 않는 엔진(에디터)은 끈다.
	bool bExitOnEscape = true;        // 테두리 없는 창에는 닫기 버튼이 없다.
	const char* SplashImage = nullptr;
	const char* SkyPanorama = "Assets/SkySphere/IceEscapeSnowPanorama-v2.png";
};

// 모든 엔진 모드의 베이스. 플랫폼·루프·Present는 FEngineLoop가 맡고,
// 서브클래스(Game, Editor)는 Tick에서 한 프레임의 갱신과 렌더를 채운다.
UCLASS()
class UEngine : public UObject
{
	GENERATED_BODY()
	friend class FEngineLoop;
public:
	// 창을 만들기 전에, GEngine이 아니라 클래스 기본 객체(CDO)에서 호출된다.
	virtual FEngineConfig GetConfig() const { return {}; }

	// Device·Window·Swapchain·AssetManager가 준비된 뒤 호출된다.
	virtual bool Init();
	// Init이 끝난 뒤 게임을 시작한다. UE의 UEngine::Start와 같은 자리다.
	// 게임 엔진은 여기서 시작 맵을 연다. 실패하면 false를 돌려주고 실행을 끝낸다.
	virtual bool Start() { return true; }
	// 한 프레임의 갱신과 모든 렌더 패스를 기록한다. Present는 FEngineLoop가 호출 직후에 한다.
	virtual void Tick(float DeltaTime) = 0;
	virtual void OnResize(uint32 Width, uint32 Height) {}
	// UObject 일괄 정리와 GPU 자원 해제 전에 호출된다.
	virtual void PreExit() {};

	//UWorld* GetWorld() const { return World; }

	FWorldContext& CreateNewWorldContext(EWorldType WorldType);
	FWorldContext* GetWorldContextFromWorld(const UWorld* InWorld);
	FWorldContext* GetWorldContextFromType(EWorldType WorldType);
	FWorldContext* GetWorldContextFromPIEInstance(int32 PIEInstance);
	UWorld* GetEditorWorld() const;
	virtual UWorld* CreatePIEWorldByDuplication(FWorldContext& Context, UWorld* SourceWorld);
	// PIE 컨텍스트는 게임 인스턴스와 복제 월드까지 함께 정리한다.
	void DestroyWorldContext(UWorld* InWorld);

	// 레벨 전환. UE의 SetClientTravel → TickWorldTravel → LoadMap 흐름을 따른다.
	// 월드 Tick 도중 월드를 지우지 않도록 요청만 기록하고 다음 Tick 시작에 연다.
	void SetClientTravel(UWorld* InWorld, const FString& MapPath);
	void TickWorldTravel(FWorldContext& Context);
	// 컨텍스트와 게임 인스턴스는 두고 월드만 씬 파일로 바꾼다. 읽기에 실패하면 지금 월드를 유지한다.
	bool LoadMap(FWorldContext& Context, const FString& MapPath);
	// 월드 하나를 정리하고 지운다. DestroyWorldContext와 LoadMap이 같이 쓴다.
	void DestroyWorld(UWorld* InWorld);

	FAudioDevice* GetMainAudioDevice() const { return MainAudioDevice; }
	void InitializeAudioDevice();     // UE UEngine::InitializeAudioDeviceManager
	void ShutdownAudioDevice();

	// 창, 스왑체인, 렌더러를 가진 엔진 루프. UE에서는 전역 GEngineLoop로 어디서나 접근한다.
	FEngineLoop& GetEngineLoop() const { return *EngineLoop; }

protected:
	// 맵 교체 직전·직후 알림. 에디터는 PIE 월드 포인터와 패널을 여기서 갱신한다.
	virtual void PreLoadMap(FWorldContext& Context) {}
	virtual void PostLoadMap(FWorldContext& Context) {}

	FAudioDevice* MainAudioDevice = nullptr;

	TIndirectArray<FWorldContext> WorldContextlist;
	//UWorld* World = nullptr;
	int32 NextWorldContextHandle = 0;
private:
	FEngineLoop* EngineLoop = nullptr;
};

// FEngineLoop::Init에서 설정되고 Exit에서 해제된다.
extern UEngine* GEngine;
