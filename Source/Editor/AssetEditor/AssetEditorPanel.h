#pragma once
#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
class URenderAsset;
class UWorld;
class FSkyboxRenderer;
class UEditorEngine;

// Root asset tab for TEAM5 imported static meshes, textures, materials and skeletons.
class FAssetEditorPanel : public IEditorPanel
{
public:
	FAssetEditorPanel();
	~FAssetEditorPanel() override;
	bool Init() override { SetOpen(false); return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Asset Editor"; }
	bool IsRootPanel() const override { return true; }
	void OpenAsset(const FString& Path);
	void NewMaterial(const FString& InPath, bool bParticleSprite);
	void NewMaterialInstance(const FString& InPath, const FString& ParentPath);
	void RenderPreview(UEditorEngine* Engine);
	void Release();
	void SaveIfDirty();
	bool HasFocusedWindow() const { return IsOpen() && bFocused; }

private:
	void SaveMaterial();
	FString Path;
	FString Status;
	URenderAsset* Asset = nullptr;
	UWorld* PreviewWorld = nullptr;
	FEditorViewportClient Client;
	float CameraSpeed = 1.0f;
	TUniquePtr<FSkyboxRenderer> Skybox;
	bool bVisible = false;
	bool bDirty = false;
	bool bFocused = false;
};
