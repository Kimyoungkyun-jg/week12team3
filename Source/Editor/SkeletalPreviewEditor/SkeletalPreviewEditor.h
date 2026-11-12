#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Camera/ViewportCameraMovement.h"

class USkeletalMesh;                                 
class USkeletalMeshComponent;
class FLineBatcher;

struct FEditorBoneItem
{
	int32 BoneIndex;
	FString Name;

	// 자식 Bone Index 배열. 계층 구조 그릴 때 사용
	TArray<int32> ChildBoneIndices;

	// BindPose 행렬의 역행렬. Gizmo 그릴 때 사용
	FMatrix InverseBindPose;
};

struct FViewportPointerInput
{
	// 이미지 크기에 대한 비율로 저장하여 분리 창과 렌더 타깃의 좌표를 일치시킵니다.
	FVector2 MouseUV = FVector2(0.0f, 0.0f);
	bool bHovered = false;
	bool bFocused = false;
	bool bMousePressed = false;
	bool bMouseDown = false;
	bool bCancel = false;
};

class FSkeletalPreviewEditor : public FAssetEditor
{
public:
	using FAssetEditor::FAssetEditor;

	void Tick(float DeltaTime) final;        // 순서 고정: 카메라, TickBeforeWorld, 월드 틱, TickAfterWorld, 입력 비우기
	void OnRenderPreview() override;
	void OnRenderLine(FLineBatcher& LineBatcher) override;

	void FocusMesh();
	void SetCameraSpeed(float Speed) { CameraSpeed = FMath::Clamp(Speed, 1.0f, 150.0f); }
	void SelectBone(int32 BoneIndex);        // 선택만 바꾸고 OnBoneSelected() 호출
	void SetBoneVisualizerVisible(bool bVisible) { bShowBoneVisualizer = bVisible; }
	void SetBoneVisualizerColor(const FVector4& Color) { BoneVisualizerColor = Color; }
	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input, const FViewportPointerInput& InPointer = {});

	USkeletalMesh* GetMesh() const { return Mesh; }
	USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
	float GetCameraSpeed() const { return CameraSpeed; }
	const TArray<int32>& GetRootBoneIndices() const { return RootBoneIndices; }
	const TArray<FEditorBoneItem>& GetBoneItems() const { return BoneItems; }
	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
	bool IsBoneVisualizerVisible() const { return bShowBoneVisualizer; }
	const FVector4& GetBoneVisualizerColor() const { return BoneVisualizerColor; }

	void SetWeightHeatmap(bool bEnable);
	bool IsWeightHeatmapEnabled() const { return bShowWeightHeatmap; }

protected:
	bool InitializePreview();                // Mesh 검증, 프리뷰 씬 생성, 컴포넌트·본 목록·포커스
	void ShutdownResources() override;       // 선택·컴포넌트·Mesh 정리 후 FAssetEditor::ShutdownResources

	virtual void TickBeforeWorld(float DeltaTime) {}
	virtual void TickAfterWorld(float DeltaTime) { UpdateBonePicking(); }
	virtual bool IsViewportInteractionBusy() const { return false; }
	virtual void OnBoneSelected() {}
	virtual void CancelViewportInteraction() {}   // FocusMesh 첫 줄에서 호출. 메시 에디터는 BoneGizmo.EndDrag()

	bool CanProcessViewportInput() const;
	void UpdateBonePicking();
	int32 PickBone(const FVector2& MousePosition) const;

	void RefreshHeatmap();
	bool bShowWeightHeatmap = false;

	USkeletalMesh* Mesh = nullptr;
	USkeletalMeshComponent* MeshComponent = nullptr;

	FViewportCameraInput CameraInput;
	FViewportPointerInput PointerInput;
	float CameraSpeed = 5.0f;
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	bool bViewportVisible = false;
	bool bInitialFocusPending = true;

	TArray<FEditorBoneItem> BoneItems;
	TArray<int32> RootBoneIndices;
	int32 SelectedBoneIndex = -1;

	float BoneSphereSize = 0.0f;
	FVector4 BoneVisualizerColor = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	bool bShowBoneVisualizer = true;

private:
	void BuildBoneItems();
};