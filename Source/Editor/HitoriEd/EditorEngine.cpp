#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Core/ResourcePaths.h"

#include "Editor/HitoriEd/EditorEngine.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"
#include "Input/MouseCapture.h"
#include "GameFramework/PlayerController.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BillboardComponent.h"
#include "Serialization/JsonArchive.h"


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
#include "Components/DecalComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkeletalMeshDebugComponent.h"

#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "UObject/UObjectIterator.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

#include "Scripting/LuaScriptManager.h"
#include <sol/sol.hpp>

#include "Components/ShapeComponent.h"

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

FEngineConfig UEditorEngine::GetConfig() const
{
	FEngineConfig Desc;
	Desc.Title = L"Hitori Engine";
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.bBorderless = false;
	Desc.SyncInterval = 0;
	// View는 각자 깊이 버퍼를 쓰고 백버퍼에는 ImGui만 그린다.
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false;
	// 파일이 없으면 검은 배경에 상태 텍스트만 표시된다.
	Desc.SplashImage = ResourcePaths::SplashImage;
	return Desc;
}

// 렌더 자원·월드·에디터와 MultipleViewports 연결을 초기화한다.
// Device·Window·Swapchain·AssetManager는 FEngineLoop가 먼저 만들어 둔다.
bool UEditorEngine::Init()
{
	// 에셋 로드처럼 등록 전에 찍힌 로그는 FLog가 보관했다가 넘겨준다. 종료 중 로그가 와도 안전하도록 해제하지 않는다.
	static FFileLogSink* EditorLogFile = new FFileLogSink(EditorResourcePaths::EditorLogFile, EditorResourcePaths::EditorLogBackupCount);
	FLog::AddSink(EditorLogFile);

	if (!Super::Init())
		return false;

	FWorldContext& InitialWorldContext = CreateNewWorldContext(EWorldType::Editor);
	InitialWorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Editor, true));
	EditorWorld = InitialWorldContext.World();

	MainWindow = GetEngineLoop().GetMainWindow();
	MainWindowSC = GetEngineLoop().GetSwapchain();
	Renderer = GetEngineLoop().GetRenderer();
	FRenderDevice* RenderDevice = GetEngineLoop().GetRenderDevice();

	EditorUI = MakeUnique<FEditorUI>();
	EditorUI->Init();

	EditorUI->SetNewSceneCallback([this]() { CreateNewScene(); });
	EditorUI->SetOpenSceneCallback([this]() { OpenScene(); });
	EditorUI->SetSaveSceneCallback([this]() { SaveCurrentScene(); });
	EditorUI->SetSaveSceneAsCallback([this]() { SaveSceneAs(); });
	EditorUI->SetPackageGameCallback([this]() { Packager.Start("Shipping"); });
	EditorUI->SetIsPackagingQuery([this]() { return Packager.IsRunning(); });

	OutputLogPanel = EditorUI->AddEditorPanel<FOutputLogPanel>();
	FLog::AddSink(OutputLogPanel);
	HTR_LOG(Info, "Editor Initialize...");

	// 테스트 코드 
#ifdef ENGINE_DEBUG
	FCollision::RunSelfTest();
#endif

	HTR_LOG(Info, "Initialize ImGui...");
	ImGuiRenderer = MakeUnique<FImGuiRenderer>();
	if (!ImGuiRenderer->Init(MainWindow->GetHandle(), RenderDevice->GetDevice(), RenderDevice->GetContext()))
	{
		HTR_LOG(Error, "Failed To Initialize ImGui!");
	}
	HTR_LOG(Info, "Initialize ImGui Success!");

	GridRenderer = MakeUnique<FGridRenderer>();
	GridRenderer->Init(Renderer);

	GizmoRenderer = MakeUnique<FGizmoRenderer>();
	GizmoRenderer->Init(Renderer);

	Gizmo = MakeUnique<FGizmo>();

	DetailsPanel = EditorUI->AddEditorPanel<FDetailsPanel>();
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


	EditorControlsPanel = MakeUnique<FEditorControlsPanel>();
	EditorControlsPanel->Init();
	EditorUI->GetToolbar()->SetEditingControls([this]() { EditorControlsPanel->DrawToolbar(); });
	ViewportsPanel = EditorUI->AddEditorPanel<FViewportsPanel>();
	ContentDrawerPanel = EditorUI->AddEditorPanel<FContentDrawerPanel>();
	EditorUI->SetFbxImportedCallback([this]()
	{
		RefreshImportedAssets();
		ContentDrawerPanel->RequestThumbnailRefresh();
	});
	EditorUI->SetFbxImportBlockedQuery([this]() { return IsPlaying(); });
	AssetEditorPanel = EditorUI->AddEditorPanel<FAssetEditorPanel>();
	// 다시 가져오기의 옵션은 바이너리 헤더에서 읽는다. Target Skeleton은 헤더에 없으므로,
	// 다른 FBX의 Skeleton에 맞춘 AnimSequence면 그 Skeleton을 대상으로 다시 지정한다(메시 없는 FBX는 대상 없이 임포트되지 않는다).
	const auto ReadReimportOptions = [](const FString& Path, FFbxAssetData& Data) -> bool
	{
		FString Error;
		if (!FFbxAssetBake::ReadHeader(Path, Data, Error)) { HTR_LOG(Error, "[FBX] {}", Error); return false; }
		if (Data.Type != EFbxAssetType::AnimSequence) return true;
		FFbxAssetData Full, SkeletonHeader; std::filesystem::path SkeletonFile;
		if (FFbxAssetBake::ReadBaked(Path, Full, Error) && FbxPath::ResolveKey(Full.SkeletonKey, SkeletonFile, Error) &&
			FFbxAssetBake::ReadHeader(FbxPath::ToString(SkeletonFile), SkeletonHeader, Error) &&
			SkeletonHeader.SourcePath != Data.SourcePath)
			Data.Options.TargetSkeletonKey = Full.SkeletonKey;
		return true;
	};
	ContentDrawerPanel->SetImportFbxCallback([this, ReadReimportOptions](const FString& Path, bool Reimport)
	{
		if (IsPlaying()) { HTR_LOG(Warning, "Stop PIE before importing FBX."); return; }
		if (!Reimport) { EditorUI->QueueFbxImports({ Path }); return; }
		FFbxAssetData Data;
		if (!ReadReimportOptions(Path, Data)) return;
		EditorUI->QueueFbxReimport(FbxPath::ToString(FbxPath::SourceFile(Data.ActualSourcePath)),
			FbxPath::ToString(FbxPath::FromString(Path).parent_path()), Data.Options, Data.SourcePath);
	});
	ContentDrawerPanel->SetReimportFromFileCallback([this, ReadReimportOptions](const FString& Path)
	{
		if (IsPlaying()) { HTR_LOG(Warning, "Stop PIE before importing FBX."); return; }
		FFbxAssetData Data;
		if (!ReadReimportOptions(Path, Data)) return;
		EditorUI->BrowseFbxReimportFile(FbxPath::ToString(FbxPath::FromString(Path).parent_path()), Data.Options, Data.SourcePath);
	});
	ContentDrawerPanel->SetImportFbxFilesCallback([this](const FString& OutputDirectory)
	{
		if (IsPlaying()) { HTR_LOG(Warning, "Stop PIE before importing FBX."); return; }
		EditorUI->BrowseFbxImportFiles(OutputDirectory);
	});
	// 바이너리가 없는 FBX는 시작할 때 차례로 묻는다. 다음 실행에서도 바이너리가 없으면 다시 묻는다.
	EditorUI->QueueFbxImports(UAssetManager::Get().GetFbxImportCandidates());
	ContentDrawerPanel->SetOpenAssetCallback([this](const FString& Path)
	{
		FString Extension = std::filesystem::path(Path).extension().string();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
		// 더블클릭(또는 우클릭 > Open): 씬은 레벨로 열고, 스크립트는 Details의 Edit Script처럼 외부 편집기로 연다.
		if (Extension == ".scene")
		{
			if (IsPlaying()) { HTR_LOG(Warning, "Stop PIE before opening a scene."); return; }
			// File > Open Scene과 같은 형식(절대 경로)으로 기억해야 Save와 스크립트 이름(씬이름_액터이름)이 같게 나온다.
			std::error_code Ec;
			const std::filesystem::path Absolute = std::filesystem::absolute(FbxPath::FromString(Path), Ec).lexically_normal();
			const FString ScenePath = Ec ? Path : FbxPath::ToString(Absolute);
			if (FJsonArchive::LoadWorld(EditorWorld, ScenePath))
			{
				FEditorFileUtils::CurrentScenePath = ScenePath;
				HTR_LOG(Info, "Load Scene : {}", ScenePath);
				ResetSceneSelection();
			}
		}
		else if (Extension == ".lua")
		{
			FLuaScriptManager::Get().OpenScriptInEditor(Path);
		}
		else if (Extension == ".animmontage" && AnimMontageEditorPanel)
		{
			AnimMontageEditorPanel->OpenAsset(Path);
		}
		else if (Extension == ".aimoffset" && AimOffsetEditorPanel)
		{
			AimOffsetEditorPanel->OpenAsset(Path);
		}
		else AssetEditorPanel->OpenAsset(Path);
	});

	// OutLine
	OutlineRenderer = MakeUnique<FOutlineRenderer>();
	OutlineRenderer->Init(Renderer);

	SceneDepthRenderer = MakeUnique<FSceneDepthRenderer>();
	SceneDepthRenderer->Init(Renderer);
	CascadeFloorRenderer = MakeUnique<FCascadeFloorRenderer>();
	CascadeFloorRenderer->Init();

	SettingsPanel = EditorUI->AddEditorPanel<FSettingsPanel>();
	SequencerPanel = EditorUI->AddEditorPanel<FSequencerPanel>();
	OutputLogPanel->SetEditorSettings(&SettingsPanel->GetMutableSettings());

	UIEditorPanel = EditorUI->AddEditorPanel<FUIEditorPanel>();
	PoseRetargetPanel = EditorUI->AddEditorPanel<FPoseRetargetPanel>();
	PoseRetargetPanel->SetOpen(false);
	ContentDrawerPanel->SetCreateWidgetCallback([this](const FString& Path) { UIEditorPanel->NewAsset(Path); });
	ContentDrawerPanel->SetOpenWidgetCallback([this](const FString& Path) { UIEditorPanel->OpenAsset(Path); });

	SkeletalMeshViewerPanel = EditorUI->AddEditorPanel<FSkeletalMeshViewerPanel>();
	ContentDrawerPanel->SetOpenSkeletalMeshCallback([this](const FString& Path) { SkeletalMeshViewerPanel->OpenFBX(Path); });
	AnimSequenceViewerPanel = EditorUI->AddEditorPanel<FAnimSequenceViewerPanel>();
	ContentDrawerPanel->SetOpenAnimSequenceCallback([this](const FString& Path) { AnimSequenceViewerPanel->OpenSequence(Path); });
	AnimGraphEditorPanel = EditorUI->AddEditorPanel<FAnimGraphEditorPanel>();
	ContentDrawerPanel->SetCreateAnimGraphCallback([this](const FString& Path, const FString& SkeletonKey) { AnimGraphEditorPanel->NewAsset(Path, SkeletonKey); });
	ContentDrawerPanel->SetOpenAnimGraphCallback([this](const FString& Path) { AnimGraphEditorPanel->OpenAsset(Path); });
	BlendSpaceEditorPanel = EditorUI->AddEditorPanel<FBlendSpaceEditorPanel>();
	AnimMontageEditorPanel = EditorUI->AddEditorPanel<FAnimMontageEditorPanel>();
	AimOffsetEditorPanel = EditorUI->AddEditorPanel<FAimOffsetEditorPanel>();
	AnimGraphEditorPanel->SetOpenBlendSpaceCallback([this](const FString& Path) { BlendSpaceEditorPanel->OpenAsset(Path); });
	AnimGraphEditorPanel->SetOpenMontageCallback([this](const FString& Path) { AnimMontageEditorPanel->OpenAsset(Path); });
	AnimGraphEditorPanel->SetOpenAimOffsetCallback([this](const FString& Path) { AimOffsetEditorPanel->OpenAsset(Path); });
	BlendSpaceEditorPanel->SetOpenSequenceCallback([this](const FString& Path) { AnimSequenceViewerPanel->OpenSequence(Path); });
	AnimMontageEditorPanel->SetOpenSequenceCallback([this](const FString& Path) { AnimSequenceViewerPanel->OpenSequence(Path); });
	AimOffsetEditorPanel->SetOpenSequenceCallback([this](const FString& Path) { AnimSequenceViewerPanel->OpenSequence(Path); });
	AnimMontageEditorPanel->SetSavedCallback([this]() { AnimGraphEditorPanel->NotifyAnimationAssetSaved(); });
	AimOffsetEditorPanel->SetSavedCallback([this]() { AnimGraphEditorPanel->NotifyAnimationAssetSaved(); });
	ContentDrawerPanel->SetCreateBlendSpaceCallback([this](const FString& Path, bool b2D) { BlendSpaceEditorPanel->NewAsset(Path, b2D); });
	ContentDrawerPanel->SetOpenBlendSpaceCallback([this](const FString& Path) { BlendSpaceEditorPanel->OpenAsset(Path); });

	Outline = MakeUnique<FOutline>();

	SystemFont = UAssetManager::GetAssetByPath<UFont>(ResourcePaths::DefaultFont);

	TextRenderer = MakeUnique<FTextRenderer>();
	TextRenderer->Init();

	UIRenderer = MakeUnique<FUIRenderer>();
	UIRenderer->Init();

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



	OutlinerPanel = EditorUI->AddEditorPanel<FOutlinerPanel>();
	OutlinerPanel->SetWorld(GetActiveWorld());
	OutlinerPanel->SetSelectionCallback(
		[this](USceneComponent* Root)
		{
			TArray<USceneComponent*> Targets;
			for (AActor* Actor : Selection.GetActors())
				if (USceneComponent* Component = Actor->GetRootComponent())
					Targets.Add(Component);
			Gizmo->SetTargets(Root, Targets, true);
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
					TArray<USceneComponent*> Targets;
					for (AActor* Actor : Selection.GetActors())
						if (USceneComponent* Root = Actor->GetRootComponent())
							Targets.Add(Root);
					Gizmo->SetTargets(SceneComponent, Targets, true);
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
	OutlinerPanel->SetActorDoubleClickCallback([this](AActor* Actor) { FocusViewportOnActor(Actor); });
	OutlinerPanel->SetSelection(&Selection);
	DetailsPanel->SetDeleteComponentCallback(
		[this](UActorComponent* Component)
		{
			DeleteComponent(Component);
		}
	);

	LineBatcher = MakeUnique<FLineBatcher>();
	LineBatcher->Init(Renderer, GetActiveWorld());

	DetailsPanel->SetWorld(GetActiveWorld());

	EditorControlsPanel->SetWorld(EditorWorld);
	EditorControlsPanel->SetIsPlayingQuery([this]() { return IsPlaying(); });
	EditorControlsPanel->SetSkipGizmoHotkeyQuery([this]() { return SkeletalMeshViewerPanel->HasFocuesdViewer(); });
	EditorControlsPanel->SetGizmo(Gizmo.get());
	EditorControlsPanel->SetViewportsPanel(ViewportsPanel);
	// 새로 만든 액터를 선택해 디테일 패널과 기즈모가 바로 그 액터를 가리키게 한다.
	EditorControlsPanel->SetOnActorSpawned([this](AActor* Actor) { OutlinerPanel->SelectActor(Actor); });

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

	const ImVec4 White = { 1, 1, 1, 1 };
	const ImVec4 Green = { 0.40f, 0.80f, 0.30f, 1.0f };

	Save = { "Save",   "Save Current Scene",UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarSaveIcon),  White, { EKeyCode::S, true } };

	// PIE 중에는 복제 월드가 아니라 에디터 월드를 저장해야 하므로 막는다.
	Save.CanExecute = [this]()->bool { return !PlayWorld; };
	Save.Execute = [this]() { SaveCurrentScene(); };

	// 씬은 PIE 중이 아닐 때만 저장하고, 애셋 에디터의 변경은 PIE 중에도 저장한다.
	SaveAll = { "Save All", "Save the scene and all open animation assets", nullptr, White, { EKeyCode::S, true, false, true } };
	SaveAll.Execute = [this]() { SaveAllAssets(); };

	// 액터 복사·붙여넣기·복제 (TEAM1). PIE 중에는 복제 월드를 건드리지 않도록 막는다.
	Copy = { "Copy", "Copy Selected Actors", nullptr, White, { EKeyCode::C, true } };
	Copy.CanExecute = [this]() { return !PlayWorld && !Selection.IsEmpty(); };
	Copy.Execute = [this]() { CopySelectedActors(); };
	Paste = { "Paste", "Paste Copied Actors", nullptr, White, { EKeyCode::V, true } };
	Paste.CanExecute = [this]() { return !PlayWorld && !CopiedActorData.IsEmpty(); };
	Paste.Execute = [this]() { PasteCopiedActors(); };
	Duplicate = { "Duplicate", "Duplicate Selected Actors", nullptr, White, { EKeyCode::D, true } };
	Duplicate.CanExecute = [this]() { return !PlayWorld && !Selection.IsEmpty(); };
	Duplicate.Execute = [this]() { CopySelectedActors(); PasteCopiedActors(); };

	Play = { "Play",   "Play World",        UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPlayIcon),  Green, { EKeyCode::P, false, true } };

	Play.CanExecute = [this]()->bool {return !PlayWorld; };
	Play.Execute = [this]() { RequestPlaySession(); };

	Pause = { "Pause",  "Pause",			UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPauseIcon), White, { EKeyCode::Pause } };

	Pause.CanExecute = [this]()->bool {return !bIsPaused && PlayWorld; };
	Pause.Execute = [this]() { bIsPaused = true; if (PlayWorld) PlayWorld->SetPaused(true); };


	Resume = { "Resume", "Resume",          UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPlayIcon),  White, { EKeyCode::Pause } };

	Resume.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	Resume.Execute = [this]() { bIsPaused = false; if (PlayWorld) PlayWorld->SetPaused(false); };

	StepFrame = { "Step", "Step One Frame", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarStepIcon),  White, {} };

	StepFrame.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	StepFrame.Execute = [this]() { bStepRequested = true; };


	Stop = { "Stop",   "Stop",				UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarStopIcon),  White, { EKeyCode::Escape } };

	Stop.CanExecute = [this]()->bool {return PlayWorld; };
	Stop.Execute = [this]() { RequestEndPlayMap(); };

	// F8 자체는 PIE 입력 처리(UpdatePIEInput)가 이미 받는다. 같은 키를 ProcessShortcuts에 또 넣으면 한 번 눌러 두 번 전환되므로,
	// 여기의 Shortcut은 툴팁 표시용이다(Stop의 Escape와 같다). 버튼은 F8 입력과 같은 경로로 전환을 요청한다.
	const auto RequestEjectToggle = [this]()
	{
		if (FWorldContext* Context = GetPIEWorldContext(); Context && Context->GameViewport)
			Context->GameViewport->InputKey(nullptr, static_cast<int32>(EKeyCode::F8), true);
	};
	const ImVec4 Orange = { 1.0f, 0.70f, 0.25f, 1.0f };
	Eject = { "Eject", "Eject from the player pawn and use the editor camera", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarEjectIcon), White, { EKeyCode::F8 } };
	Eject.CanExecute = [this]()->bool { return PlayWorld && !IsSimulatingInEditor(); };
	Eject.IsVisible = [this]()->bool { return !IsSimulatingInEditor(); };
	Eject.Execute = RequestEjectToggle;
	Possess = { "Possess", "Possess the player pawn again", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarEjectIcon), Orange, { EKeyCode::F8 } };
	Possess.CanExecute = [this]()->bool { return PlayWorld && IsSimulatingInEditor(); };
	Possess.IsVisible = [this]()->bool { return IsSimulatingInEditor(); };
	Possess.Execute = RequestEjectToggle;

	EditorUI->GetToolbar()->SetCommands({ Play, Pause, Resume, StepFrame, Stop, Eject, Possess });
	EditorUI->GetToolbar()->SetPlayModeBinding(
		[this]() { return PlayMode; },
		[this](EPlayModeType Mode) { PlayMode = Mode; },
		[this]() { return !IsPlaying(); });

	FLuaScriptManager::Get().GetState().script("print('hello', 1 + 2, nil)");
	EditorUI->RegisterPanelSettings();

	return true;
}

// 프레임 시작·View 상태·월드 갱신 후 View별 오프스크린 렌더와 UI 합성을 진행한다.
// 입력·창 메시지는 FEngineLoop가 먼저 처리하고 Present는 호출 직후에 한다.
void UEditorEngine::Tick(const float DeltaTime)
{
	// PIE 창 닫기 요청 처리
	if (PIEWindow && PIEWindow->ConsumeCloseRequest())
		RequestEndPlayMap();
	if (bRequestEndPlayMapQueued) { bRequestEndPlayMapQueued = false; EndPlayMap(); }
	if (bPlaySessionRequested) { bPlaySessionRequested = false; if (!PlayWorld) StartPlayInEditorSession(); }
	// PIE 중 OpenLevel 요청 처리. 월드 Tick 전에 해야 지운 월드를 Tick하지 않는다.
	if (PlayWorld)
		if (FWorldContext* PIEContext = GetWorldContextFromWorld(PlayWorld))
			TickWorldTravel(*PIEContext);

	BeginFrame(DeltaTime);
	UpdateViewportState(DeltaTime);
	TickWorldAndEditor(DeltaTime);
	if (FAudioDevice* AudioDevice = GetMainAudioDevice())
	{
		UWorld* ActiveWorld = GetActiveWorld();
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
	RenderViewports();
	EndFrame();
}

void UEditorEngine::PreLoadMap(FWorldContext& Context)
{
	if (Context.WorldType == EWorldType::PIE)
		ResetSceneSelection();
}

void UEditorEngine::RefreshImportedAssets()
{
	// 리임포트는 옛 객체를 Retired로 남긴다. 레벨의 컴포넌트를 같은 키의 새 객체로 옮긴다.
	// 뷰어와 리타겟 패널은 자기 월드의 포즈 상태를 함께 관리하므로 여기서 건드리지 않는다.
	const auto Current = []<typename T>(T* Asset) -> T*
	{
		if (!Asset || !Asset->IsRetired()) return Asset;
		T* Updated = UAssetManager::GetAssetByPath<T>(Asset->GetPath());
		return Updated ? Updated : Asset;
	};
	for (TObjectIterator<UMeshComponent> It; It; ++It)
	{
		UMeshComponent* Component = *It;
		if (!Component || !Component->GetOwner() || Component->GetOwner()->GetWorld() != EditorWorld) continue;

		// 메시를 바꾸면 슬롯 덮어쓰기가 지워지므로 먼저 보관한다.
		TArray<UMaterial*> Overrides;
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot) Overrides.Add(Current(Component->GetOverrideMaterial(Slot)));
		if (auto* Skinned = Cast<USkinnedMeshComponent>(Component)) Skinned->SetSkeletalMesh(Current(Skinned->GetSkeletalMesh()));
		else if (auto* Static = Cast<UStaticMeshComponent>(Component)) Static->SetStaticMesh(Current(Static->GetStaticMesh()));
		for (int32 Slot = 0; Slot < Overrides.Num() && Slot < Component->GetNumMaterials(); ++Slot)
			if (Overrides[Slot] != Component->GetOverrideMaterial(Slot)) Component->SetMaterial(Slot, Overrides[Slot]);
	}
}

void UEditorEngine::PostLoadMap(FWorldContext& Context)
{
	if (Context.WorldType != EWorldType::PIE) return;
	// GameViewport 객체는 그대로라 ViewportsPanel 연결은 다시 하지 않아도 된다.
	PlayWorld = Context.World();
	OnActiveWorldChanged();
}

// DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
void UEditorEngine::BeginFrame(const float DeltaTime)
{
	Packager.Tick();
	FStatOverlay::Tick(DeltaTime);
	EditorControlsPanel->FEditorControlsPanel::DeltaTime = DeltaTime;
	EditorControlsPanel->Tick(DeltaTime);

	PruneSelection();

	if (!ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Delete))
		DeleteSelectedActors();

	// 툴바 명령의 단축키 (TEAM1처럼 매 프레임 처리한다). 텍스트 입력 중이면 ProcessShortcuts가 건너뛴다.
	// Stop(Esc)은 PIE 입력에서 따로 처리한다. 스켈레탈 뷰어 조작 중에는 레벨 단축키를 받지 않는다.
	if (!(SkeletalMeshViewerPanel && SkeletalMeshViewerPanel->HasFocuesdViewer()))
	{
		// 우클릭 비행 중의 Ctrl+D 등은 카메라 이동 키와 겹치므로 받지 않는다.
		if (!FInputSystem::IsMouseDown(EMouseButton::Right))
		{
			// 애니메이션 에디터 창에 포커스가 있으면 Ctrl+S는 그 창이 처리하므로 씬 저장과 복사·붙여넣기 등은 받지 않는다.
			// 전체 저장(Ctrl+Shift+S)은 어디에 포커스가 있어도 동작한다.
			const bool bAnimEditorFocused =
				(AnimSequenceViewerPanel && AnimSequenceViewerPanel->HasFocusedWindow()) ||
				(AnimGraphEditorPanel && AnimGraphEditorPanel->HasFocusedWindow()) ||
				(BlendSpaceEditorPanel && BlendSpaceEditorPanel->HasFocusedWindow());
			if (bAnimEditorFocused)
				EditorCommandUI::ProcessShortcuts({ &SaveAll });
			else
				EditorCommandUI::ProcessShortcuts({ &Save, &SaveAll, &Play, &Pause, &Resume, &Copy, &Paste, &Duplicate });
		}

		// F: 선택한 액터로 포커스 (UE). 우클릭 비행 중이거나 글자 입력 중이면 받지 않는다.
		if ((!IsPlaying() || IsSimulatingInEditor()) && !ImGui::GetIO().WantTextInput && !FInputSystem::IsMouseDown(EMouseButton::Right) &&
			FInputSystem::IsKeyPressed(EKeyCode::F) && !FInputSystem::IsKeyDown(EKeyCode::Control))
		{
			FocusViewportOnActor(OutlinerPanel ? OutlinerPanel->GetSelectedActor() : nullptr);
		}
	}
}

void UEditorEngine::DeleteSelectedActors()
{
	// 실행 중 삭제는 게임 로직이 들고 있는 참조를 깨뜨릴 수 있어 Pause·SIE 상태에서만 허용한다 (DeleteActor와 같은 규칙).
	if (Selection.IsEmpty() || (IsPlaying() && !bIsPaused && !IsSimulatingInEditor()))
		return;
	const TArray<AActor*> Targets = Selection.GetActors();
	OutlinerPanel->SelectActor(nullptr);
	DetailsPanel->SelectComponent(nullptr);
	for (AActor* Actor : Targets)
	{
		if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			Actor->Destroy();
	}
}

void UEditorEngine::CopySelectedActors()
{
	if (!FJsonArchive::SaveActorsToBytes(Selection.GetActors(), CopiedActorData))
	{
		HTR_LOG(Warning, "Copy: nothing to copy");
		return;
	}
	HTR_LOG(Info, "Copied {} actor(s)", Selection.GetActors().Num());
}

// 복사한 위치에 그대로 만들고, 새 액터들을 선택해 바로 기즈모로 옮길 수 있게 한다 (UE와 같음).
void UEditorEngine::PasteCopiedActors()
{
	TArray<AActor*> PastedActors;
	if (!FJsonArchive::SpawnActorsFromBytes(EditorWorld, CopiedActorData, PastedActors) || PastedActors.IsEmpty())
	{
		HTR_LOG(Warning, "Paste: failed to create actors");
		return;
	}
	Selection.SelectActors(PastedActors);
	OutlinerPanel->RefreshFromSelection();
	HTR_LOG(Info, "Pasted {} actor(s)", PastedActors.Num());
}

void UEditorEngine::PruneSelection()
{
	if (Selection.IsEmpty())
		return;
	UWorld* World = GetActiveWorld();
	ULevel* Level = World ? World->GetCurrentLevel() : nullptr;

	TArray<AActor*> Alive;
	bool bChanged = false;
	for (AActor* Actor : Selection.GetActors())
	{
		// 레벨 목록에 있는지 포인터로만 확인한 뒤에야 액터를 읽는다.
		const bool bInLevel = Level && Level->GetActors().Find(Actor) != INDEX_NONE;
		if (bInLevel && !Actor->IsActorBeingDestroyed())
			Alive.Add(Actor);
		else
			bChanged = true;
	}
	if (!bChanged)
		return;
	Selection.SelectActors(Alive);
	OutlinerPanel->RefreshFromSelection();
}

void UEditorEngine::RenderSelectionOutlines(const FMatrix& ViewProjection, const FViewportSettings& Viewport)
{
	const AActor* Primary = Selection.GetPrimary();
	for (AActor* Actor : Selection.GetActors())
	{
		if (Actor == Primary)
			continue;   // Primary는 Outline이 이미 그렸다
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (!Primitive)
		{
			for (UActorComponent* Component : Actor->GetComponents())
				if ((Primitive = Cast<UPrimitiveComponent>(Component))) break;
		}
		if (!Primitive)
			continue;
		FOutline Extra;
		Extra.SetTarget(Primitive);
		OutlineRenderer->OnRender(Extra, ViewProjection, Viewport);
	}
}

void UEditorEngine::FocusViewportOnActor(AActor* Actor)
{
	if (!Actor || !ViewportsPanel)
		return;

	FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(ViewportsPanel->GetActiveViewIndex());
	// 게임 카메라를 보여 주는 PIE View는 편집 카메라가 아니므로 옮기지 않는다.
	if (!Client || !Client->IsActive() || IsPlayingView(ViewportsPanel->GetActiveViewIndex()))
		return;

	// 에디터 아이콘(빌보드)은 빼고 보이는 프리미티브 바운드를 합친다. 없으면 액터 위치 주변 1m를 본다.
	bool bHasBounds = false;
	FVector Min, Max;
	for (UActorComponent* Component : Actor->GetComponents())
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
			continue;
		if (const UBillboardComponent* Billboard = Cast<UBillboardComponent>(Primitive); Billboard && Billboard->IsEditorIcon())
			continue;
		const FBox Box = Primitive->CalcBounds();
		if (!bHasBounds)
		{
			Min = Box.Min;
			Max = Box.Max;
			bHasBounds = true;
			continue;
		}
		Min = FVector(std::min(Min.X, Box.Min.X), std::min(Min.Y, Box.Min.Y), std::min(Min.Z, Box.Min.Z));
		Max = FVector(std::max(Max.X, Box.Max.X), std::max(Max.Y, Box.Max.Y), std::max(Max.Z, Box.Max.Z));
	}

	const FVector Center = bHasBounds ? (Min + Max) * 0.5f : Actor->GetActorLocation();
	const float Radius = bHasBounds ? std::max((Max - Min).Length() * 0.5f, 0.1f) : 1.0f;
	Client->FocusOnBounds(Center, Radius);
}

// 패널 요청과 입력을 뷰포트 클라이언트에 전달해 레이아웃과 카메라 상태를 갱신한다.
void UEditorEngine::UpdateViewportState(const float DeltaTime)
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

	int32 WheelDelta = FInputSystem::GetWheelDelta();
	// 우클릭으로 카메라를 잡은 동안 휠은 이동 속도를 바꾼다 (UE, TEAM1). 잡지 않았을 때 휠은 앞뒤 이동(직교는 줌)이다.
	if (CapturedViewportIndex != -1 && WheelDelta != 0)
	{
		const float Steps = static_cast<float>(WheelDelta) / 120.0f;
		if (FEditorViewportClient* CapturedClient = ViewportsPanel->GetViewportClient(CapturedViewportIndex))
		{
			CapturedClient->SetCameraSpeed(CapturedClient->GetCameraSpeed() * std::pow(1.25f, Steps));
			ViewportsPanel->ShowCameraSpeed(CapturedViewportIndex, CapturedClient->GetCameraSpeed());
		}
		WheelDelta = 0;
	}

	for (int32 i = 0; i < 4; ++i)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(i);
		if (!Client || !Client->IsActive())
			continue;

		Client->TickInput(DeltaTime, i == CapturedViewportIndex,
			i == HoveredViewIndex, WheelDelta, 10.0f * Client->GetCameraSpeed(), 0.2f * Client->GetMouseSensitivity());
	}

	// 활성 뷰포트 시점 동기화
	const int32 ActiveIndex = ViewportsPanel->GetActiveViewIndex();
	if (FEditorViewportClient* ActiveClient = ViewportsPanel->GetViewportClient(ActiveIndex))
	{
		if (ACameraActor* MainCam = !PlayWorld && EditorWorld ? EditorWorld->GetMainCamera() : nullptr)
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

// 월드를 한 번 Tick·Capture한 뒤 에디터와 피킹을 갱신한다.
void UEditorEngine::TickWorldAndEditor(const float DeltaTime)
{
	const FEditorSettings& RenderSettings = SettingsPanel->GetSettings();
	const bool bVSSkinningUnavailable = RenderSettings.SkinningMode == ESkinningMode::GPU
		&& RenderSettings.RenderingPath != ERenderingPath::Forward;
	USkeletalMeshComponent::SetSkinningMode(bVSSkinningUnavailable ? ESkinningMode::CPU : RenderSettings.SkinningMode);

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
		EditorUI->Tick(DeltaTime);
	}
	UpdateGizmoAndPicking();
	EditorWorld->GetScene().UpdateAllTransforms();
	if (PlayWorld && bIsPaused)
		PlayWorld->GetScene().UpdateAllTransforms();
}

// 활성 View별 렌더를 수행한다.
void UEditorEngine::RenderViewports()
{
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
			}
		}
	}

	for (int32 ViewIndex = 0; ViewIndex < AllViewportClients.Num(); ++ViewIndex) {
		FEditorViewportClient* ViewClient = AllViewportClients[ViewIndex];
		if (!ViewClient || !ViewClient->IsActive())
			continue;

		const FRect& ViewRect = ViewClient->GetRect();
		if (ViewRect.Width <= 0.0f || ViewRect.Height <= 0.0f)
			continue;

		bool bIsGameView = false;
		int32 TargetPlayerIndex = -1;

		if (bIsPIE && !IsPIEInWindow() && PlayMode == EPlayModeType::InViewport &&
			ViewIndex == PIEStartEditorViewIndex)
		{
			bIsGameView = true;
			TargetPlayerIndex = 0;
		}

		// 선택된 게임 뷰포트와 나머지 에디터 뷰포트는 같은 PIE 월드를 본다.
		UWorld* CurrentWorld = bIsPIE ? PlayWorld : EditorWorld;

		FSceneView SceneView;
		FSceneRenderOptions Options;
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
					BuildSceneRenderOptions(GameClient, Options);

					if (WorldContextlist[WorldIdx].World())
					{
						CurrentWorld = WorldContextlist[WorldIdx].World();
					}

					SceneView = GameClient->CalcSceneView(ViewRect);

					FSceneRenderer SceneRenderer(CurrentWorld, SceneView, GameClient->GetRenderData());
					SceneRenderer.InitViews(Renderer, Options);

					RenderGameFrame(GameClient, SceneView, Options, SceneRenderer, GameClient->IsSIEMode());

					break;
				}
			}
		}
		else
		{
			BuildSceneRenderOptions(ViewClient, bIsGameView, Options);

			SceneView = ViewClient->CalcSceneView(ViewRect);

			FSceneRenderer SceneRenderer(CurrentWorld, SceneView, ViewClient->GetRenderData());
			SceneRenderer.InitViews(Renderer, Options);

			RenderFrame(ViewClient, SceneView, Options, SceneRenderer, CurrentWorld, bIsGameView);
		}
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
				FSceneRenderOptions Options;
				BuildSceneRenderOptions(GameClient, Options);

				FRect ViewRect{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height) };
				FSceneView SceneView = GameClient->CalcSceneView(ViewRect);

				FSceneRenderer SceneRenderer(Context->World(), SceneView, GameClient->GetRenderData());
				SceneRenderer.InitViews(Renderer, Options);

				RenderGameFrame(GameClient, SceneView, Options, SceneRenderer, GameClient->IsSIEMode());

				CopyPIEWindowToBackbuffer();
			}
		}
	}

	UIEditorPanel->RenderPreview(*UIRenderer);
	if (PoseRetargetPanel && PoseRetargetPanel->IsOpen())
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!PoseRetargetPanel->IsViewportVisible(Index)) continue;
			UWorld* World = PoseRetargetPanel->GetWorld(Index);
			FEditorViewportClient& Client = PoseRetargetPanel->GetViewport(Index);
			if (!World || Client.GetWidth() <= 1 || Client.GetHeight() <= 1) continue;
			World->GetScene().UpdateAllTransforms();
			FSceneView View = Client.CalcSceneView();
			FSceneRenderOptions Options;
			BuildSceneRenderOptions(&Client, false, Options);
			Options.bDrawPrimitives = true;
			FSceneRenderer SceneRenderer(World, View, Client.GetRenderData());
			SceneRenderer.InitViews(Renderer, Options);
			RenderScene(Client.GetViewTargets(), View, SceneRenderer, World, Options,
				[this, Index, &View](FSceneOverlayCallbacks& Overlays)
				{
					Overlays.EditorGizmo = [this, Index, &View]
					{
						PoseRetargetPanel->DrawBones(Index, *LineBatcher, View.ViewProjectionMatrix);
					};
				});
		}
	}

	if (SkeletalMeshViewerPanel->IsOpen())
	{
		for (const TUniquePtr<FSkeletalMeshViewer>& Viewer : SkeletalMeshViewerPanel->GetViewers())
		{
			if (Viewer->bOpen && Viewer->bVisible)
				RenderPreviewViewport(&Viewer->Client, Viewer->World, &Viewer->Gizmo, Viewer->Skybox.get(), Viewer->bShowGrid);
		}
	}
	AssetEditorPanel->RenderPreview(this);
	AnimSequenceViewerPanel->RenderPreview(this);
	AnimGraphEditorPanel->RenderPreview(this);
	BlendSpaceEditorPanel->RenderPreview(this);
	AnimMontageEditorPanel->RenderPreview(this);
	AimOffsetEditorPanel->RenderPreview(this);
}

void UEditorEngine::UpdatePIEViewCamera()
{
	if (!PlayWorld || PIEViewIndex == InvalidViewIndex)
		return;
	FWorldContext* Context = GetPIEWorldContext();
	if (!Context || !Context->GameViewport)
		return;
	FGameViewportClient& GameViewport = *Context->GameViewport;
	if (PIEViewIndex >= 0 && PIEViewIndex < AllViewportClients.Num())
	{
		if (FEditorViewportClient* View = AllViewportClients[PIEViewIndex])
		{
			const FRect& GameRect = View->GetRect();
			if (GameRect.Width <= 0.0f || GameRect.Height <= 0.0f) return;
			const FSceneView GameView = GameViewport.CalcSceneView(GameRect);
			View->SetViewLocation(GameView.ViewLocation);
			View->SetViewRotation(GameView.ViewRotation);
			View->SetViewFOV(GameView.FieldOfView);
			View->SetOrthoWidth(GameView.OrthoWidth);
			View->SetIsPerspective(GameView.bIsPerspective);
		}
	}
}

bool UEditorEngine::IsSimulatingInEditor() const
{
	for (const FWorldContext& Context : WorldContextlist)
		if (Context.WorldType == EWorldType::PIE && Context.GameViewport)
			return Context.GameViewport->IsSIEMode();
	return false;
}

// UE의 PIE 커서 조작 (TEAM5): Shift+F1로 게임을 돌린 채 커서를 꺼내고, PIE View를 클릭하면 다시 잠근다.
// 잠긴 동안에는 창 포커스나 View 크기가 바뀌어도 따라가도록 매 프레임 잠금 범위를 맞춘다.
void UEditorEngine::UpdatePIEMouseCapture(FGameViewportClient& GameViewport, bool bMouseOverPIE)
{
	APlayerController* PlayerController = PlayWorld ? PlayWorld->GetPlayerController() : nullptr;
	if (!PlayerController)
	{
		FMouseCapture::Release();
		return;
	}

	if (PlayerController->GetCursorMode() == ECursorMode::Locked)
	{
		bPIEMouseReleased = false;
		const FInputChord ReleaseMouse{ EKeyCode::F1, false, false, true };
		if (!ImGui::GetIO().WantTextInput && ReleaseMouse.IsPressedThisFrame())
		{
			bPIEMouseReleased = true;
			// 스크립트도 커서가 풀린 것을 알도록 컨트롤러 모드를 함께 바꾼다.
			PlayerController->SetCursorMode(ECursorMode::Free);
			return;
		}
		SetGameMouseCapture(PlayWorld, true);
	}
	else if (bPIEMouseReleased && !bIsPaused && !GameViewport.IsSIEMode() &&
		bMouseOverPIE && FInputSystem::IsMousePressed(EMouseButton::Left))
	{
		bPIEMouseReleased = false;
		PlayerController->SetCursorMode(ECursorMode::Locked);
	}
}

bool UEditorEngine::SetGameMouseCapture(const UWorld* InWorld, bool bEnabled)
{
	if (!InWorld || InWorld != PlayWorld || PIEViewIndex == InvalidViewIndex)
		return false;

	// 일시정지 중에는 편집할 수 있게, SIE에서는 에디터 카메라를 움직일 수 있게 커서를 놓는다.
	if (!bEnabled || bIsPaused || IsSimulatingInEditor())
	{
		FMouseCapture::Release();
		return false;
	}

	HWND Window = nullptr;
	RECT Bounds{};
	if (IsPIEInWindow())
	{
		Window = PIEWindow->GetHandle();
		::GetClientRect(Window, &Bounds);
		::MapWindowPoints(Window, nullptr, reinterpret_cast<POINT*>(&Bounds), 2);
	}
	else
	{
		if (!MainWindow || !ViewportsPanel || PIEViewIndex < 0 || PIEViewIndex >= AllViewportClients.Num() || !AllViewportClients[PIEViewIndex])
			return false;
		// 패널 안의 PIE View 영역에만 가둔다.
		const FRect& Rect = AllViewportClients[PIEViewIndex]->GetRect();
		const FVector2 Origin = ViewportsPanel->GetContentScreenOrigin();
		Window = MainWindow->GetHandle();
		Bounds.left = static_cast<LONG>(Origin.X + Rect.X);
		Bounds.top = static_cast<LONG>(Origin.Y + Rect.Y);
		Bounds.right = static_cast<LONG>(Origin.X + Rect.X + Rect.Width);
		Bounds.bottom = static_cast<LONG>(Origin.Y + Rect.Y + Rect.Height);
	}
	return FMouseCapture::Update(Window, Bounds, true);
}

void UEditorEngine::TickPIEInput(float DeltaTime)
{
	FWorldContext* Context = GetPIEWorldContext();
	if (!Context || !Context->GameViewport) return;
	FGameViewportClient& GameViewport = *Context->GameViewport;
	const FVector2 Mouse = ViewportsPanel->GetLocalMousePosition();
	const int32 InputViewIndex = IsPIEInWindow() && GameViewport.IsSIEMode()
		? PIEStartEditorViewIndex : PIEViewIndex;
	const FRect PIEViewRect = (InputViewIndex >= 0 && InputViewIndex < AllViewportClients.Num() && AllViewportClients[InputViewIndex])
		? AllViewportClients[InputViewIndex]->GetRect() : FRect{};
	const bool bMouseOverPIE = Mouse.X >= PIEViewRect.X && Mouse.Y >= PIEViewRect.Y &&
		Mouse.X < PIEViewRect.X + PIEViewRect.Width && Mouse.Y < PIEViewRect.Y + PIEViewRect.Height;
	const int32 ActiveViewIdx = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : -1;
	const bool bEditorSIEFocus = IsPIEInWindow() && GameViewport.IsSIEMode() &&
		::GetForegroundWindow() == MainWindow->GetHandle() &&
		ActiveViewIdx == PIEStartEditorViewIndex && ViewportsPanel->IsHovered() &&
		!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
	const bool bFocused = IsPIEInWindow()
		? ::GetForegroundWindow() == PIEWindow->GetHandle() || bEditorSIEFocus
		: ::GetForegroundWindow() == MainWindow->GetHandle() &&
		ActiveViewIdx == InputViewIndex &&
		ViewportsPanel->IsHovered() &&
		!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
	// ESC는 PIE View 위에 마우스가 없어도 PIE를 끝낸다 (UE와 같음). 이름 편집이나 스켈레탈 뷰어 조작은 제외한다.
	const bool bEditorOwnsInput = ::GetForegroundWindow() == MainWindow->GetHandle() ||
		(IsPIEInWindow() && ::GetForegroundWindow() == PIEWindow->GetHandle());
	const bool bViewerFocused = SkeletalMeshViewerPanel && SkeletalMeshViewerPanel->HasFocuesdViewer();
	if (bEditorOwnsInput && !bViewerFocused && !ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Escape))
	{
		GameViewport.InputKey(nullptr, static_cast<int32>(EKeyCode::Escape), true);
		if (GameViewport.ConsumeExitRequest())
		{
			RequestEndPlayMap();
			return;
		}
	}

	const bool bClickableOverPIE = IsPIEInWindow()
		? ::GetForegroundWindow() == PIEWindow->GetHandle()
		: bMouseOverPIE && ViewportsPanel->IsHovered() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
	UpdatePIEMouseCapture(GameViewport, bClickableOverPIE);

	// Eject/Possess 전환 요청. F8 키와 툴바 버튼이 같은 요청 플래그를 쓰는데, 버튼을 누를 때는 마우스가 툴바 위에 있어서
	// 아래 포커스 조건(뷰포트 위에 마우스)을 채우지 못한다. 먼저 처리하지 않으면 마우스가 뷰포트로 돌아올 때까지 전환이 미뤄진다.
	const auto ProcessSIERequest = [&]()
	{
		if (!GameViewport.ConsumeSIERequest())
			return;
		if (GameViewport.IsSIEMode()) GameViewport.EndSIEMode();
		else GameViewport.BeginSIEMode();
		// SIE에서는 에디터 카메라를 움직이므로 커서를 놓는다. 돌아오면 UpdatePIEMouseCapture가 다시 잠근다.
		if (GameViewport.IsSIEMode())
			FMouseCapture::Release();
		// SIE 전환은 월드를 바꾸지 않으므로 현재 선택과 기즈모를 유지한다(dev/kim).
	};
	ProcessSIERequest();

	if (!bFocused) { GameViewport.LostFocus(); return; }
	if (!IsPIEInWindow() &&
		(FInputSystem::IsMousePressed(EMouseButton::Left) || FInputSystem::IsMousePressed(EMouseButton::Right)) &&
		!bMouseOverPIE)
	{
		GameViewport.LostFocus();
		return;
	}

	// F8 키는 포커스가 있을 때만 요청으로 바뀐다. 같은 프레임에 바로 처리한다.
	if (FInputSystem::IsKeyPressed(EKeyCode::F8))
		GameViewport.InputKey(nullptr, static_cast<int32>(EKeyCode::F8), true);
	ProcessSIERequest();
	constexpr EKeyCode Keys[] = { EKeyCode::W, EKeyCode::A, EKeyCode::S, EKeyCode::D,
		EKeyCode::Q, EKeyCode::E, EKeyCode::F, EKeyCode::Space, EKeyCode::Shift };
	for (EKeyCode Key : Keys)
	{
		if (FInputSystem::IsKeyPressed(Key))
		{
			if (GameViewport.IsSIEMode()) GameViewport.GetKeyInputBySIEMode(static_cast<int32>(Key), true);
			else GameViewport.InputKey(nullptr, static_cast<int32>(Key), true);
		}
		if (FInputSystem::IsKeyReleased(Key))
		{
			if (GameViewport.IsSIEMode()) GameViewport.GetKeyInputBySIEMode(static_cast<int32>(Key), false);
			else GameViewport.InputKey(nullptr, static_cast<int32>(Key), false);
		}
	}

	// UI 히트 테스트용 마우스 좌표 → 게임 렌더 타깃 기준
	if (IsPIEInWindow())
	{
		// PIE 창은 렌더 타깃과 크기가 같고, FInputSystem 좌표가 그 창의 클라이언트 좌표다.
		GameViewport.MouseMove(nullptr, FInputSystem::GetMouseX(), FInputSystem::GetMouseY());
	}
	else if (PIEViewRect.Width > 0.0f && PIEViewRect.Height > 0.0f)
	{
		// Mouse는 뷰포트 패널 기준, PIEViewRect는 그 안의 게임 뷰 영역이다.
		const float U = (Mouse.X - PIEViewRect.X) / PIEViewRect.Width;
		const float V = (Mouse.Y - PIEViewRect.Y) / PIEViewRect.Height;
		GameViewport.MouseMove(nullptr,
			static_cast<int32>(U * GameViewport.GetWidth()),
			static_cast<int32>(V * GameViewport.GetHeight()));
	}

	constexpr EMouseButton MouseButtons[] = { EMouseButton::Left, EMouseButton::Right };
	for (EMouseButton Button : MouseButtons)
	{
		const int32 Key = Button == EMouseButton::Left ? static_cast<int32>(EKeyCode::LButton) : static_cast<int32>(EKeyCode::RButton);
		if (FInputSystem::IsMousePressed(Button))
		{
			if (GameViewport.IsSIEMode()) GameViewport.GetKeyInputBySIEMode(Key, true);
			else GameViewport.InputKey(nullptr, Key, true);
		}
		if (FInputSystem::IsMouseReleased(Button))
		{
			if (GameViewport.IsSIEMode()) GameViewport.GetKeyInputBySIEMode(Key, false);
			else GameViewport.InputKey(nullptr, Key, false);
		}
	}
	const float MouseX = static_cast<float>(FInputSystem::GetMouseDeltaX());
	const float MouseY = static_cast<float>(FInputSystem::GetMouseDeltaY());
	if (GameViewport.IsSIEMode())
	{
		GameViewport.GetAxisInputBySIEMode(EGameInputAxis::MouseX, MouseX);
		GameViewport.GetAxisInputBySIEMode(EGameInputAxis::MouseY, MouseY);
		FEditorViewportClient* Source = ViewportsPanel ? ViewportsPanel->GetViewportClient(PIEStartEditorViewIndex) : nullptr;
		GameViewport.TickSIEInput(DeltaTime, 10.0f * (Source ? Source->GetCameraSpeed() : 1.0f),
			0.2f * (Source ? Source->GetMouseSensitivity() : 1.0f));
	}
	else
	{
		GameViewport.InputAxis(nullptr, 0, EGameInputAxis::MouseX, MouseX, DeltaTime);
		GameViewport.InputAxis(nullptr, 0, EGameInputAxis::MouseY, MouseY, DeltaTime);
	}
}

// 화면을 표시하고 UI 변경 후 View 설정을 보관한다.
void UEditorEngine::EndFrame()
{
	PresentFrame();
	// 메인 Swapchain은 FEngineLoop가 VSync 설정대로 Present하므로 PIE 창은 기다리지 않고 올린다.
	if (bPIEWindowRendered && PIESwapchain)
		PIESwapchain->SwapBuffers(0);
	// UI 변경 후 설정을 복사해 종료 시 카메라 수명에 의존하지 않는다.
	SettingsPanel->CaptureViewportSettings();
	// 프로파일러 반영
	FStatRegistry::EndFrame();
	USkeletalMeshComponent::EndSkinningStatsFrame();
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
	if (PlayWorld && IsSimulatingInEditor() && !IsPIEInWindow() && ViewIndex == PIEViewIndex)
	{
		if (FWorldContext* Context = GetPIEWorldContext())
			if (Context->GameViewport)
				if (UCameraComponent* Camera = Context->GameViewport->GetCameraComponent())
				{
					ViewClient->SetViewLocation(Camera->GetWorldLocation());
					ViewClient->SetViewRotation(Camera->GetWorldRotation());
					ViewClient->SetViewFOV(Camera->GetFieldOfView());
					ViewClient->SetClipPlanes(Camera->GetNearZ(), Camera->GetFarZ());
					ViewClient->SetOrthoWidth(Camera->GetOrthoWidth());
					ViewClient->SetIsPerspective(!Camera->GetIsOrthogonal());
				}
	}

	const FVector2 LocalMousePosition = ViewportsPanel->GetLocalMousePosition();
	ViewClient->UpdateGizmoAndPicking(Gizmo.get(), LocalMousePosition, [this](AActor* SelectedActor) {
		// Shift·Ctrl 클릭은 기존 선택에 더하거나 빼고, 빈 곳을 눌러도 선택을 유지한다 (TEAM1).
		const bool bAdditive = FInputSystem::IsKeyDown(EKeyCode::Shift) || FInputSystem::IsKeyDown(EKeyCode::Control);
		if (!bAdditive)
			OutlinerPanel->SelectActor(SelectedActor);
		else if (SelectedActor)
			OutlinerPanel->ToggleActorSelection(SelectedActor);
		});
}

void UEditorEngine::BuildSceneRenderOptions(FGameViewportClient* ViewClient, FSceneRenderOptions& Options)
{
	if (!SettingsPanel)
	{
		return;
	}

	// Keep PIE bound to the view that started this play session.
	const FEditorViewportClient* Source = ViewportsPanel ? ViewportsPanel->GetViewportClient(PIEStartEditorViewIndex) : nullptr;
	Options.bCacheShadows = SettingsPanel->GetSettings().bCacheShadows;
	Options.PcfMode = SettingsPanel->GetSettings().PcfMode;
	Options.RenderingPath = SettingsPanel->GetSettings().RenderingPath;
	Options.ReflectionModel = SettingsPanel->GetSettings().ReflectionModel;
	Options.ShadingModel = SettingsPanel->GetSettings().ShadingModel;
	Options.LightCullingMode = SettingsPanel->GetSettings().LightCullingMode;
	Options.DirectionalShadowMapSize = SettingsPanel->GetSettings().GetDirectionalShadowMapSize();
	Options.PointShadowMapSize = SettingsPanel->GetSettings().GetPointShadowMapSize();
	Options.SpotShadowMapSize = SettingsPanel->GetSettings().GetSpotShadowMapSize();

	const FEditorSettings& S = SettingsPanel->GetSettings();
	Options.bGammaCorrection = S.bGammaCorrection;
	Options.DisplayGamma = S.DisplayGamma;
	if (Source) Source->ApplyRenderSettings(Options);

	Options.DecalRenderMethod = S.DecalRenderMethod;
}

void UEditorEngine::BuildSceneRenderOptions(FEditorViewportClient* ViewClient, bool IsPIE, FSceneRenderOptions& Options)
{
	if (!SettingsPanel)
	{
		return;
	}

	const FViewShowFlags& ShowFlags = ViewClient->GetShowFlags();
	Options.bCacheShadows = SettingsPanel->GetSettings().bCacheShadows;
	Options.PcfMode = SettingsPanel->GetSettings().PcfMode;
	Options.RenderingPath = SettingsPanel->GetSettings().RenderingPath;
	Options.LightCullingMode = SettingsPanel->GetSettings().LightCullingMode;
	Options.DirectionalShadowMapSize = SettingsPanel->GetSettings().GetDirectionalShadowMapSize();
	Options.PointShadowMapSize = SettingsPanel->GetSettings().GetPointShadowMapSize();
	Options.SpotShadowMapSize = SettingsPanel->GetSettings().GetSpotShadowMapSize();

	const FEditorSettings& S = SettingsPanel->GetSettings();
	Options.bGammaCorrection = S.bGammaCorrection;
	Options.DisplayGamma = S.DisplayGamma;

	Options.bDrawPrimitives = IsPIE || ShowFlags.bPrimitives;
	const EViewportMode ViewMode = ViewClient->GetViewportMode();
	Options.bEnableFog = (ViewMode == EViewportMode::Solid || ViewMode == EViewportMode::Unlit) && ShowFlags.bFog;
	if (!IsPIE && ViewMode == EViewportMode::Unlit)
		Options.bUnlit = true;
	Options.bShowSceneDepth = !IsPIE && ViewClient->IsSceneDepth();
	Options.bShowCascade = !IsPIE && ViewClient->IsCascade();
	Options.bShowShadowMask = !IsPIE && ViewClient->IsShadowMask();
	Options.bShowWorldNormal = !IsPIE && ViewClient->IsWorldNormal();
	Options.bShowLightCulling = !IsPIE && ViewClient->IsLightCulling();

	Options.DecalRenderMethod = S.DecalRenderMethod;
	ViewClient->ApplyRenderSettings(Options);
}

// 게임 뷰포트 화면을 렌더링한다
void UEditorEngine::RenderGameFrame(FGameViewportClient* GameClient, const FSceneView& SceneView, const FSceneRenderOptions& RenderOptions, FSceneRenderer& SceneRenderer, bool bIsSIEMode)
{
	if (!GameClient) return;

	FSceneViewTargets& Targets = GameClient->GetViewTargets();
	UWorld* TargetWorld = GameClient->GetWorld();
	const bool bRendered = RenderScene(Targets, SceneView, SceneRenderer, TargetWorld, RenderOptions,
		[this, GameClient, &Targets, &SceneView, bIsSIEMode](FSceneOverlayCallbacks& Overlays)
		{
			const uint32 Width = Targets.SceneColor->GetWidth();
			const uint32 Height = Targets.SceneColor->GetHeight();
			Overlays.GameUI = [this, GameClient, Width, Height]
				{
					if (!UIRenderer) return;
					GameClient->PaintUI(UIDrawList);
					UIRenderer->Render(UIDrawList, Width, Height);
				};

			if (!bIsSIEMode) return;

			Overlays.EditorLines = [this, &SceneView, GameClient]
				{
					LineBatcher->BeginFrame();
					DrawCollisionShapes(GameClient->GetWorld(), LineBatcher.get(), nullptr);
					DrawSpringArms(GameClient->GetWorld(), LineBatcher.get(), nullptr);
					DrawDecalVolume(GameClient->GetWorld(), LineBatcher.get(), nullptr);
					LineBatcher->OnRender(SceneView.ViewProjectionMatrix);
				};
			if (!bIsSIEMode) return;
			const FViewportSettings ViewportSetting{ 0, 0, Targets.SceneColor->GetWidth(),
				Targets.SceneColor->GetHeight(), 0.0f, 1.0f };
			if (Outline->GetTarget())
				Overlays.SIEOutline = [this, &SceneView, ViewportSetting]
				{
					OutlineRenderer->OnRender(*Outline, SceneView.ViewProjectionMatrix, ViewportSetting);
				};
			if (Gizmo->GetTarget())
				Overlays.SIEGizmo = [this, &SceneView]
				{
					GizmoRenderer->OnRender(*Gizmo, SceneView.ViewProjectionMatrix,
						SceneView.ViewLocation, !SceneView.bIsPerspective);
				};
		});
	if (!bRendered) return;
}

bool UEditorEngine::RenderScene(FSceneViewTargets& Targets, const FSceneView& SceneView,
	FSceneRenderer& SceneRenderer, UWorld* TargetWorld, const FSceneRenderOptions& Options,
	const std::function<void(FSceneOverlayCallbacks&)>& BuildOverlays, FSkyboxRenderer* SkyboxOverride)
{
	return SceneRenderer.Render(Renderer, Targets, Options,
		[&](FScenePassCallbacks& Callbacks)
		{
			Callbacks.SkyboxOverride = SkyboxOverride;
			FTexture2D* ColorTarget = Targets.SceneColor.get();
			FTexture2D* DepthTarget = Targets.Depth.get();
			if (SceneDepthRenderer)
				Callbacks.DepthVisualization = [this, &SceneView, DepthTarget, ColorTarget, Range = Options.SceneDepthRange]
				{
					SceneDepthRenderer->OnRender(SceneView, DepthTarget, ColorTarget, Range);
				};
			if (Options.bShowCascade && CascadeFloorRenderer)
				Callbacks.CascadeVisualization = [this, &SceneView, &SceneRenderer, DepthTarget, ColorTarget]
				{
					CascadeFloorRenderer->OnRender(SceneView, SceneRenderer.GetRenderData(), DepthTarget, ColorTarget);
				};
			if (BuildOverlays) BuildOverlays(Callbacks.Overlays);
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

void UEditorEngine::RenderActorUUIDs(const FSceneView& SceneView)
{
	const bool bOrtho = !SceneView.bIsPerspective;
	UWorld* TargetWorld = PlayWorld ? PlayWorld : EditorWorld;

	for (AActor* Actor : TargetWorld->GetPersistentLevel()->GetActors())
	{
		if (!Actor)
			continue;
		USceneComponent* Root = Actor->GetRootComponent();

		if (!Root)
			continue;

		FBox Box = Root->CalcBounds();
		FVector UUIDLoc((Box.Min.X + Box.Max.X) * 0.5f,
			(Box.Min.Y + Box.Max.Y) * 0.5f, Box.Max.Z + 0.5f);

		FString Text = "UUID : " + std::to_string(Actor->GetUUID());
		TextRenderer->BuildTextMesh(Text, 0.5f, *SystemFont);

		FVector Facing = bOrtho ? (SceneView.ViewForward * -1.0f)
			: (SceneView.ViewLocation - UUIDLoc).Normalized();
		FVector Up = FVector(0.0f, 0.0f, 1.0f);
		FVector Right = FVector::Cross(Up, Facing).Normalized();
		FVector RealUp = FVector::Cross(Facing, Right).Normalized();
		FMatrix BillboardWorld = FMatrix::Identity;
		BillboardWorld.M[0][0] = Facing.X;
		BillboardWorld.M[0][1] = Facing.Y;
		BillboardWorld.M[0][2] = Facing.Z;
		BillboardWorld.M[1][0] = Right.X;
		BillboardWorld.M[1][1] = Right.Y;
		BillboardWorld.M[1][2] = Right.Z;
		BillboardWorld.M[2][0] = RealUp.X;
		BillboardWorld.M[2][1] = RealUp.Y;
		BillboardWorld.M[2][2] = RealUp.Z;
		BillboardWorld.M[3][0] = UUIDLoc.X;
		BillboardWorld.M[3][1] = UUIDLoc.Y;
		BillboardWorld.M[3][2] = UUIDLoc.Z;

		TextRenderer->OnRender(Text, BillboardWorld, 0.5f, *SystemFont,
			SceneView.ViewProjectionMatrix);
	}
}

// 뷰포트 하나의 씬과 에디터 요소를 렌더링한다
void UEditorEngine::RenderFrame(FEditorViewportClient* ViewClient,
	const FSceneView& SceneView,
	const FSceneRenderOptions& Options,
	FSceneRenderer& SceneRenderer,
	UWorld* TargetWorld,
	const bool bIsPIE)
{
	if (!ViewClient || !SettingsPanel) return;

	FSceneViewTargets& Targets = ViewClient->GetViewTargets();
	const bool bRendered = RenderScene(Targets, SceneView, SceneRenderer, TargetWorld, Options,
		[this, ViewClient, &SceneView, &Targets, bIsPIE, TargetWorld](FSceneOverlayCallbacks& Overlays)
		{
			if (bIsPIE)
			{
				// PIE에서는 그리드, 기즈모 없이 충돌 도형만 그린다
				Overlays.EditorLines = [this, &SceneView, TargetWorld]
					{
						LineBatcher->BeginFrame();
						DrawCollisionShapes(TargetWorld, LineBatcher.get(), nullptr);
						DrawSpringArms(TargetWorld, LineBatcher.get(), nullptr);
						LineBatcher->OnRender(SceneView.ViewProjectionMatrix);
					};
				return;
			}
			const FViewportSettings ViewportSetting{ 0, 0, Targets.SceneColor->GetWidth(),Targets.SceneColor->GetHeight(), 0.0f, 1.0f };
			const bool bIsActiveViewport = ViewportsPanel && SceneView.ViewIndex == ViewportsPanel->GetActiveViewIndex();

			const bool bDrawSelectedLight = bIsActiveViewport && Gizmo->GetTarget() &&
				(Cast<ASpotLightActor>(Gizmo->GetTarget()->GetOwner()) ||
					Cast<APointLightActor>(Gizmo->GetTarget()->GetOwner()) ||
					Cast<ARectLightActor>(Gizmo->GetTarget()->GetOwner()));

			const bool bDrawCascadeBounds = ViewClient->GetShowFlags().bCascadeBounds;
			{
				Overlays.EditorLines = [this, ViewClient, &SceneView, bDrawSelectedLight, bDrawCascadeBounds]
					{
						LineBatcher->BeginFrame();
						if (bDrawCascadeBounds)
						{
							FSceneRenderData& Data = ViewClient->GetRenderData();
							const FVector4 Colors[4] = {
								FVector4(1.f, 0.2f, 0.2f, 1.f),
								FVector4(1.f, 0.8f, 0.2f, 1.f),
								FVector4(0.2f, 1.f, 0.4f, 1.f),
								FVector4(0.2f, 0.6f, 1.f, 1.f)
							};
							const FBox ClipBox{ FVector(-1.f, -1.f, 0.f), FVector(1.f, 1.f, 1.f) };
							// 광원의 첫 그림자 뷰(VisibleLightShadowViews)부터 cascade 순서로 들어 있다. 그림자가 없는 방향광은 건너뛴다.
							const uint32 DirectionalOffset = Data.GetDirectionalLightOffset();
							for (uint32 LightIndex = 0; LightIndex < Data.GetDirectionalLightCount(); ++LightIndex)
							{
								const uint32 VisibleIndex = DirectionalOffset + LightIndex;
								if (VisibleIndex >= static_cast<uint32>(Data.VisibleLightShadowViews.Num())) continue;
								const uint32 FirstCascade = Data.VisibleLightShadowViews[VisibleIndex];
								if (FirstCascade == FSceneRenderData::InvalidShadowView) continue;
								for (uint32 CascadeIndex = 0; CascadeIndex < Data.CascadeShadowCount; ++CascadeIndex)
								{
									const uint32 ShadowIndex = FirstCascade + CascadeIndex;
									if (ShadowIndex >= static_cast<uint32>(Data.LightShadows.Num())) break;
									LineBatcher->AddOrientedBox(ClipBox, Data.LightShadows[ShadowIndex].ViewProjection.Inverse(), Colors[CascadeIndex]);
								}
							}
						}
						if (ViewClient->GetShowFlags().bBounds)
						{
							LineBatcher->BuildVertexBuffer();
						}
						if (bDrawSelectedLight)
						{
							if (ASpotLightActor* Light = Cast<ASpotLightActor>(Gizmo->GetTarget()->GetOwner()))
								Light->GetSpotLightComponent()->DrawDebug(LineBatcher.get());
							else if (APointLightActor* Light = Cast<APointLightActor>(Gizmo->GetTarget()->GetOwner()))
								Light->GetPointLightComponent()->DrawDebug(LineBatcher.get());
							else if (ARectLightActor* Light = Cast<ARectLightActor>(Gizmo->GetTarget()->GetOwner()))
								Light->GetRectLightComponent()->DrawDebug(LineBatcher.get());
						}

						const AActor* SelectedActor =
							Gizmo->GetTarget() ? Gizmo->GetTarget()->GetOwner() : nullptr;

						DrawCollisionShapes(EditorWorld, LineBatcher.get(), SelectedActor);
						DrawSpringArms(EditorWorld, LineBatcher.get(), SelectedActor);
						DrawDecalVolume(EditorWorld, LineBatcher.get(), SelectedActor);

						LineBatcher->OnRender(SceneView.ViewProjectionMatrix);
					};
			}
			if (ViewClient->GetShowFlags().bGrid)
			{
				Overlays.EditorGrid = [this, ViewClient, &SceneView, ViewportSetting]
					{
						EGridPlane GridPlane = EGridPlane::XY;
						switch (ViewClient->GetViewportType())
						{
						case ELevelViewportType::Front: GridPlane = EGridPlane::YZ; break;
						case ELevelViewportType::Right: GridPlane = EGridPlane::XZ; break;
						default: break;
						}
						if (SceneView.bIsPerspective)
							GridRenderer->OnRenderPSGrid(SceneView.ViewProjectionMatrix, SceneView.ViewLocation,
								SettingsPanel->GetSettings(), ViewportSetting);
						else
							GridRenderer->OnRenderBatchGrid(SceneView.ViewProjectionMatrix, SceneView.ViewLocation,
								SceneView.ViewForward, GridPlane,
								static_cast<float>(SettingsPanel->GetSettings().GridSpacing),
								SceneView.bIsPerspective, ViewportSetting);
					};
			}
			if (bIsActiveViewport && (Outline->GetTarget() || Selection.GetActors().Num() > 1))
				Overlays.EditorOutline = [this, &SceneView, ViewportSetting]
				{
					OutlineRenderer->OnRender(*Outline, SceneView.ViewProjectionMatrix, ViewportSetting);
					RenderSelectionOutlines(SceneView.ViewProjectionMatrix, ViewportSetting);
				};
			if (bIsActiveViewport && Gizmo->GetTarget())
				Overlays.EditorGizmo = [this, &SceneView]
				{
					GizmoRenderer->OnRender(*Gizmo, SceneView.ViewProjectionMatrix,
						SceneView.ViewLocation, !SceneView.bIsPerspective);
				};
			Overlays.bClearEditorDepth = true;
			if (ViewClient->GetShowFlags().bUUID)
				Overlays.EditorUUID = [this, &SceneView] { RenderActorUUIDs(SceneView); };
		});
	if (!bRendered) return;
}

void UEditorEngine::RenderPreviewViewport(FEditorViewportClient* PreviewClient, UWorld* PreviewWorld, const FGizmo* PreviewGizmo,
	FSkyboxRenderer* SkyboxOverride, bool bDrawGrid, const UPrimitiveComponent* BoundsComponent)
{
	if (!PreviewClient || !PreviewWorld || PreviewClient->GetWidth() == 0 || PreviewClient->GetHeight() == 0)
		return;

	PreviewWorld->GetScene().UpdateAllTransforms();

	FSceneRenderOptions Options;
	BuildSceneRenderOptions(PreviewClient, false, Options);
	Options.bDrawPrimitives = true;

	const FSceneView SceneView = PreviewClient->CalcSceneView();
	FSceneRenderer SceneRenderer(PreviewWorld, SceneView, PreviewClient->GetRenderData());
	SceneRenderer.InitViews(Renderer, Options);

	FSceneViewTargets& Targets = PreviewClient->GetViewTargets();
	RenderScene(Targets, SceneView, SceneRenderer, PreviewWorld, Options, [this, &SceneView, &Targets, PreviewGizmo, bDrawGrid, PreviewWorld, BoundsComponent](FSceneOverlayCallbacks& Overlays)
		{
			Overlays.EditorLines = [this, &SceneView, PreviewWorld, BoundsComponent]
			{
				LineBatcher->BeginFrame();
				for (TObjectIterator<USkeletalMeshDebugComponent> It; It; ++It)
					if (It->GetWorld() == PreviewWorld && It->ShouldDrawDebug()) It->DrawDebug(LineBatcher.get());
				if (BoundsComponent)
				{
					LineBatcher->AddBox(BoundsComponent->CalcBounds(), FVector4(1.0f, 0.85f, 0.15f, 1.0f));
				}
				LineBatcher->OnRender(SceneView.ViewProjectionMatrix);
			};
			if (bDrawGrid)
			{
				const FViewportSettings ViewportSetting{ 0, 0, Targets.SceneColor->GetWidth(), Targets.SceneColor->GetHeight(), 0.0f, 1.0f };
			Overlays.EditorGrid = [this, &SceneView, ViewportSetting]
				{
					GridRenderer->OnRenderBatchGrid(
						SceneView.ViewProjectionMatrix,
						SceneView.ViewLocation,
						SceneView.ViewForward,
						EGridPlane::XY,
						static_cast<float>(SettingsPanel->GetSettings().GridSpacing),
						SceneView.bIsPerspective,
						ViewportSetting
					);
				};
			}

			if (PreviewGizmo && PreviewGizmo->GetTarget())
			{
				Overlays.EditorGizmo = [this, &SceneView, PreviewGizmo]
					{
						GizmoRenderer->OnRender(
							*PreviewGizmo, 
							SceneView.ViewProjectionMatrix, 
							SceneView.ViewLocation, 
							!SceneView.bIsPerspective
						);
					};
			}
		},
		SkyboxOverride);
}

// View Texture가 포함된 UI를 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
void UEditorEngine::PresentFrame()
{
	// Swapchain 렌더링
	RenderCommand::BeginRenderPass(MainWindowSC->GetBackbuffer(), nullptr,
		MainWindow ? MainWindow->GetWidth() : 0,
		MainWindow ? MainWindow->GetHeight() : 0);

	{
		SCOPE_CYCLE_COUNTER(STAT_ImGui);

		ImGuiRenderer->Begin();

		EditorUI->OnRender();

		ImGuiRenderer->End();
	}

	RenderCommand::EndRenderPass();
}

void UEditorEngine::DrawPIEWindows()
{
}

// ImGui를 정리한다. UObject 일괄 삭제와 공용 자원·Device 정리는 FEngineLoop가 이어서 한다.
void UEditorEngine::PreExit()
{
	EndPlayMap();
	// 시퀀서는 에디터 월드 액터를 가리키므로 월드보다 먼저 정리한다
	if (SequencerPanel) SequencerPanel->Shutdown();
	if (PoseRetargetPanel) PoseRetargetPanel->Release();
	if (SkeletalMeshViewerPanel) SkeletalMeshViewerPanel->Release();
	if (AnimSequenceViewerPanel) AnimSequenceViewerPanel->Release();
	if (AnimGraphEditorPanel) AnimGraphEditorPanel->Release();
	if (BlendSpaceEditorPanel) BlendSpaceEditorPanel->Release();
	if (AnimMontageEditorPanel) AnimMontageEditorPanel->Release();
	if (AimOffsetEditorPanel) AimOffsetEditorPanel->Release();
	if (ContentDrawerPanel) ContentDrawerPanel->Release();
	if (AssetEditorPanel) AssetEditorPanel->Release();
	EditorWorld->ClearWorld();
	// PIE 창이 열린 채 종료하면 Device가 살아 있을 때 Swapchain과 타깃을 먼저 정리한다.
	DestroyPIEWindow();
	ImGuiRenderer->Shutdown();
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

void UEditorEngine::StartPlayInEditorSession()
{
	// 별도 창 모드면 패널 View는 모두 에디터 월드로 두고 창 하나에만 PIE 월드를 그린다.
	// 창을 만들지 못하면 선택한 View에서 실행한다.
	const bool bNewWindow = (PlayMode == EPlayModeType::InEditorFloating);

	PIEStartEditorViewIndex = ViewportsPanel ? ViewportsPanel->GetActiveViewIndex() : 0;
	if (bNewWindow && CreatePIEWindow())
		PIEViewIndex = -2;
	else
		// 지금 편집 중인 View 하나만 PIE 월드를 보여준다.
		PIEViewIndex = PIEStartEditorViewIndex;

	FWorldContext& PIEContext = CreateNewWorldContext(EWorldType::PIE);
	PIEContext.PIEInstance = 0;
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	GameInstance->SetWorldContext(&PIEContext);
	if (!GameInstance->InitializeForPlayInEditor(0) ||
		!GameInstance->StartPlayInEditorGameInstance())
	{
		UWorld* FailedWorld = PIEContext.World();
		DestroyWorldContext(FailedWorld);
		PIEViewIndex = InvalidViewIndex;
		PIEStartEditorViewIndex = InvalidViewIndex;
		DestroyPIEWindow();
		return;
	}
	PlayWorld = GameInstance->GetWorld();
	if (PIEContext.GameViewport) PIEContext.GameViewport->SetAllowCameraOverlayDebug(true);
	if (!bNewWindow && PIEContext.GameViewport && ViewportsPanel)
	{
		ViewportsPanel->SetGameViewportClient(PIEStartEditorViewIndex, PIEContext.GameViewport.get());
	}

	HTR_LOG(Info, "[PIE] Started ({})", bNewWindow && IsPIEInWindow() ? "new window" : "viewport");
	OnActiveWorldChanged();
}

UWorld* UEditorEngine::CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld)
{
	UWorld* NewPIEWorld = UWorld::GetDuplicatedWorldForPIE(InEditorWorld);
	if (!NewPIEWorld) return nullptr;
	const int32 StartView = PIEStartEditorViewIndex;
	if (StartView >= 0 && StartView < AllViewportClients.Num() && AllViewportClients[StartView] && NewPIEWorld->GetMainCamera())
	{
		FEditorViewportClient* Source = AllViewportClients[StartView];
		UCameraComponent* Camera = NewPIEWorld->GetMainCamera()->GetCameraComponent();
		Camera->SetWorldLocationAndRotation(Source->GetViewLocation(), Source->GetViewRotation(), false);
		Camera->SetFieldOfView(Source->GetViewFOV());
		Camera->SetNearZ(Source->GetNearClip());
		Camera->SetFarZ(Source->GetFarClip());
		Camera->SetOrthoWidth(Source->GetOrthoWidth());
		Camera->SetIsOrthogonal(!Source->IsPerspective());
	}
	PIEContext.SetCurrentWorld(NewPIEWorld);
	return NewPIEWorld;
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

void UEditorEngine::EndPlayMap()
{
	if (!PlayWorld)
		return;
	bIsPaused = false;
	bStepRequested = false;
	bPIEMouseReleased = false;
	FMouseCapture::Release();
	HTR_LOG(Info, "[PIE] Ended");

	ResetSceneSelection();
	if (ViewportsPanel)
		ViewportsPanel->ClearGameViewportClient();
	DestroyWorldContext(PlayWorld);
	PlayWorld = nullptr;
	PIEViewIndex = InvalidViewIndex;
	PIEStartEditorViewIndex = InvalidViewIndex;
	DestroyPIEWindow();
	OnActiveWorldChanged();
}

bool UEditorEngine::CreatePIEWindow()
{
	constexpr int32 DefaultWidth = 1280;
	constexpr int32 DefaultHeight = 720;

	TUniquePtr<FWindow> NewWindow = MakeUnique<FWindow>();
	if (!NewWindow->Create(::GetModuleHandleW(nullptr), DefaultWidth, DefaultHeight, L"Hitori Engine - PIE"))
	{
		HTR_LOG(Error, "Failed to create PIE window. Falling back to the selected viewport.");
		return false;
	}
	// 닫기 버튼은 PIE만 끝내고, ImGui는 메인 창 입력만 받도록 훅을 끊는다.
	NewWindow->SetQuitOnClose(false);
	NewWindow->SetReceivesWndProcHook(false);

	PIEWindow = std::move(NewWindow);
	PIESwapchain = MakeUnique<FSwapchain>(GetEngineLoop().GetRenderDevice(), PIEWindow.get());
	PIETargetWidth = 0;
	PIETargetHeight = 0;
	PIEWindow->Show();
	return true;
}

void UEditorEngine::DestroyPIEWindow()
{
	if (!PIEWindow)
		return;

	PIETargetWidth = 0;
	PIETargetHeight = 0;
	bPIEWindowRendered = false;

	// Swapchain이 창 핸들을 출력 대상으로 쥐고 있으므로 창보다 먼저 놓는다.
	PIESwapchain.reset();
	PIEWindow->Destroy();
	PIEWindow.reset();

	// PIE 창이 포커스를 갖고 있었다면 에디터로 돌려준다.
	::SetForegroundWindow(MainWindow->GetHandle());
}

bool UEditorEngine::UpdatePIEWindowTargets()
{
	const uint32 Width = PIEWindow->GetWidth();
	const uint32 Height = PIEWindow->GetHeight();
	// 최소화 중에는 그릴 대상이 없다.
	if (Width == 0 || Height == 0 || ::IsIconic(PIEWindow->GetHandle()))
	{
		return false;
	}

	if (PIEWindow->CheckResized())
		PIESwapchain->Resize(Width, Height);

	if (Width != PIETargetWidth || Height != PIETargetHeight)
	{
		FWorldContext* Context = GetPIEWorldContext();
		if (!Context || !Context->GameViewport) return false;
		Context->GameViewport->Resize(Width, Height);
		if (!Context->GameViewport->GetViewTargets().IsValidFor(Width, Height))
		{
			return false;
		}

		PIETargetWidth = Width;
		PIETargetHeight = Height;
	}

	return true;
}

void UEditorEngine::CopyPIEWindowToBackbuffer()
{
	if (!PIESwapchain)
		return;

	FTexture2D* Backbuffer = PIESwapchain->GetBackbuffer();
	FWorldContext* Context = GetPIEWorldContext();
	FTexture2D* PIEColorTarget = Context && Context->GameViewport
		? Context->GameViewport->GetColorTarget() : nullptr;
	if (!Backbuffer || !PIEColorTarget ||
		Backbuffer->GetWidth() != PIETargetWidth ||
		Backbuffer->GetHeight() != PIETargetHeight)
		return;

	RenderCommand::CopyTexture(Backbuffer, PIEColorTarget);
	bPIEWindowRendered = true;
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

// 씬과 열려 있는 애니메이션 에디터의 저장하지 않은 변경을 모두 저장한다.
void UEditorEngine::SaveAllAssets()
{
	if (!IsPlaying())
	{
		SaveCurrentScene();
	}
	if (AnimSequenceViewerPanel)
	{
		AnimSequenceViewerPanel->SaveIfDirty();
	}
	if (AnimGraphEditorPanel)
	{
		AnimGraphEditorPanel->SaveIfDirty();
	}
	if (BlendSpaceEditorPanel)
	{
		BlendSpaceEditorPanel->SaveIfDirty();
	}
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
