#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Animation/AnimationAssetPreview.h"
#include "Asset/AnimGraphAsset.h"
#include <functional>

class UEditorEngine;

class FAnimationAssetEditorPanel : public IEditorPanel
{
public:
	enum class EKind { Montage, AimOffset };
	explicit FAnimationAssetEditorPanel(EKind InKind) : Kind(InKind) {}
	~FAnimationAssetEditorPanel() override = default;
	bool Init() override { SetOpen(false); return true; }
	bool IsRootPanel() const override { return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void RenderPreview(UEditorEngine* Engine);
	void Release() { Preview.Release(); }
	void OpenAsset(const FString& InPath);
	void NewAsset(const FString& InPath);
	void SetOpenSequenceCallback(std::function<void(const FString&)> Callback) { OnOpenSequence = std::move(Callback); }
	void SetSavedCallback(std::function<void()> Callback) { OnSaved = std::move(Callback); }

protected:
	EKind Kind;

private:
	void Save();
	void StartPreview();
	void EvaluatePreview();
	void DrawAssetDetails();
	void DrawGraph();
	void DrawSelectedDetails();
	void DrawAssetBrowser();
	void DrawTransport();
	bool IsCompatibleSequence(const FString& Path) const;
	float SequenceLength() const;
	FString SkeletonPath() const;
	FString Path;
	FString Status;
	FAnimMontageAsset Montage;
	FAimOffsetAsset AimOffset;
	FAnimationAssetPreview Preview;
	std::function<void(const FString&)> OnOpenSequence;
	std::function<void()> OnSaved;
	float LeftRatio = 0.20f;
	float RightRatio = 0.22f;
	float ViewportRatio = 0.67f;
	float DetailsRatio = 0.67f;
	float PreviewTime = 0.0f;
	float PreviewPitch = 0.0f;
	int SelectedSample = -1;
	int SelectedSection = -1;
	int SelectedNotify = -1;
	int DraggedMarker = -1;
	char Search[128]{};
	bool bPlaying = true;
	bool bDirty = false;
	bool bVisible = false;
	bool bFocusRequested = false;
};

class FAnimMontageEditorPanel final : public FAnimationAssetEditorPanel
{
public:
	FAnimMontageEditorPanel() : FAnimationAssetEditorPanel(EKind::Montage) {}
	const char* GetPanelName() const override { return "Anim Montage Editor"; }
};

class FAimOffsetEditorPanel final : public FAnimationAssetEditorPanel
{
public:
	FAimOffsetEditorPanel() : FAnimationAssetEditorPanel(EKind::AimOffset) {}
	const char* GetPanelName() const override { return "Aim Offset Editor"; }
};
