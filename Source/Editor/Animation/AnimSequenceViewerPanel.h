#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Containers/Array.h"
#include "Render/SkyboxRenderer.h"

class UAnimSequence;
class UAnimSingleNodeInstance;
class USkeletalMesh;
class UWorld;
class UEditorEngine;
class ASkyLightActor;
class ADirectionalLightActor;
class USkeletalMeshDebugComponent;

class FAnimSequenceViewerPanel : public IEditorPanel
{
public:
	FAnimSequenceViewerPanel();
	~FAnimSequenceViewerPanel() override;
	bool Init() override { SetOpen(false); return true; }
	const char* GetPanelName() const override { return "Animation Sequence"; }
	bool IsRootPanel() const override { return true; }
	void OpenSequence(const FString& Path);
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void RenderPreview(UEditorEngine* Engine);
	void Release();

	// 현재 시퀀스를 .fbxbin에 저장한다 (Notify 포함). Ctrl+S와 Save Sequence 버튼이 쓴다.
	void Save();
	void SaveIfDirty() { if (IsOpen() && bDirty) Save(); }
	bool IsDirty() const { return bDirty; }
	// 이 창(또는 안의 자식 창)에 키보드 포커스가 있다. 레벨 에디터의 Ctrl+S 등이 이때는 동작하지 않게 하는 데 쓴다.
	bool HasFocusedWindow() const { return IsOpen() && bFocused; }

private:
	void MarkDirty() { bDirty = true; Status = "Unsaved changes"; }
	void SetPreviewMesh(USkeletalMesh* Mesh);
	void DrawBoneTree(int32 Parent);
	void DrawBoneDetails();
	void DrawAssetDetails();
	void DrawViewport();
	void DrawTimeline();
	void DrawNotifyTrack(float TrackWidth);
	void DrawNotifyDetails();
	void DrawAssetBrowser();

	FString Path;
	FString Status;
	UAnimSequence* Sequence = nullptr;
	UWorld* PreviewWorld = nullptr;
	USkeletalMeshDebugComponent* MeshComponent = nullptr;
	ASkyLightActor* SkyLight = nullptr;
	ADirectionalLightActor* DirectionalLight = nullptr;
	FEditorViewportClient Client;
	float CameraSpeed = 1.0f;
	TUniquePtr<FSkyboxRenderer> Skybox;
	TArray<TArray<int32>> BoneChildren;
	int32 SelectedBone = -1;
	// Notify 편집 상태. 인덱스는 Sequence->Notifies 기준이다.
	int32 SelectedNotify = -1;
	int32 DraggingNotify = -1;
	// 끌고 있는 항목의 동작. 점 Notify는 Move, 막대(Notify State)는 이동 또는 양끝 크기 조절이다.
	enum class ENotifyDragMode { None, Move, ResizeStart, ResizeEnd };
	ENotifyDragMode DragMode = ENotifyDragMode::None;
	// Move: 잡은 위치와 시작 시각의 차이(초). ResizeStart: 고정해 두는 끝 시각(초).
	float DragGrabOffset = 0.0f;
	float DragFixedEnd = 0.0f;
	// 우클릭한 위치(초)와 대상 Notify. 팝업이 열려 있는 동안 유지한다.
	float NotifyPopupTime = 0.0f;
	int32 NotifyPopupTarget = -1;
	// Notify를 새로 골랐을 때 오른쪽 Notify 탭으로 전환하기 위한 요청
	bool bFocusNotifyTab = false;
	float PreviewRate = 1.0f;
	bool bPlaying = true;
	bool bLoop = true;
	bool bShowGrid = true;
	bool bShowBounds = false;
	bool bShowSkybox = true;
	float SkyLightIntensity = 0.3f;
	float DirectionalLightIntensity = 1.0f;
	bool bVisible = false;
	bool bFocused = false;
	// 저장하지 않은 변경이 있다 (Notify, Rate Scale, Root Motion)
	bool bDirty = false;
	// 창을 완전히 닫아 프리뷰를 해제했으면 true. 다시 열릴 때 프리뷰를 복원한다.
	bool bRestorePreviewOnOpen = false;
	char Search[128]{};
};
