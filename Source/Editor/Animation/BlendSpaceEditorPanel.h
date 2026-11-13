#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include <vector>
#include <functional>

class UBlendSpace;
class USkeleton;
class USkeletalMeshComponent;
class UWorld;
class UEditorEngine;

class FBlendSpaceEditorPanel : public IEditorPanel
{
public:
	~FBlendSpaceEditorPanel() override { Release(); }
	bool Init() override { SetOpen(false); return true; }
	const char* GetPanelName() const override { return "BlendSpace Editor"; }
	bool IsRootPanel() const override { return true; }
	void OpenAsset(const FString& InPath);
	void SetOpenSequenceCallback(std::function<void(const FString&)> Callback) { OnOpenSequence = std::move(Callback); }
	void NewAsset(const FString& InPath, bool bIn2D);
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void RenderPreview(UEditorEngine* Engine);
	void Release();
	// 현재 문서를 저장한다. Ctrl+S와 전체 저장(Ctrl+Shift+S)이 쓴다.
	void Save();
	void SaveIfDirty() { if (IsOpen() && bDirty) Save(); }
	bool IsDirty() const { return bDirty; }
	// 이 창(또는 안의 자식 창)에 키보드 포커스가 있다. 이때는 레벨 에디터의 Ctrl+S 등이 동작하지 않는다.
	bool HasFocusedWindow() const { return IsOpen() && bFocused; }

private:
	struct FAxis
	{
		char Name[64]{};
		float Min = 0.0f;
		float Max = 600.0f;
		int Divisions = 4;
		bool bSnap = true;
	};
	struct FSample
	{
		float X = 0.0f;
		float Y = 0.0f;
		FString SequencePath;
	};
	void StartPreview();
	bool RebuildBlendSpace();
	void EvaluatePreview();
	void DrawAssetDetails();
	void DrawGraph();
	void DrawSampleDetails();
	void DrawAssetBrowser();
	void AddSample(const FString& SequencePath, float X, float Y);
	bool Validate(FString& Error) const;
	float Snap(float Value, const FAxis& Axis) const;
	// Ignore 번째 샘플을 빼고 (X, Y)에 다른 샘플이 있는지. 1D는 X만 본다.
	bool IsSamplePositionTaken(int Ignore, float X, float Y) const;

	FString Path;
	std::function<void(const FString&)> OnOpenSequence;
	float LeftRatio = 0.20f;
	float RightRatio = 0.22f;
	float ViewportRatio = 0.67f;
	float DetailsRatio = 0.70f;
	FString Status;
	FString SkeletonPath;
	FAxis XAxis;
	FAxis YAxis;
	std::vector<FSample> Samples;
	int SelectedSample = -1;
	bool b2D = true;
	bool bDirty = false;
	bool bFocused = false;
	bool bPlaying = true;
	bool bVisible = false;
	// 창을 완전히 닫아 프리뷰를 해제했으면 true. 다시 열릴 때 프리뷰를 복원한다.
	bool bRestorePreviewOnOpen = false;
	bool bFocusRequested = false;
	float PreviewX = 0.0f;
	float PreviewY = 0.0f;
	float PreviewTime = 0.0f;
	char Search[128]{};
	UBlendSpace* PreviewSpace = nullptr;
	UWorld* PreviewWorld = nullptr;
	USkeletalMeshComponent* MeshComponent = nullptr;
	FEditorViewportClient Client;
	float CameraSpeed = 1.0f;
};
