#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Math/Color.h"

class UParticleSystem;
class UParticleEmitter;
class UParticleModule;
class AParticleSystemActor;
class UWorld;
class FSkyboxRenderer;
class UEditorEngine;

class FParticleSystemEditorPanel : public IEditorPanel
{
public:
	FParticleSystemEditorPanel();
	~FParticleSystemEditorPanel() override;

	bool Init() override { SetOpen(false); return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Particle System Editor"; }
	bool IsRootPanel() const override { return true; }

	void OpenAsset(const FString& InPath);
	void RenderPreview(UEditorEngine* Engine);
	void Release();

private:
	void CreateDefaultSystem();
	void SaveAsset();
	void RenderMenuBar();
	void RenderToolbar();
	void RenderViewportPanel(float Width, float Height);
	void RenderEmittersPanel(float Width, float Height);
	void RenderDetailsPanel(float Width, float Height);
	void RenderCurveEditorPanel(float Width, float Height);

	FString AssetPath;
	FString StatusMessage;
	UParticleSystem* EditingSystem = nullptr;
	UParticleEmitter* SelectedEmitter = nullptr;
	UParticleModule* SelectedModule = nullptr;
	UWorld* PreviewWorld = nullptr;
	AParticleSystemActor* PreviewActor = nullptr;
	FEditorViewportClient Client;
	TUniquePtr<FSkyboxRenderer> Skybox;

	float CameraSpeed = 1.0f;
	float SimSpeed = 1.0f;
	float TopSplitRatio = 0.56f;
	int32 SelectedCurveTrackIndex = 0;
	int32 SelectedCurveKeyId = -1;
	bool bPaused = false;
	bool bShowBounds = false;
	bool bShowOriginAxis = true;
	bool bShowGrid = true;
	bool bShowSkybox = false;
	bool bShowViewport = true;
	bool bShowEmitters = true;
	bool bShowDetails = true;
	bool bShowCurveEditor = true;
	bool bVisible = false;
	bool bFocusRequested = false;
	float BackgroundColor[4] = { 0.06f, 0.06f, 0.07f, 1.0f };
};
