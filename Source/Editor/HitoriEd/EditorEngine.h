#pragma once

#include "Engine/Engine.h"
#include "Core/Window.h"
#include "Core/Types.h"

#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Render/RenderDevice.h"
#include "Render/Swapchain.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Render/LineBatcher.h"
#include "Editor/Rendering/SceneDepthRenderer.h"
#include "Editor/Rendering/CascadeFloorRenderer.h"
#include "Render/SceneRenderOptions.h"
#include "Render/SceneViewTargets.h"
#include "Render/RenderGraph/RenderGraph.h"


#include "Editor/EditorUI/EditorUI.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/EditorControls/EditorControlsPanel.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Editor/Sequence/SequencerPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/LevelViewportLayout.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/ParticleEditor/ParticleSystemEditorPanel.h"
#include "Editor/UIEditor/UIEditorPanel.h"
#include "Editor/SkeletalMeshEditor/Retarget/PoseRetargetPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshViewer.h"
#include "Editor/Animation/AnimSequenceViewerPanel.h"
#include "Editor/Animation/AnimGraphEditorPanel.h"
#include "Editor/Animation/BlendSpaceEditorPanel.h"
#include "Editor/Animation/AnimationAssetEditorPanels.h"

#include "Editor/Rendering/Outline.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorSelection.h"

#include "PlayInEditorDataTypes.h"
#include "Core/Misc/Optional.h"

#include "Text/TextRenderer.h"

#include "Editor/HitoriEd/GamePackager.h"

#include <Engine\GameViewportClient.h>

#include "UI/UIRenderer.h"
#include "UI/UIDrawList.h"

#include "EditorEngine.generated.h"

inline constexpr int32 InvalidViewIndex = -1;

UCLASS()
class UEditorEngine : public UEngine
{
	GENERATED_BODY()
public:
	FEngineConfig GetConfig() const override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void PreExit() override;

	// Active View의 입력과 Picking 결과만 Gizmo 및 선택 상태에 반영한다.
	void UpdateGizmoAndPicking();
	// 뷰포트 하나의 씬과 에디터 요소를 렌더링한다.
	void RenderFrame(FEditorViewportClient* ViewClient, const FSceneView& SceneView, const FSceneRenderOptions& RenderOptions, FSceneRenderer& SceneRenderer, UWorld* TargetWorld, const bool bIsPIE);
	// FBX Viewer용 뷰포트 화면을 렌더링한다.
	void RenderPreviewViewport(FEditorViewportClient* PreviewClient, UWorld* PreviewWorld, const FGizmo* PreviewGizmo,
		class FSkyboxRenderer* SkyboxOverride = nullptr, bool bDrawGrid = false, const class UPrimitiveComponent* BoundsComponent = nullptr);
	// 게임 뷰포트 화면을 렌더링한다.
	void RenderGameFrame(FGameViewportClient* GameClient, const FSceneView& SceneView, const FSceneRenderOptions& RenderOptions, FSceneRenderer& SceneRenderer, bool bIsSIEMode = false);
	// 네 View 결과와 ImGui를 메인 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
	void PresentFrame();
	void DeleteActor(AActor* Actor);
	void DeleteComponent(UActorComponent* Component);
	// 활성 편집 View의 카메라를 액터 바운드가 꽉 차게 옮긴다 (F, 아웃라이너 더블 클릭)
	void FocusViewportOnActor(AActor* Actor);
	// 선택한 액터를 모두 지운다 (Delete)
	void DeleteSelectedActors();
	// Ctrl+C / Ctrl+V / Ctrl+D (TEAM1). 씬 저장과 같은 형식으로 복사하고, 붙여넣은 액터들을 선택한다.
	void CopySelectedActors();
	void PasteCopiedActors();
	// 지워졌거나 다른 월드로 바뀐 액터를 선택에서 뺀다. 포인터 비교만 하므로 해제된 액터를 건드리지 않는다.
	void PruneSelection();
	// 선택한 액터마다 외곽선을 그린다. Primary는 Outline이 그린다.
	void RenderSelectionOutlines(const FMatrix& ViewProjection, const FViewportSettings& Viewport);

	//Play 버튼을 눌렀을때 Play Session 실행을 요청한다.
	inline void RequestPlaySession() { bPlaySessionRequested = true; }
	inline void RequestEndPlayMap() { bRequestEndPlayMapQueued = true; bIsPaused = false; }

	//실제 PIE 를 실행
	void StartPlayInEditorSession();
	UWorld* CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld);

	void OnActiveWorldChanged();

	void EndPlayMap();

	// PIE View(또는 PIE 창)에 커서를 가둔다. 일시정지·SIE 중이면 놓는다.
	bool SetGameMouseCapture(const UWorld* InWorld, bool bEnabled) override;

	bool IsPlaying() const { return PlayWorld != nullptr; }
	EPIEState GetPIEState() const
	{
		if (!PlayWorld) return EPIEState::Stopped;
		if (bIsPaused) return EPIEState::Paused;
		return IsSimulatingInEditor() ? EPIEState::Simulating : EPIEState::Playing;
	}

	////FEditorDelegates::PrePIEEnded / EndPIE.Broadcast()
	//void TeardownPlaySession(const FWorldContext& PIEContext);            // 액터 EndPlay, 월드 정리(CleanupWorld)
	//void RestoreEditorWorld(UWorld* EditorWorld);            // GWorld를 에디터 월드로 복구
	//void DestroyWorldContext(UWorld* PlayWorld);

protected:
	void PreLoadMap(FWorldContext& Context) override;
	void PostLoadMap(FWorldContext& Context) override;
	void RefreshImportedAssets();

private:
	// 이번 프레임 DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
	void BeginFrame(float DeltaTime);
	// 패널 요청과 입력을 적용해 레이아웃·카메라 상태를 갱신한다.
	void UpdateViewportState(float DeltaTime);
	// 월드를 정확히 한 번 Tick·Capture한 뒤 에디터 상호작용을 갱신한다.
	void TickWorldAndEditor(float DeltaTime);
	// 한 번 캡처한 월드 결과를 재사용해 현재 레이아웃의 각 View를 렌더한다.
	void RenderViewports();
	// PIE View를 게임 카메라 상태와 동기화한다.
	void UpdatePIEViewCamera();
	void TickPIEInput(float DeltaTime);

	// 별도 PIE 창(New Editor Window)을 만들고 정리한다.
	bool CreatePIEWindow();
	void DestroyPIEWindow();
	// 창 크기에 맞춰 Swapchain과 오프스크린 타깃을 다시 만든다. 그릴 수 없으면 false다.
	bool UpdatePIEWindowTargets();
	// PIE 창의 오프스크린 결과를 백버퍼로 복사한다. Present는 EndFrame에서 한다.
	void CopyPIEWindowToBackbuffer();
	bool IsPIEInWindow() const { return PIEWindow != nullptr; }
	// 화면 합성과 View 설정 보관으로 프레임을 마무리한다.
	void EndFrame();

	void BuildSceneRenderOptions(FGameViewportClient* ViewClient, FSceneRenderOptions& Options);
	void BuildSceneRenderOptions(FEditorViewportClient* ViewClient, bool IsPIE, FSceneRenderOptions& Options);

	bool RenderScene(FSceneViewTargets& Targets, const FSceneView& SceneView,
		FSceneRenderer& SceneRenderer, UWorld* TargetWorld, const FSceneRenderOptions& Options,
		const std::function<void(FSceneOverlayCallbacks&)>& BuildOverlays,
		class FSkyboxRenderer* SkyboxOverride = nullptr);
	void RenderActorUUIDs(const FSceneView& View);

	FWorldContext& GetEditorWorldContext();
	FWorldContext* GetPIEWorldContext(int32 WorldPIEInstance = 0);
	UWorld* GetActiveWorld() const { return PlayWorld ? PlayWorld : EditorWorld; }

	// PIE 중이고 일시정지가 아닌 View는 Gizmo·Picking 등 에디터 상호작용을 하지 않는다.
	bool IsPlayingView(int32 ViewIndex) const { return PlayWorld && ViewIndex == PIEViewIndex && !bIsPaused && !IsSimulatingInEditor(); }
	bool IsSimulatingInEditor() const;

	// FEngineLoop 소유. OnInit에서 받아 둔다.
	FWindow* MainWindow = nullptr;
	FSwapchain* MainWindowSC = nullptr;
	FRenderer* Renderer = nullptr;

	TUniquePtr<FEditorUI> EditorUI;

	TUniquePtr<FImGuiRenderer> ImGuiRenderer;
	TUniquePtr<FGridRenderer> GridRenderer;
	TUniquePtr<FGizmoRenderer> GizmoRenderer;
	TUniquePtr<FTextRenderer> TextRenderer;
	TUniquePtr<FLineBatcher> LineBatcher;
	TUniquePtr<FOutline> Outline;
	TUniquePtr<FOutlineRenderer> OutlineRenderer;
	TUniquePtr<FSceneDepthRenderer> SceneDepthRenderer;
	TUniquePtr<FCascadeFloorRenderer> CascadeFloorRenderer;
	TUniquePtr<FUIRenderer> UIRenderer;
	TUniquePtr<FGizmo> Gizmo;

	FUIDrawList UIDrawList;

	UFont* SystemFont;

	FOutputLogPanel* OutputLogPanel = nullptr;

	FDetailsPanel* DetailsPanel = nullptr;
	TUniquePtr<FEditorControlsPanel> EditorControlsPanel;
	FSettingsPanel* SettingsPanel = nullptr;
	FSequencerPanel* SequencerPanel = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;
	FSplitRatio ViewportSplitRatio{0.5f, 0.5f};
	ELayoutMode ViewportLayoutMode = ELayoutMode::QuadSplit;
	int32 SingleViewportIndex = 0;
	TArray<FEditorViewportClient*> AllViewportClients;
	FOutlinerPanel* OutlinerPanel = nullptr;
	// 아웃라이너와 뷰포트 피킹이 함께 쓰는 다중 선택 (TEAM1)
	FEditorSelection Selection;
	TArray<uint8> CopiedActorData;
	FEditorCommand Copy;
	FEditorCommand Paste;
	FEditorCommand Duplicate;
	FContentDrawerPanel* ContentDrawerPanel = nullptr;
	FAssetEditorPanel* AssetEditorPanel = nullptr;
	FParticleSystemEditorPanel* ParticleSystemEditorPanel = nullptr;
	FUIEditorPanel* UIEditorPanel = nullptr;
	FPoseRetargetPanel* PoseRetargetPanel = nullptr;
	FSkeletalMeshViewerPanel* SkeletalMeshViewerPanel = nullptr;
	FAnimSequenceViewerPanel* AnimSequenceViewerPanel = nullptr;
	FAnimGraphEditorPanel* AnimGraphEditorPanel = nullptr;
	FBlendSpaceEditorPanel* BlendSpaceEditorPanel = nullptr;
	FAnimMontageEditorPanel* AnimMontageEditorPanel = nullptr;
	FAimOffsetEditorPanel* AimOffsetEditorPanel = nullptr;

	UWorld* EditorWorld = nullptr;
	UWorld* PlayWorld = nullptr;
	// PIE 시작 시점의 편집 View. PIE가 아니면 InvalidViewIndex다.
	int32 PIEViewIndex = -1;
	int32 PIEStartEditorViewIndex = -1;

	// Play 버튼이 다음 세션을 띄울 위치
	EPlayModeType PlayMode = EPlayModeType::InViewport;

	// New Editor Window(PIE) 모드에서만 존재한다. Swapchain이 창 핸들을 쓰므로 창보다 먼저 해제한다.
	TUniquePtr<FWindow> PIEWindow;
	TUniquePtr<FSwapchain> PIESwapchain;
	uint32 PIETargetWidth = 0;
	uint32 PIETargetHeight = 0;
	bool bPIEWindowRendered = false;

	FVector UUIDLocation;

	FGamePackager Packager;

	FEditorCommand Save;
	// 씬과 열려 있는 애니메이션 에디터의 저장하지 않은 변경을 모두 저장한다 (Ctrl+Shift+S)
	FEditorCommand SaveAll;

	FEditorCommand Play;
	FEditorCommand Pause;
	FEditorCommand Resume;
	FEditorCommand StepFrame;
	FEditorCommand Stop;
	// UE의 Eject / Possess. 폰에서 빠져나와 에디터 카메라로 보거나(SIE) 다시 폰으로 돌아간다. 키는 F8이다.
	FEditorCommand Eject;
	FEditorCommand Possess;

	bool bIsStep = false;

	bool bPlaySessionRequested = false;
	bool bRequestEndPlayMapQueued = false;
	bool bStepRequested = false;
	bool bIsPaused = false;
	// Shift+F1로 커서를 꺼냈는지. 참이면 PIE View를 클릭할 때 다시 잠근다 (TEAM5)
	bool bPIEMouseReleased = false;
	void UpdatePIEMouseCapture(FGameViewportClient& GameViewport, bool bMouseOverPIE);

	void ResetSceneSelection();

	void CreateNewScene();
	void OpenScene();
	void SaveCurrentScene();
	void SaveAllAssets();
	void SaveSceneAs();

	void CreatePIESession();
	void StopPIESession();
	void DrawPIEWindows();


	void UpdateInputOwner();
	void DispatchGameInput(float DeltaTime);
	FGameViewportClient* FindPIEGameClient(int32 PIEInstance) const;

	int32 InputOwnerPIEInstance = -1;
	bool bGameMouseCaptured = false;
	int32 PendingFocusedPIEInstance = -1;
	int32 PendingWheelDelta = 0;

};
