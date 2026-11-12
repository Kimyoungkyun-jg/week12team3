#include "EnginePCH.h"

#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/HitoriEd/Editor.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"

#include "Render/GeometryGenerator.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"

#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor/LightActorBase.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/TextRenderComponent.h"

#include "Asset/AssetManager.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "UObject/UObjectIterator.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

#include "Script/LuaScriptManager.h"
#include <sol/sol.hpp>

#include "Components/ShapeComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "Physics/Collision.h"

#include "Audio/AudioDevice.h"

namespace
{
	DECLARE_CYCLE_STAT("Viewport Update", STAT_ViewportUpdate);
	DECLARE_CYCLE_STAT("World Tick", STAT_WorldTick);
	DECLARE_CYCLE_STAT("Editor Tick", STAT_EditorTick);
	DECLARE_CYCLE_STAT("Capture World", STAT_CaptureWorld);
	DECLARE_CYCLE_STAT("Build Render Queue", STAT_BuildRenderQueue);
	DECLARE_CYCLE_STAT("ImGui", STAT_ImGui);
}

#include "Core/SplashScreen.h"
#include "Core/Misc/Paths.h"
#include "Editor/EditorUI/EditorTitleBar.h"

// 에디터 엔진을 가리키는 전역 포인터. UE의 GEditor와 같다. Init에서 채우고 PreExit에서 비운다.
UEditorEngine* GEditor = nullptr;

FEngineConfig UEditorEngine::GetConfig() const
{
	FEngineConfig Desc;
	// 창 제목에 연 프로젝트 이름을 붙인다. Title은 포인터만 넘기므로 문자열을 static으로 둔다.
	static std::wstring Title;
	Title = L"Hitori Engine";
	if (FPaths::HasProject())
		Title += L" - " + FPaths::ProjectFilePath().stem().wstring();
	Desc.Title = Title.c_str();
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.bBorderless = false;
	Desc.SyncInterval = 0;
	// View는 각자 깊이 버퍼를 쓰고 백버퍼에는 ImGui만 그린다.
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false;
	// 파일이 없으면 검은 배경에 상태 텍스트만 표시된다.
	Desc.SplashImage = "Engine/Splash/Splash.png";
	return Desc;
}

// 렌더 자원·월드·에디터와 MultipleViewports 연결을 초기화한다.
// Device·Window·Swapchain·AssetManager는 FEngineLoop가 먼저 만들어 둔다.
bool UEditorEngine::Init()
{
	if (!Super::Init())
		return false;

	GEditor = this;

	// FEngineLoop가 소유한 창·스왑체인·렌더러를 받아 둔다.
	MainWindow = GetEngineLoop().GetMainWindow();
	MainWindowSC = GetEngineLoop().GetSwapchain();
	Renderer = GetEngineLoop().GetRenderer();

	// 순서에 의미가 있다. 패널은 ImGui와 렌더러를, 명령은 패널을 쓴다.
	// InitEditor만 UE에 있는 이름이다. 나머지 다섯은 UE에서 레벨 에디터 모듈과 Slate가 맡는 일이라
	// 대응하는 UE 함수가 없는, 이 엔진 고유의 단계다.
	InitEditor();
	InitLevelEditorTab();
	InitImGui();
	InitEditorRenderers();
	InitPanels();
	InitCommands();

	return true;
}

// 에디터 월드와 그 월드 컨텍스트를 만든다. UE의 UEditorEngine::InitEditor가 하는 일 중 이 부분에 해당한다.
void UEditorEngine::InitEditor()
{
	FWorldContext& InitialWorldContext = CreateNewWorldContext(EWorldType::Editor);
	InitialWorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Editor, true));
	EditorWorld = InitialWorldContext.World();
}

// 에디터 탭 관리자와 레벨 에디터 탭을 만들고 메뉴 동작과 출력 로그를 연결한다.
void UEditorEngine::InitLevelEditorTab()
{
	EditorManager = MakeUnique<FEditorManager>(*this);
	LevelEditorTab = &EditorManager->GetLevelEditorTab();

	LevelEditorTab->OnNewScene.AddDynamic(this, &UEditorEngine::CreateNewScene);
	LevelEditorTab->OnOpenScene.AddDynamic(this, &UEditorEngine::OpenScene);
	LevelEditorTab->OnSaveScene.AddDynamic(this, &UEditorEngine::SaveCurrentScene);
	LevelEditorTab->OnSaveSceneAs.AddDynamic(this, &UEditorEngine::SaveSceneAs);
	LevelEditorTab->OnImportFbx.Add([this](EFbxImportMode Mode) { FEditorFileUtils::ImportFbx(Mode); });
	LevelEditorTab->OnPackageGame.Add([this]() { Packager.Start("Release"); });
	LevelEditorTab->SetIsPackagingQuery([this]() { return Packager.IsRunning(); });

	OutputLogPanel = LevelEditorTab->AddPanel<FOutputLogPanel>();
	FLog::AddSink(OutputLogPanel);
	HTR_LOG(Info, "Editor Initialize...");

	// 테스트 코드 
#ifdef ENGINE_DEBUG
	FCollision::RunSelfTest();
#endif
}

// ImGui를 초기화하고 메인 메뉴 바를 창의 제목 표시줄로 쓴다.
void UEditorEngine::InitImGui()
{
	FRenderDevice* RenderDevice = GetEngineLoop().GetRenderDevice();

	HTR_LOG(Info, "Initialize ImGui...");
	ImGuiRenderer = MakeUnique<FImGuiRenderer>();
	if (!ImGuiRenderer->Init(MainWindow->GetHandle(), RenderDevice->GetDevice(), RenderDevice->GetContext()))
	{
		HTR_LOG(Error, "Failed To Initialize ImGui!");
	}
	HTR_LOG(Info, "Initialize ImGui Success!");

	// 기본 제목 표시줄을 없애고 ImGui 메인 메뉴 바를 제목 표시줄로 쓴다.
	EditorTitleBar::Install(*MainWindow);
	// 엔진 콘텐츠에 로고 이미지가 있으면 제목 표시줄 왼쪽 위 칸에 그린다. 없으면 빈 칸으로 둔다.
	{
		const FString LogoPath = "Engine/Icons/Logo.png";
		std::error_code Ec;
		if (std::filesystem::exists(FPaths::FromUtf8(FPaths::ResolveContentPath(LogoPath)), Ec))
		{
			if (UTexture2D* Logo = UAssetManager::GetAssetByPath<UTexture2D>(LogoPath))
				EditorTitleBar::SetLogo((ImTextureID)Logo->GetResource()->GetSRV());
		}
	}
}

// 선택 상태(기즈모, 외곽선 대상)와 UI 렌더러를 만든다. 뷰포트에 그리는 도구는 뷰포트 클라이언트가 갖는다.
void UEditorEngine::InitEditorRenderers()
{
	Gizmo = MakeUnique<FGizmo>();

	Outline = MakeUnique<FOutline>();

	UIRenderer = MakeUnique<FUIRenderer>();
	UIRenderer->Init();

}

// 레벨 에디터 탭의 패널을 만들고 패널끼리, 패널과 월드·기즈모를 연결한다.
void UEditorEngine::InitPanels()
{
	DetailsPanel = LevelEditorTab->AddPanel<FDetailsPanel>();
	DetailsPanel->SetImGuiRenderer(ImGuiRenderer.get());
	DetailsPanel->SetShadowPreviewQuery([this](const ULightComponent* Light, FTexture2DArray*& Atlas,
		TArray<FLightShadowRenderData>& Shadows)
		{
			Atlas = nullptr;
			Shadows.Reset();
			if (!Light || !ViewportsPanel) return false;
			const int32 Index = ViewportsPanel->GetActiveViewIndex();
			FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
			if (!Client || !Client->IsActive() || Client->GetRect().Width <= 0 || Client->GetRect().Height <= 0)
				return false;
			if (PlayWorld && !IsPIEInWindow() && PlayMode == EPlayModeType::InViewport && Index == PIEStartEditorViewIndex)
			{
				FWorldContext* Context = GetPIEWorldContext();
				if (!Context || !Context->GameViewport || Light->GetWorld() != Context->World()) return false;
				return Context->GameViewport->GetRenderData().GetShadowPreview(Light, Atlas, Shadows);
			}
			if (Light->GetWorld() != (PlayWorld ? PlayWorld : EditorWorld)) return false;
			return Client->GetRenderData().GetShadowPreview(Light, Atlas, Shadows);
		});


	EditorControlsPanel = LevelEditorTab->AddPanel<FEditorControlsPanel>();
	ViewportsPanel = LevelEditorTab->AddPanel<FViewportsPanel>();
	ContentDrawerPanel = LevelEditorTab->AddPanel<FContentDrawerPanel>();

	SettingsPanel = LevelEditorTab->AddPanel<FSettingsPanel>();
	SequencerPanel = LevelEditorTab->AddPanel<FSequencerPanel>();
	OutputLogPanel->SetEditorSettings(&SettingsPanel->GetMutableSettings());

	// 위젯 에셋은 다른 에셋처럼 EditorManager가 여는 탭에서 편집한다.
	// 새로 만들 때는 디스크에 바로 만들어 콘텐츠 드로어에 곧장 보이도록 한 뒤 연다.
	ContentDrawerPanel->SetCreateWidgetCallback([this](const FString& Path)
	{
		if (FUIEditor::CreateDefaultAsset(Path))
		{
			EditorManager->OpenEditor(Path);
		}
	});
	ContentDrawerPanel->SetOpenWidgetCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });

	// StaticMesh / SkeletalMesh Viewer 열기
	ContentDrawerPanel->SetOpenMeshCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });

	ContentDrawerPanel->SetBlendSpaceCreatedCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });
    ContentDrawerPanel->SetAnimBlueprintCreatedCallback([this](const FString& Path) { EditorManager->OpenEditor(Path); });

	// 화면 나눔 비율 설정 가져오기
	ViewportSplitRatio = {
		SettingsPanel->GetSettings().MultipleViewportsHorizontal,
		SettingsPanel->GetSettings().MultipleViewportsVertical
	};
	// SingleView에 사용할 인덱스 설정
	SingleViewportIndex = SettingsPanel->GetSettings().MultipleViewportsSingleViewIndex;
	// 뷰포트 레이아웃 설정
	ViewportLayoutMode = SettingsPanel->GetSettings().bMultipleViewportsSingle
		? ELayoutMode::Single
		: ELayoutMode::QuadSplit;
	EditorWorld->GetMainCamera()->GetCameraComponent()->SetExternalInputManaged(true);

	OutlinerPanel = LevelEditorTab->AddPanel<FOutlinerPanel>();
	OutlinerPanel->SetWorld(GetActiveWorld());
	OutlinerPanel->SetSelectionCallback(
		[this](USceneComponent* Root)
		{
			Gizmo->SetTarget(Root, true);
			DetailsPanel->SetTarget(Root ? Root->GetOwner() : nullptr);

			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root);
			if (!Primitive && Root && Root->GetOwner())
			{
				for (UActorComponent* C : Root->GetOwner()->GetComponents())
					if ((Primitive = Cast<UPrimitiveComponent>(C))) break;
			}
			Outline->SetTarget(Primitive);
		});

	DetailsPanel->SetSelectionCallback(
		[this](UActorComponent* Component)
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				AActor* Owner = SceneComponent->GetOwner();
				if (Owner && SceneComponent == Owner->GetRootComponent())
				{
					Gizmo->SetTarget(SceneComponent, true);
				}
				else
				{
					Gizmo->SetTarget(SceneComponent);
				}
			}
			else
			{
				Gizmo->SetTarget(nullptr);
			}
		}
	);

	OutlinerPanel->SetDeleteActorCallback(
		[this](AActor* Actor)
		{
			DeleteActor(Actor);
		}
	);
	DetailsPanel->SetDeleteComponentCallback(
		[this](UActorComponent* Component)
		{
			DeleteComponent(Component);
		}
	);

	DetailsPanel->SetWorld(GetActiveWorld());

	EditorControlsPanel->SetWorld(EditorWorld);
	EditorControlsPanel->SetIsPlayingQuery([this]() { return IsPlaying(); });
	EditorControlsPanel->SetGizmo(Gizmo.get());
	EditorControlsPanel->SetViewportsPanel(ViewportsPanel);

	SettingsPanel->SetWorld(EditorWorld);
	SettingsPanel->SetViewportsPanel(ViewportsPanel);
	SequencerPanel->SetWorld(EditorWorld, true);
	// 에디터 월드 미리보기: Camera Cut 카메라에 0번 뷰포트를 태운다 (PIE는 ULevelSequencePlayer가 게임 뷰포트를 바꾼다)
	SequencerPanel->SetPilotCameraHandler([this](UCameraComponent* Camera)
	{
		if (!Camera || !ViewportsPanel) return;
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(0))
		{
			Client->SetViewLocation(Camera->GetWorldLocation());
			Client->SetViewRotation(Camera->GetWorldRotation());
			Client->SetViewFOV(Camera->GetFieldOfView());
		}
	});
	ViewportsPanel->SetLayoutMode(ViewportLayoutMode, SingleViewportIndex);

	for (int32 i = 0; i < 4; ++i)
	{
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(i))
		{
			Client->SetWorld(EditorWorld);
			AllViewportClients.Add(Client);
		}
	}
	SettingsPanel->ApplyViewportSettings();
}

// 저장·열기·Play 같은 에디터 명령과 단축키를 정의하고 툴바에 넘긴다.
void UEditorEngine::InitCommands()
{
	const ImVec4 White = { 1, 1, 1, 1 };
	const ImVec4 Green = { 0.40f, 0.80f, 0.30f, 1.0f };

	Save = { "Save",   "Save Current Scene",UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Save.png"),  White, { EKeyCode::S, true } };

	Save.CanExecute = [this]()->bool {return !PlayWorld; };
	Save.Execute = [this]() { SaveCurrentScene(); };

	// UE: Save As = Ctrl+Alt+S
	SaveAs = { "Save As", "Save Current Scene As", UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Save.png"),  White, { EKeyCode::S, true, true } };

	SaveAs.CanExecute = [this]()->bool {return !PlayWorld; };
	SaveAs.Execute = [this]() { SaveSceneAs(); };

	// UE: New Level = Ctrl+N, Open Level = Ctrl+O
	NewScene = { "New Scene", "Create a new scene", nullptr, White, { EKeyCode::N, true } };

	NewScene.CanExecute = [this]()->bool {return !PlayWorld; };
	NewScene.Execute = [this]() { CreateNewScene(); };

	OpenSceneCommand = { "Open Scene", "Load an existing scene", nullptr, White, { EKeyCode::O, true } };

	OpenSceneCommand.CanExecute = [this]()->bool {return !PlayWorld; };
	OpenSceneCommand.Execute = [this]() { OpenScene(); };

	Play = { "Play",   "Play World",        UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Play.png"),  Green, { EKeyCode::P, false, true } };

	Play.CanExecute = [this]()->bool {return !PlayWorld; };
	Play.Execute = [this]() { RequestPlaySession(); };

	Pause = { "Pause",  "Pause",			UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Pause.png"), White, { EKeyCode::Pause } };

	Pause.CanExecute = [this]()->bool {return !bIsPaused && PlayWorld; };
	Pause.Execute = [this]() { bIsPaused = true; if (PlayWorld) PlayWorld->SetPaused(true); };


	Resume = { "Resume", "Resume",          UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Play.png"),  White, { EKeyCode::Pause } };

	Resume.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	Resume.Execute = [this]() { bIsPaused = false; if (PlayWorld) PlayWorld->SetPaused(false); };

	StepFrame = { "Step", "Step One Frame", UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Step.png"),  White, {} };

	StepFrame.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	StepFrame.Execute = [this]() { bStepRequested = true; };


	Stop = { "Stop",   "Stop",				UAssetManager::GetAssetByPath<UTexture2D>("Engine/Icons/Toolbar/Stop.png"),  White, { EKeyCode::Escape } };

	Stop.CanExecute = [this]()->bool {return PlayWorld; };
	Stop.Execute = [this]() { RequestEndPlayMap(); };

	FLevelEditorToolbar& LevelEditorToolbar = *LevelEditorTab->GetToolbar();
	LevelEditorToolbar.SetCommands({ Play, Pause, Resume, StepFrame, Stop });
	LevelEditorToolbar.SetPlayModeBinding(
		[this]() { return PlayMode; },
		[this](EPlayModeType Mode) { PlayMode = Mode; },
		[this]() { return !IsPlaying(); });
}

// 프레임 시작·View 상태·월드 갱신 후 View별 오프스크린 렌더와 UI 합성을 진행한다.
// 입력·창 메시지는 FEngineLoop가 먼저 처리하고 Present는 호출 직후에 한다.
void UEditorEngine::Tick(const float DeltaTime)
{
	// PIE 창 닫기 요청 처리
	if (PIEWindow && PIEWindow->ConsumeCloseRequest())
		RequestEndPlayMap();
	// 에디터 단축키 (UE와 같은 키: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Alt+S, Alt+P, Pause).
	// Stop(Esc)은 PIE 입력 처리에서 따로 받으므로 여기서 빼서 두 번 실행되지 않게 한다.
	EditorCommandUI::ProcessShortcuts({ &NewScene, &OpenSceneCommand, &Save, &SaveAs, &Play, &Pause, &Resume });

	if (bRequestEndPlayMapQueued) { bRequestEndPlayMapQueued = false; EndPlayMap(); }
	StartQueuedPlaySessionRequest();
	// PIE 중 OpenLevel 요청 처리. 월드 Tick 전에 해야 지운 월드를 Tick하지 않는다.
	if (PlayWorld)
		if (FWorldContext* PIEContext = GetWorldContextFromWorld(PlayWorld))
			TickWorldTravel(*PIEContext);

	// 패키징 진행, 통계 표시, 패널에 넘길 DeltaTime을 갱신하고 Delete 키를 처리한다.
	Packager.Tick();
	FStatOverlay::Tick(DeltaTime);
	EditorControlsPanel->FEditorControlsPanel::DeltaTime = DeltaTime;

	if (!ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Delete))
		DeleteActor(OutlinerPanel->GetSelectedActor());

	// 뷰포트 레이아웃과 에디터 카메라를 갱신한다. 패널의 요청(레이아웃 변경, 분할선 드래그)을 적용하고,
	// 어느 View 위에 마우스가 있는지 찾아 활성 View를 정한 뒤 View마다 카메라 입력을 처리한다.
	{
		SCOPE_CYCLE_COUNTER(STAT_ViewportUpdate);

		const FVector2 ViewportSize = ViewportsPanel->GetContentSize();
		const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();

		ELayoutMode RequestedLayout{};
		int32 RequestedSingleViewIndex = SingleViewportIndex;

		if (ViewportsPanel->ConsumeLayoutRequest(RequestedLayout, RequestedSingleViewIndex))
		{
			if (RequestedLayout == ELayoutMode::Single)
				SingleViewportIndex = RequestedSingleViewIndex;
			ViewportLayoutMode = RequestedLayout;

			FEditorSettings& Settings = SettingsPanel->GetMutableSettings();
			Settings.bMultipleViewportsSingle = RequestedLayout == ELayoutMode::Single;
			Settings.MultipleViewportsSingleViewIndex = RequestedSingleViewIndex;
		}

		const float HorizontalDrag = ViewportsPanel->ConsumeHorizontalDrag();
		const float VerticalDrag = ViewportsPanel->ConsumeVerticalDrag();
		if (HorizontalDrag != 0.0f)
			ViewportSplitRatio = ApplySplitterDrag(ViewportSplitRatio, EDragAxis::Horizontal, HorizontalDrag, ViewportSize, 0.1f);
		if (VerticalDrag != 0.0f)
			ViewportSplitRatio = ApplySplitterDrag(ViewportSplitRatio, EDragAxis::Vertical, VerticalDrag, ViewportSize, 0.1f);
		if (HorizontalDrag != 0.0f || VerticalDrag != 0.0f)
		{
			SettingsPanel->GetMutableSettings().MultipleViewportsHorizontal = ViewportSplitRatio.Horizontal;
			SettingsPanel->GetMutableSettings().MultipleViewportsVertical = ViewportSplitRatio.Vertical;
		}

		FRect ViewRects[4]{};
		if (ViewportLayoutMode == ELayoutMode::Single)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				ViewRects[i] = (i == SingleViewportIndex)
					? FRect{ 0.0f, 0.0f, ViewportSize.X, ViewportSize.Y }
				: FRect{};
			}
		}
		else
		{
			ComputeViewRects(ViewportSplitRatio, ViewportSize, ViewRects);
		}

		for (int32 i = 0; i < 4; ++i)
		{
			const bool bSlotActive = (ViewportLayoutMode == ELayoutMode::Single)
				? (i == SingleViewportIndex)
				: true;
			ViewportsPanel->SetView(i, ViewRects[i], bSlotActive);
		}

		// 마우스 호버 감지
		int32 HoveredViewIndex = -1;
		if (ViewportsPanel->IsHovered())
		{
			for (int32 i = 0; i < 4; ++i)
			{
				if (ViewRects[i].Width > 0.0f && ViewRects[i].Height > 0.0f &&
					LocalMousePosition.X >= ViewRects[i].X &&
					LocalMousePosition.X < ViewRects[i].X + ViewRects[i].Width &&
					LocalMousePosition.Y >= ViewRects[i].Y &&
					LocalMousePosition.Y < ViewRects[i].Y + ViewRects[i].Height)
				{
					HoveredViewIndex = i;
					break;
				}
			}
		}

		// 마우스 캡처 및 활성 뷰 결정
		static int32 CapturedViewportIndex = -1;
		if (FInputSystem::IsMouseReleased(EMouseButton::Right))
		{
			CapturedViewportIndex = -1;
		}
		if (FInputSystem::IsMousePressed(EMouseButton::Right) && HoveredViewIndex != -1)
		{
			CapturedViewportIndex = HoveredViewIndex;
			ImGui::SetWindowFocus("Viewports");
		}
		if (CapturedViewportIndex != -1)
		{
			ViewportsPanel->SetActiveViewIndex(CapturedViewportIndex);
		}
		else if (FInputSystem::IsMousePressed(EMouseButton::Left) && HoveredViewIndex != -1)
		{
			ViewportsPanel->SetActiveViewIndex(HoveredViewIndex);
		}

		const int32 WheelDelta = FInputSystem::GetWheelDelta();
		const float MoveSpeed = 10.0f * SettingsPanel->GetSettings().CameraSpeed;
		const float MouseSens = 0.2f * SettingsPanel->GetSettings().MouseSensitivity;

		// PIE 게임 화면을 보여 주는 뷰의 카메라는 게임이 소유한다.
		// 여기서 에디터 카메라까지 움직이면 같은 마우스 입력이 두 번 적용된다.
		// SIE에서는 그 뷰도 에디터 뷰포트로 돌아와 있으므로 다른 뷰처럼 에디터 카메라를 움직인다.
		const bool bSimulating = IsSimulatingInEditor();
		const int32 GameOwnedViewIndex = (PlayWorld && !bSimulating) ? PIEViewIndex : InvalidViewIndex;

		for (int32 i = 0; i < 4; ++i)
		{
			FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(i);
			if (!Client || !Client->IsActive() || i == GameOwnedViewIndex)
				continue;

			Client->TickInput(DeltaTime, i == CapturedViewportIndex,
				i == HoveredViewIndex, WheelDelta, MoveSpeed, MouseSens);
		}

		// 활성 뷰포트 시점 동기화
		// SIE 중에는 PlayWorld의 MainCamera를 건드리지 않는다. 게임으로 돌아갔을 때 게임 카메라가 그대로 있어야 한다.
		const int32 ActiveIndex = ViewportsPanel->GetActiveViewIndex();
		if (FEditorViewportClient* ActiveClient = bSimulating ? nullptr : ViewportsPanel->GetViewportClient(ActiveIndex))
		{
			if (ACameraActor* MainCam = GetActiveWorld()->GetMainCamera())
			{
				if (UCameraComponent* CamCom = MainCam->GetCameraComponent())
				{
					CamCom->SetRelativeLocation(ActiveClient->GetViewLocation());
					CamCom->SetRelativeRotation(ActiveClient->GetViewRotation());
					CamCom->SetFieldOfView(ActiveClient->GetViewFOV());
					CamCom->SetOrthoWidth(ActiveClient->GetOrthoWidth());
					CamCom->SetIsOrthogonal(!ActiveClient->IsPerspective());
				}
			}
		}
	}

	// 월드를 Tick한다. PIE 중이면 게임 입력을 먼저 넘긴다. 이어서 에셋 에디터 Tick, 피킹과 기즈모 조작을 처리한다.
	// 게임 입력 처리
	if (PlayWorld)
	{
		TickPIEInput(DeltaTime);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_WorldTick);
		if (bStepRequested && bIsPaused)
		{
			GetActiveWorld()->SetPaused(false);
			GetActiveWorld()->Tick(1.0f / 60.0f);
			GetActiveWorld()->SetPaused(true);
			bStepRequested = false;
		}
		else if (!bIsPaused)
			GetActiveWorld()->Tick(DeltaTime);

	}
	if (PlayWorld && !bIsPaused)
	{
		FWorldContext* Context = GetWorldContextFromWorld(PlayWorld);
		if (Context && Context->GameViewport)
			Context->GameViewport->TickUI(DeltaTime);
	}
	{
		SCOPE_CYCLE_COUNTER(STAT_EditorTick);
		EditorManager->Tick(DeltaTime);
	}
	UpdateGizmoAndPicking();
	EditorWorld->GetScene().UpdateAllTransforms();
	if (PlayWorld && bIsPaused)
		PlayWorld->GetScene().UpdateAllTransforms();

	// 오디오 리스너 위치를 넘기고 오디오 장치를 갱신한다.
	if (FAudioDevice* AudioDevice = GetMainAudioDevice())
	{
		APlayerController* PC = PlayWorld ? PlayWorld->GetPlayerController() : nullptr;
		if (PC)
		{
			FVector Location, Front, Right;
			//PC->GetAudioListenerPosition(Location, Front, Right);
			AudioDevice->SetListener(PlayWorld, Location, Front, Right);
		}
		else
		{
			// 에디터 뷰포트 카메라: 활성 FEditorViewportClient의 GetViewLocation() / 회전으로 채운다.
			// (뷰포트 레이아웃 재구성 작업과 겹치므로, 그 작업에서 "활성 뷰포트 클라이언트" 접근자를 쓰는 게 깔끔하다)
		}
		AudioDevice->Update(!bIsPaused);
	}

	// 뷰포트를 그린다. UE의 UEditorEngine::Tick도 이 자리에서 뷰포트를 돌며 UpdateSingleViewportClient를 부른다.
	// 먼저 그림자 미리보기 상태를 지우고, 이번 프레임에 미리 볼 라이트와 PIE 화면의 표시 설정을 넘겨 둔다.
	const bool bIsPIE = (PlayWorld != nullptr);
	const ULightComponent* PreviewLight = DetailsPanel ? DetailsPanel->GetSelectedLight() : nullptr;
	// Invalidate skipped views too: never expose a previous frame's atlas range.
	for (FEditorViewportClient* Client : AllViewportClients)
	{
		if (!Client) continue;
		Client->GetRenderData().PreviewLight = PreviewLight;
		Client->GetRenderData().PreviewShadowRange = {};
	}
	if (bIsPIE)
	{
		if (FWorldContext* Context = GetPIEWorldContext())
		{
			if (Context->GameViewport)
			{
				Context->GameViewport->GetRenderData().PreviewLight = PreviewLight;
				Context->GameViewport->GetRenderData().PreviewShadowRange = {};

				// PIE 게임 화면은 에디터 설정 창의 값으로 그린다. 게임 뷰포트는 에디터를 모르므로 여기서 넘겨 준다.
				if (SettingsPanel)
				{
					const FEditorSettings& S = SettingsPanel->GetSettings();
					FSceneRenderOptions& ShowFlags = Context->GameViewport->EngineShowFlags;
					ShowFlags.bEnableFXAA = S.bEnableFXAA;
					ShowFlags.bCacheShadows = S.bCacheShadows;
					ShowFlags.PcfMode = S.PcfMode;
					ShowFlags.RenderingPath = S.RenderingPath;
					ShowFlags.LightingModel = S.LightingModel;
					ShowFlags.LightCullingMode = S.LightCullingMode;
					ShowFlags.DirectionalShadowMapSize = S.GetDirectionalShadowMapSize();
					ShowFlags.PointShadowMapSize = S.GetPointShadowMapSize();
					ShowFlags.SpotShadowMapSize = S.GetSpotShadowMapSize();
					ShowFlags.bGammaCorrection = S.bGammaCorrection;
					ShowFlags.DisplayGamma = S.DisplayGamma;
					ShowFlags.bShowVignette = S.bShowVignette;
					ShowFlags.bShowCameraOverlay = S.bShowCameraOverlay;
					ShowFlags.bShowSkeletalMesh = S.bShowSkeletalMesh;
				}
			}
		}
	}

	// 지금 편집 중인 뷰포트를 전역에 알려 둔다. 선택 외곽선과 기즈모는 그 뷰포트의 Draw만 그린다.
	GCurrentLevelEditingViewportClient = ViewportsPanel
		? ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex())
		: nullptr;

	// View마다 UpdateSingleViewportClient를 불러 그린다.
	for (int32 ViewIndex = 0; ViewIndex < AllViewportClients.Num(); ++ViewIndex)
	{
		UpdateSingleViewportClient(AllViewportClients[ViewIndex], ViewIndex);
	}

	// 별도 창(FWindow) 모드인 경우 PIE 창 타깃에 렌더링하고 스왑체인 백버퍼로 복사한다.
	bPIEWindowRendered = false;
	if (bIsPIE && IsPIEInWindow() && UpdatePIEWindowTargets())
	{
		FWorldContext* Context = GetPIEWorldContext();
		if (Context && Context->GameViewport && Context->World())
		{
			FGameViewportClient* GameClient = Context->GameViewport.get();
			const uint32 Width = GameClient->GetWidth();
			const uint32 Height = GameClient->GetHeight();
			if (Width > 0 && Height > 0)
			{
				GameClient->Draw(Renderer, UIRenderer.get());

				CopyPIEWindowToBackbuffer();
			}
		}
	}

	// 열려 있는 에셋 에디터의 미리보기를 그린다.
	EditorManager->RenderPreview();

	// 네 View의 결과 텍스처와 ImGui를 메인 창의 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
	// Swapchain 렌더링
	RenderCommand::BeginRenderPass(MainWindowSC->GetBackbuffer(), nullptr,
		MainWindow ? MainWindow->GetWidth() : 0,
		MainWindow ? MainWindow->GetHeight() : 0);

	{
		SCOPE_CYCLE_COUNTER(STAT_ImGui);

		ImGuiRenderer->Begin();

		EditorManager->DrawEditorUI();

		ImGuiRenderer->End();
	}

	RenderCommand::EndRenderPass();

	// 메인 Swapchain은 FEngineLoop가 VSync 설정대로 Present하므로 PIE 창은 기다리지 않고 올린다.
	if (bPIEWindowRendered && PIESwapchain)
		PIESwapchain->SwapBuffers(0);
	// UI 변경 후 설정을 복사해 종료 시 카메라 수명에 의존하지 않는다.
	SettingsPanel->CaptureViewportSettings();
	// 프로파일러 반영
	FStatRegistry::EndFrame();

	EditorManager->LateTick();
}


void UEditorEngine::PreLoadMap(FWorldContext& Context)
{
	if (Context.WorldType == EWorldType::PIE)
	{
		ResetSceneSelection();
		// 월드가 바뀌면 SIE를 끝내고 게임 화면으로 돌아간다.
		if (bIsSimulatingInEditor)
			ToggleBetweenPIEandSIE();
	}
}

void UEditorEngine::PostLoadMap(FWorldContext& Context)
{
	if (Context.WorldType != EWorldType::PIE) return;
	// GameViewport 객체는 그대로라 ViewportsPanel 연결은 다시 하지 않아도 된다.
	PlayWorld = Context.World();
	OnActiveWorldChanged();
}

// View 하나를 그린다. PIE를 시작한 View는 게임 뷰포트로, 나머지는 에디터 뷰포트로 그린다.
// UE의 UEditorEngine::UpdateSingleViewportClient와 같은 자리다. UE처럼 어느 쪽이든 뷰포트 클라이언트의 Draw가 그린다.
void UEditorEngine::UpdateSingleViewportClient(FEditorViewportClient* ViewClient, int32 ViewIndex)
{
	const bool bIsPIE = (PlayWorld != nullptr);
	if (!ViewClient || !ViewClient->IsActive())
		return;

	const FRect& ViewRect = ViewClient->GetRect();
	if (ViewRect.Width <= 0.0f || ViewRect.Height <= 0.0f)
		return;

	bool bIsGameView = false;
	int32 TargetPlayerIndex = -1;

	// SIE에서는 이 View도 에디터 뷰포트로 그린다. UE도 Simulate 화면은 레벨 에디터 뷰포트 클라이언트가 그린다.
	if (bIsPIE && !bIsSimulatingInEditor && !IsPIEInWindow() && PlayMode == EPlayModeType::InViewport &&
		ViewIndex == PIEStartEditorViewIndex)
	{
		bIsGameView = true;
		TargetPlayerIndex = 0;
	}

	if (bIsGameView && TargetPlayerIndex >= 0)
	{
		FGameViewportClient* GameClient = nullptr;
		for (int32 WorldIdx = 0; WorldIdx < WorldContextlist.Num(); ++WorldIdx)
		{
			if (WorldContextlist[WorldIdx].WorldType == EWorldType::PIE &&
				WorldContextlist[WorldIdx].PIEInstance == TargetPlayerIndex &&
				WorldContextlist[WorldIdx].GameViewport)
			{
				GameClient = WorldContextlist[WorldIdx].GameViewport.get();
				// 게임 뷰포트의 타깃 크기를 이 View의 영역에 맞춘 뒤 그린다.
				GameClient->Resize(
					static_cast<uint32>(std::max(ViewRect.Width, 1.0f)),
					static_cast<uint32>(std::max(ViewRect.Height, 1.0f)));
				GameClient->Draw(Renderer, UIRenderer.get());

				break;
			}
		}
	}
	else
	{
		ViewClient->Draw(Renderer);
	}
}

FWorldContext& UEditorEngine::GetEditorWorldContext()
{
	for (int32 i = 0; i < WorldContextlist.Num(); ++i)
	{
		if (WorldContextlist[i].WorldType == EWorldType::Editor)
		{
			return WorldContextlist[i];
		}
	}

	return CreateNewWorldContext(EWorldType::Editor);
}

FWorldContext* UEditorEngine::GetPIEWorldContext(int32 WorldPIEInstance)
{
	for (FWorldContext& WorldContext : WorldContextlist)
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.PIEInstance == WorldPIEInstance)
		{
			return &WorldContext;
		}
	}

	return nullptr;
}



void UEditorEngine::UpdateGizmoAndPicking()
{
	const int32 ViewIndex = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : -1;
	if (ViewIndex < 0 || ViewIndex >= 4 || !ViewportsPanel || !ViewportsPanel->IsHovered())
		return;
	if (IsPlayingView(ViewIndex))
		return;

	FEditorViewportClient* ViewClient = ViewportsPanel->GetViewportClient(ViewIndex);
	if (!ViewClient || !ViewClient->IsActive())
		return;

	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();
	ViewClient->UpdateGizmoAndPicking(Gizmo.get(), LocalMousePosition, [this](AActor* SelectedActor) {
		OutlinerPanel->SelectActor(SelectedActor);
		});
}

//void UEditorEngine::RenderSceneText(UWorld* TargetWorld, const FSceneView& SceneView)
//{
//	if (!TextRenderer) return;
//	for (TObjectIterator<UTextRenderComponent> Text; Text; ++Text)
//	{
//		if (!Text || !Text->IsRegistered() || !Text->GetFont() || !Text->IsShown(SceneView.bGameView) || !Text->GetOwner() ||
//			Text->GetOwner()->GetWorld() != TargetWorld)
//			continue;
//		TextRenderer->OnRender(Text->GetText(), Text->GetWorldMatrix(), Text->GetTextSize(),
//			*Text->GetFont(), SceneView.ViewProjectionMatrix);
//	}
//}
//
//bool UEditorEngine::RenderHeightFog(UWorld* TargetWorld, const FSceneView& SceneView,
//	FTexture2D* DepthTarget, FTexture2D* ColorTarget)
//{
//	if (!TargetWorld || !HeightFogRenderer || !DepthTarget || !ColorTarget ||
//		!SceneView.bIsPerspective)
//		return false;
//
//	for (TObjectIterator<UExponentialHeightFogComponent> Fog; Fog; ++Fog)
//	{
//		if (!Fog || !Fog->IsRegistered() || !Fog->IsShown(SceneView.bGameView) || !Fog->GetOwner() ||
//			Fog->GetOwner()->GetWorld() != TargetWorld)
//			continue;
//
//		FHeightFogSetting Setting;
//		Setting.FogDensity = Fog->GetFogDensity();
//		Setting.FogHeightFalloff = Fog->GetFogHeightFalloff();
//		Setting.FogHeight = Fog->GetFogHeight();
//		Setting.FogColor = Fog->GetFogColor();
//		Setting.StartDistance = Fog->GetStartDistance();
//		Setting.FogMaxOpacity = Fog->GetFogMaxOpacity();
//		Setting.FogCutoffDistance = Fog->GetFogCutoffDistance();
//		if (HeightFogRenderer->OnRender(SceneView, DepthTarget, ColorTarget, Setting))
//			return true;
//	}
//	return false;
//}

// ImGui를 정리한다. UObject 일괄 삭제와 공용 자원·Device 정리는 FEngineLoop가 이어서 한다.
void UEditorEngine::PreExit()
{
	EndPlayMap();
	// 시퀀서는 에디터 월드 액터를 가리키므로 월드보다 먼저 정리한다
	if (SequencerPanel) SequencerPanel->Shutdown();
	EditorWorld->ClearWorld();
	// PIE 창이 열린 채 종료하면 Device가 살아 있을 때 Swapchain과 타깃을 먼저 정리한다.
	DestroyPIEWindow();

	// 레벨 패널을 마지막으로 사용한 뒤 탭을 파괴하고 빌린 참조를 비운다.
	FLog::RemoveSink(OutputLogPanel);
	if (EditorManager)
	{
		EditorManager->Shutdown();
	}

	LevelEditorTab = nullptr;
	OutputLogPanel = nullptr;
	DetailsPanel = nullptr;
	EditorControlsPanel = nullptr;
	SettingsPanel = nullptr;
	SequencerPanel = nullptr;
	ViewportsPanel = nullptr;
	ContentDrawerPanel = nullptr;
	OutlinerPanel = nullptr;
	AllViewportClients.Empty();

	ImGuiRenderer->Shutdown();
	GEditor = nullptr;
}

// 선택과 Gizmo 참조를 정리한 뒤 Actor를 삭제한다.
void UEditorEngine::DeleteActor(AActor* Actor)
{
	if (!Actor)
		return;
	// 실행 중 삭제는 게임 로직이 들고 있는 참조를 깨뜨릴 수 있어 Pause 상태에서만 허용한다.
	if (IsPlaying() && !bIsPaused && !IsSimulatingInEditor())
		return;

	OutlinerPanel->SelectActor(nullptr);
	DetailsPanel->SelectComponent(nullptr);

	Actor->Destroy();
}

void UEditorEngine::DeleteComponent(UActorComponent* Component)
{
	if (!Component)
		return;
	if (IsPlaying() && !bIsPaused && !IsSimulatingInEditor())
		return;
	DetailsPanel->SelectComponent(nullptr);
	Outline->SetTarget(nullptr);
	Component->GetOwner()->DestroyComponent(Component);
}

void UEditorEngine::OnActiveWorldChanged()
{
	ResetSceneSelection();
	UWorld* ActiveWorld = GetActiveWorld();

	// 피킹 대상 월드도 렌더링 대상 월드에 맞춘다.
	if (ViewportsPanel)
	{
		for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
		{
			FEditorViewportClient* Client =
				ViewportsPanel->GetViewportClient(ViewIndex);

			if (Client)
			{
				Client->SetWorld(ActiveWorld);
			}
		}
	}

	if (OutlinerPanel) OutlinerPanel->SetWorld(ActiveWorld);
	if (DetailsPanel) DetailsPanel->SetWorld(ActiveWorld);
	if (EditorControlsPanel) EditorControlsPanel->SetWorld(ActiveWorld);
	if (SettingsPanel) SettingsPanel->SetWorld(ActiveWorld);
	if (SequencerPanel) SequencerPanel->SetWorld(ActiveWorld, ActiveWorld == EditorWorld);
}

// 씬 변경으로 무효화된 에디터의 선택 참조를 모두 해제한다.
void UEditorEngine::ResetSceneSelection()
{
	Gizmo->SetTarget(nullptr, false);
	Outline->SetTarget(nullptr);
	DetailsPanel->SetTarget(nullptr);
	OutlinerPanel->SelectActor(nullptr);
}

// 새 씬 생성이 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::CreateNewScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot create a new scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::NewScene(EditorWorld))
		return;

	ResetSceneSelection();
	LevelEditorTab->OnLevelLoaded.Broadcast(FEditorFileUtils::CurrentScenePath);
}

// 씬 불러오기가 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::OpenScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot open a scene during PIE. Stop the play session first.");
		return;
	}
	if (!FEditorFileUtils::LoadScene(EditorWorld))
		return;

	ResetSceneSelection();

	LevelEditorTab->OnLevelLoaded.Broadcast(FEditorFileUtils::CurrentScenePath);
}

// 공통 파일 유틸리티로 현재 씬을 저장한다.
void UEditorEngine::SaveCurrentScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveScene(EditorWorld);
}

// 공통 파일 유틸리티로 새 경로에 씬을 저장한다.
void UEditorEngine::SaveSceneAs()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveSceneAs(EditorWorld);
}

// PIE 디버그 표시 (Settings > Debug Draw (PIE))
void UEditorEngine::DrawPIEDebug(UWorld* World, const FMatrix& ViewProjection)
{
	const FEditorSettings& Settings = SettingsPanel->GetSettings();
	GShowSkeletalRootDebug = Settings.bShowRootDebug;  // 런타임 "root" 라벨 표시용

	FLineBatcher* LineBatcher = World->LineBatcher.get();
	LineBatcher->BeginFrame();
	if (Settings.bShowCollision)
		DrawCollisionShapes(World, LineBatcher, nullptr);
	LineBatcher->OnRender(ViewProjection);

	// root는 바닥 높이라 가려지기 쉬우므로 깊이 테스트 없이 따로 그린다 (항상 위에 보임)
	if (Settings.bShowRootDebug)
	{
		LineBatcher->BeginFrame();
		DrawSkeletalRootDebug(World, LineBatcher);
		LineBatcher->SetDepthTest(false);
		LineBatcher->OnRender(ViewProjection);
		LineBatcher->SetDepthTest(true);
	}
}
