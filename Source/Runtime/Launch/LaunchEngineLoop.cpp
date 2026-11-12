#include "EnginePCH.h"
#include "Launch/LaunchEngineLoop.h"

#include <shellapi.h>

#include "Launch/EntryPoint.h"
#include "Core/Misc/Paths.h"
#include "Core/CoreGlobals.h"

#include "Asset/AssetManager.h"
#include "Asset/AssetLoadProfiler.h"
#include "Core/EngineTimer.h"
#include "Core/Window.h"
#include "Core/Windows/WindowsPlatformTime.h"
#include "Input/InputSystem.h"
#include "UObject/Casts.h"
#include "UObject/Class.h"
#include "Render/RenderCommand.h"
#include "Render/RenderDevice.h"
#include "Render/RenderResourceManager.h"
#include "Render/Renderer.h"
#include "Render/Swapchain.h"
#include "Render/Texture2D.h"
#include "Core/SplashScreen.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/Async/TaskPool.h"
#include "Tasks/Tasks.h"

#include "Render/RealtimeGPUProfiler.h"

#include "UObject/UObjectGlobals.h"

namespace
{
	// CPU Frame이 Present보다 크면 CPU 병목, Present가 크면 CPU가 GPU를 기다리는 GPU 병목이다.
	DECLARE_CYCLE_STAT("Present", STAT_Present);
	DECLARE_CYCLE_STAT("CPU Frame (Engine Tick)", STAT_EngineTick);

	// 이 간격마다 FPS 평균을 갱신한다.
	constexpr double FrameStatsWindowSeconds = 0.5;

	// UE처럼 첫 인자로 프로젝트 파일을 받는다. "Path/To/Game.hproject" 또는 -project=Path/To/Game.hproject
	std::filesystem::path FindProjectInCommandLine()
	{
		int32 Count = 0;
		LPWSTR* Args = CommandLineToArgvW(GetCommandLineW(), &Count);
		if (!Args) return {};

		std::filesystem::path Result;
		for (int32 i = 1; i < Count && Result.empty(); ++i)
		{
			std::wstring Arg = Args[i];
			constexpr std::wstring_view Prefix = L"-project=";
			if (Arg.size() > Prefix.size() && _wcsnicmp(Arg.c_str(), Prefix.data(), Prefix.size()) == 0)
				Arg = Arg.substr(Prefix.size());
			const std::filesystem::path Candidate(Arg);
			if (_wcsicmp(Candidate.extension().c_str(), FPaths::ProjectExtension) == 0)
				Result = Candidate;
		}
		LocalFree(Args);
		return Result;
	}

	// 엔진 폴더와 프로젝트를 정하고 작업 디렉터리를 프로젝트 폴더로 옮긴다.
	bool InitProjectPaths(HINSTANCE hInstance)
	{
		if (!FPaths::InitEngineDir())
		{
			MessageBoxW(nullptr, L"Cannot find the engine folder (Resources/Shader).", L"Hitori", MB_OK | MB_ICONERROR);
			return false;
		}

		const std::filesystem::path ProjectFile = ResolveProjectFile(hInstance, FindProjectInCommandLine());
		if (ProjectFile.empty()) return false;

		FString Error;
		if (!FPaths::SetProject(ProjectFile, &Error))
		{
			MessageBoxA(nullptr, ("Cannot open project:\n" + Error).c_str(), "Hitori", MB_OK | MB_ICONERROR);
			return false;
		}
		return true;
	}
}

// 엔진 생성자에서도 사이클 카운터를 쓸 수 있게 가장 먼저 초기화한다.
FEngineLoop::FEngineLoop()
{
	FPlatformTime::InitTiming();
}

// TUniquePtr 멤버의 완전한 타입이 필요하므로 cpp에서 정의한다.
FEngineLoop::~FEngineLoop() = default;

bool FEngineLoop::PreInit(HINSTANCE hInstance, UClass* EngineClass)
{
	// PreInit의 첫 일은 어느 프로젝트를 열지 정하는 것이다. 이후의 모든 상대 경로가 여기에 달려 있다.
	// 프로젝트 브라우저에서 고르는 시간이 시작 시간 측정에 섞이지 않도록 타이머보다 앞에 둔다.
	if (!InitProjectPaths(hInstance))
	{
		return false;
	}

	// 시작 시간 전체(Phase/Total)는 PreInit에서 재기 시작해 Init 끝에서 기록한다.
	StartupBeginCycles = FPlatformTime::GetCycles64();
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	if (!EngineClass || !EngineClass->IsChildOf(UEngine::StaticClass()))
	{
		return false;
	}
	PendingEngineClass = EngineClass;

	// GEngine은 Init에서 만든다. 창 크기 같은 실행 설정은 클래스 기본 객체(CDO)에서 읽는다.
	// UE에서는 이 값들을 ini에서 읽는다.
	const UEngine* DefaultEngine = EngineClass->GetDefaultObject<UEngine>();
	if (!DefaultEngine)
	{
		return false;
	}
	Config = DefaultEngine->GetConfig();

	if (Config.SplashImage)
	{
		FSplashScreen::Show(hInstance, Config.SplashImage);
	}

	FSplashScreen::SetText(L"Initializing Renderer...");
	FSplashScreen::SetProgress(0.1f);

	FLoadStageTimer RendererTimer{ "Phase/Device+Renderer+Window" };
	// 0이면 주 모니터 해상도
	const uint32 Width = Config.Width != 0 ? Config.Width : static_cast<uint32>(GetSystemMetrics(SM_CXSCREEN));
	const uint32 Height = Config.Height != 0 ? Config.Height : static_cast<uint32>(GetSystemMetrics(SM_CYSCREEN));

	RenderDevice = MakeUnique<FRenderDevice>();
	RenderCommand::Init(RenderDevice.get());

	Renderer = MakeUnique<FRenderer>();
	Renderer->Init();

	MainWindow = MakeUnique<FWindow>();
	if (!MainWindow->Create(hInstance, Width, Height, Config.Title, Config.bBorderless))
	{
		FSplashScreen::Hide();
		return false;
	}

	Swapchain = MakeUnique<FSwapchain>(RenderDevice.get(), MainWindow.get());
	if (Config.bCreateDepthBuffer)
	{
		CreateDepthBuffer(MainWindow->GetWidth(), MainWindow->GetHeight());
	}

	RendererTimer.Stop();

	FSplashScreen::SetText(L"Loading Assets...");
	{
		FLoadStageTimer Timer{ "Phase/RenderResourceManager::Init" };
		FRenderResourceManager::Init();
	}

	constexpr float AssetStart = 0.1f;
	constexpr float AssetEnd = 0.8f;
	{
		FLoadStageTimer Timer{ "Phase/AssetManager::Init" };
		UAssetManager::Init([](int32 Loaded, int32 Total, const FString& Path)
			{
				FSplashScreen::SetText("Loading " + Path);
				FSplashScreen::SetProgress(AssetStart + (AssetEnd - AssetStart) * Loaded / Total);
			});
	}

	{
		FLoadStageTimer Timer{ "Phase/InitScenePasses" };
		Renderer->InitScenePasses(Config.SkyPanorama ? Config.SkyPanorama : "");
	}

	return true;
}

// PreInit이 준비한 Device·Window·Swapchain·에셋 위에 GEngine을 만들고 초기화한 뒤 루프를 돌 준비를 끝낸다.
// UE의 FEngineLoop::Init과 같은 자리다.
bool FEngineLoop::Init()
{
	FSplashScreen::SetText(L"Initializing Engine...");
	FSplashScreen::SetProgress(0.8f);

	// 워커 스레드는 에셋 로드가 끝난 뒤, 엔진이 생기기 전에 켠다. 끄는 것도 이 클래스의 Exit가 한다.
	Tasks::FTaskScheduler::Get().Initialize();

	GEngine = NewObject<UEngine>(nullptr, PendingEngineClass);
	if (!GEngine)
	{
		FSplashScreen::Hide();
		return false;
	}
	GEngine->EngineLoop = this;

	FLoadStageTimer EngineInitTimer{ "Phase/GEngine::Init" };
	if (!GEngine->Init())
	{
		FSplashScreen::Hide();
		return false;
	}
	bEngineInitialized = true;

	// 초기화가 끝난 엔진에게 게임을 시작하라고 알린다. 게임 엔진은 여기서 시작 맵을 연다.
	if (!GEngine->Start())
	{
		FSplashScreen::Hide();
		return false;
	}
	EngineInitTimer.Stop();
	FAssetLoadProfiler::AddStage("Phase/Total", FPlatformTime::ToMilliseconds64(FPlatformTime::GetCycles64() - StartupBeginCycles));
	FAssetLoadProfiler::WriteReport();

	FSplashScreen::SetProgress(1.0f);
	FSplashScreen::Hide();

	// 초기화 동안 숨겨 두었던 메인 창을 스플래시가 닫힌 뒤에 띄운다.
	MainWindow->Show();

	EngineTimer::Init();
	PrevFrameCycles = FPlatformTime::GetCycles64();

	const uint32 Cores = std::max(1u, std::thread::hardware_concurrency());
	FTaskPool::Get().Init(std::min(Cores - 1, 15u));
	return true;
}

void FEngineLoop::Tick()
{
	EngineTimer::Tick();
	FInputSystem::UpdateInputStates();

	// 창이 닫히면 WM_QUIT이 온다. 종료는 여기서 직접 하지 않고 요청만 남긴다.
	bool bWindowAlive = true;
	MainWindow->ProcessMessage(bWindowAlive);
	if (!bWindowAlive)
	{
		RequestEngineExit("main window closed");
	}

	if (Config.bExitOnEscape && FInputSystem::IsKeyPressed(EKeyCode::Escape))
	{
		RequestEngineExit("escape key");
	}

	// 종료가 요청됐으면 이번 프레임은 그리지 않는다. 루프는 GuardedMain이 끝낸다.
	if (IsEngineExitRequested())
	{
		return;
	}

	HandleResize();

	// 최소화 중에는 그릴 대상이 없다.
	if (MainWindow->GetWidth() == 0 || MainWindow->GetHeight() == 0)
	{
		return;
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_EngineTick);
		FRealtimeGPUProfiler::BeginFrame();          
		GEngine->Tick(EngineTimer::GetDeltaTime());  
		FRealtimeGPUProfiler::EndFrame();           
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_Present);
		Swapchain->SwapBuffers(Config.SyncInterval);
	}

	// EngineTimer는 DeltaTime을 0.1초로 자르므로 표시용 시간은 사이클로 따로 잰다.
	const uint64 CurrentCycles = FPlatformTime::GetCycles64();
	UpdateFrameStats(CurrentCycles - PrevFrameCycles);
	PrevFrameCycles = CurrentCycles;
}

// UObject(에셋 포함)의 GPU 자원은 Device가 살아 있을 때 해제한다.
void FEngineLoop::Exit()
{
	FRealtimeGPUProfiler::Shutdown();
	FTaskPool::Get().Shutdown();
	Tasks::FTaskScheduler::Get().Shutdown();

	if (GEngine)
	{
		// PreExit는 Init이 만든 것을 정리하므로 Init이 성공했을 때만 부른다.
		if (bEngineInitialized)
		{
			GEngine->PreExit();
		}
		GEngine->ShutdownAudioDevice();   // 믹서 스레드를 먼저 멈춘 뒤 USoundWave를 지운다
	}

	// UAssetManager도 UObject이므로 일괄 삭제 전에 종료한다.
	UAssetManager::Shutdown();
	FRenderResourceManager::Shutdown();

	DestroyAllObjectsExceptEngine();
	delete GEngine;
	GEngine = nullptr;

	DepthBuffer.reset();
	Swapchain.reset();
	Renderer.reset();

	if (RenderDevice)
	{
		RenderDevice->Shutdown();
	}
}

void FEngineLoop::BeginBackbufferPass()
{
	RenderCommand::BeginRenderPass(
		Swapchain->GetBackbuffer(),
		DepthBuffer.get(),
		MainWindow ? MainWindow->GetWidth() : 0,
		MainWindow ? MainWindow->GetHeight() : 0);

	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
	RenderCommand::SetPrimitiveTopology(EPrimitiveTopology::TriangleList);
}

void FEngineLoop::EndBackbufferPass()
{
	RenderCommand::EndRenderPass();
}

uint32 FEngineLoop::GetViewportWidth() const
{
	return MainWindow ? MainWindow->GetWidth() : 0;
}

uint32 FEngineLoop::GetViewportHeight() const
{
	return MainWindow ? MainWindow->GetHeight() : 0;
}

void FEngineLoop::HandleResize()
{
	if (!MainWindow->CheckResized())
	{
		return;
	}

	const uint32 Width = MainWindow->GetWidth();
	const uint32 Height = MainWindow->GetHeight();
	if (Width == 0 || Height == 0)
	{
		return;
	}

	Swapchain->Resize(Width, Height);
	if (Config.bCreateDepthBuffer)
	{
		CreateDepthBuffer(Width, Height);
	}
	GEngine->OnResize(Width, Height);
}

void FEngineLoop::CreateDepthBuffer(uint32 Width, uint32 Height)
{
	FTextureDesc Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.Format = ETextureFormat::D24_UNorm_S8_UInt;   // 깊이 24bit + 스텐실 8bit
	Desc.BindFlags = ETextureBindFlags::DepthStencil;  // FTexture2D가 DSV를 만들어 준다

	DepthBuffer = RenderCommand::CreateTexture2D(Desc);
}

// 소멸자가 자신을 GUObjectArray에서 빼고 마지막 원소를 그 자리로 옮기므로 매번 끝에서 하나씩 지운다.
// 액터가 컴포넌트를 함께 지워도 배열 크기를 다시 읽으므로 안전하다.
void FEngineLoop::DestroyAllObjectsExceptEngine()
{
	while (true)
	{
		int32 Index = GUObjectArray.Num() - 1;
		if (Index >= 0 && GUObjectArray[Index] == GEngine)
		{
			--Index;
		}
		if (Index < 0)
		{
			break;
		}
		delete GUObjectArray[Index];
	}
}

void FEngineLoop::UpdateFrameStats(uint64 FrameCycles)
{
	FrameStats.LastFrameMs = FPlatformTime::ToMilliseconds64(FrameCycles);

	StatsWindowCycles += FrameCycles;
	++StatsWindowFrames;

	const double WindowSeconds = FPlatformTime::ToSeconds64(StatsWindowCycles);
	if (WindowSeconds < FrameStatsWindowSeconds)
	{
		return;
	}

	FrameStats.AverageFrameMs = WindowSeconds * 1000.0 / StatsWindowFrames;
	FrameStats.AverageFPS = StatsWindowFrames / WindowSeconds;
	StatsWindowCycles = 0;
	StatsWindowFrames = 0;
}
