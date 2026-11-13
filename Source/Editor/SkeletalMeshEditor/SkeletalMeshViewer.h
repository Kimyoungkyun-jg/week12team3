#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Components/SkeletalMeshDebugComponent.h"

class UWorld;
class USkeletalMeshComponent;
class UTexture2D;
class FSkyboxRenderer;

// FBX 하나를 띄우는 창. 레벨과 섞이지 않도록 월드를 따로 갖는다.
struct FSkeletalMeshViewer
{
	~FSkeletalMeshViewer();

	FString Path;
	uint32 Id = 0;
	UWorld* World = nullptr;
	USkeletalMeshComponent* MeshComponent = nullptr;
	FEditorViewportClient Client;
	bool bOpen = true;
	bool bVisible = false;

	TArray<TArray<uint32>> Children;
	TArray<int32> WeightedVertexCounts;
	int32 SelectedBone = -1;

	FGizmo Gizmo;
	USceneComponent* BoneGizmoTarget = nullptr;

	ImVec2 ViewSize{ 0.0f, 0.0f };

	bool bScrollTreeToSelection = false;

	EBoneDrawMode BoneDrawMode = EBoneDrawMode::AllHierarchy;
	bool bShowMesh = true;
	// 선택한 본의 가중치를 색으로 보는 모드. 켜는 동안 뷰를 Unlit으로 바꾸고, 끌 때 원래 모드로 되돌린다.
	bool bShowBoneWeightHeatmap = false;
	EViewportMode ModeBeforeHeatmap = EViewportMode::Solid;
	bool bShowGrid = false;
	float DetailsHeight = 280.0f;
	float CameraSpeed = 4.0f;
	bool bEditingCameraSpeed = false;
	bool bFocusCameraSpeedInput = false;
	UTexture2D* ShowFlagsIcon = nullptr;

	// 본 트랜스폼 편집 값. 행렬을 매 프레임 분해하면 오일러 값이 튀므로 행렬이 바뀔 때만 다시 분해한다.
	int32 EditedBone = -1;
	FMatrix EditedMatrix;
	FVector EditedLocation;
	FRotator EditedRotation;
	FVector EditedScale;
	TUniquePtr<FSkyboxRenderer> Skybox;
};

class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }
	bool IsRootPanel() const override { return true; }
	void OnHidden() override { bHasFocusedViewer = false; }
	void OpenFBX(const FString& Path);
	void Release();
	const TArray<TUniquePtr<FSkeletalMeshViewer>>& GetViewers() const { return Viewers; }
	bool HasFocuesdViewer() const { return IsOpen() && bHasFocusedViewer; }

private:
	void DrawViewport(FSkeletalMeshViewer& Viewer);
	void DrawViewportToolbar(FSkeletalMeshViewer& Viewer);
	void DrawBoneTree(FSkeletalMeshViewer& Viewer);
	void DrawBoneNode(FSkeletalMeshViewer& Viewer, int32 BoneIndex);
	void DrawBoneDetails(FSkeletalMeshViewer& Viewer);
	void FocusOnSelectedBone(FSkeletalMeshViewer& Viewer);
	int32 PickBone(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, const ImVec2& ViewSize);
	void SelectBone(FSkeletalMeshViewer& Viewer, int32 BoneIndex);
	void UpdateBoneGizmo(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, bool bLeftDown);

	TArray<TUniquePtr<FSkeletalMeshViewer>> Viewers;
	uint32 NextId = 0;
	bool bHasFocusedViewer = false;
};
