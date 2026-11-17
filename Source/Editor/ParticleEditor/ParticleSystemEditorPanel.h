#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Math/Color.h"

class UParticleSystem;
class UParticleEmitter;
class UParticleModule;
class UParticleLODLevel;
class UCurveFloat;
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
	void Save();
	bool HasFocusedWindow() const { return IsOpen() && bFocused; }

private:
	void CreateDefaultSystem();
	void SaveAsset();
	bool NormalizeLODDistances();
	bool CanCopyLODData(const UParticleLODLevel* Source) const;
	void AddEmitter();
	void DuplicateEmitter(UParticleEmitter* Source);
	bool AddLowestLOD(bool bCopyPrevious);
	bool CopyLODData(UParticleLODLevel* Destination, const UParticleLODLevel* Source);
	UParticleModule* CopyModuleToLOD(UParticleLODLevel* Destination, const UParticleModule* Source);
	void CopyModuleProperties(UParticleModule* Destination, const UParticleModule* Source);
	void ApplyPreviewLOD();
	void RenderMenuBar();
	void RenderToolbar();
	void RenderLODControls();
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

	uint32 PendingViewportWidth = 0;
	uint32 PendingViewportHeight = 0;
	bool bViewportResizePending = false;
	float CameraSpeed = 1.0f;
	float SimSpeed = 1.0f;
	float MainColumnRatio = 0.46f;
	float TopRowRatio = 0.56f;
	float EmitterColumnWidth = 260.0f;
	int32 SelectedLODIndex = 0;
	int32 SelectedCurveTrackIndex = 0;
	int32 SelectedCurveKeyId = -1;
	TArray<UCurveFloat*> HiddenCurveTracks;
	bool bCopyPreviousLOD = true;
	bool bShowParticleStats = true;
	char ModuleSearchText[128] = {};
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
	bool bFocused = false;
	bool bFocusRequested = false;
	float BackgroundColor[4] = { 0.06f, 0.06f, 0.07f, 1.0f };
};
