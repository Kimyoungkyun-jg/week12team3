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

	bool Init() override;
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
	bool SaveAsset();
	bool SaveAssetAs();
	void MarkAssetModified(bool bRebuildModuleInfo = false);
	bool ResolveUnsavedChanges();
	bool ValidateForSave() const;
	void MarkPreviewRenderStateDirty();
	bool NormalizeLODDistances();
	bool CanCopyLODData(const UParticleLODLevel* Source) const;
	void AddEmitter();
	void DuplicateEmitter(UParticleEmitter* Source);
	bool AddLowestLOD(bool bCopyPrevious);
	bool InsertLOD(int32 InsertIndex, bool bCopyPrevious);
	bool RemoveLOD(int32 Index);
	bool MoveLODContent(int32 FromIndex, int32 ToIndex);
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

	void Undo();
	void Redo();
	bool RestoreSnapshot(const TArray<uint8>& Snapshot);
	void InitializeUndoHistory();
	void LoadEditorSettings();
	void SaveEditorSettings() const;

	FString AssetPath;
	FString StatusMessage;
	UParticleSystem* EditingSystem = nullptr;
	UParticleEmitter* SelectedEmitter = nullptr;
	UParticleModule* SelectedModule = nullptr;
	UWorld* PreviewWorld = nullptr;
	AParticleSystemActor* PreviewActor = nullptr;
	FEditorViewportClient Client;
	TUniquePtr<FSkyboxRenderer> Skybox;

	TArray<uint8> LastUndoSnapshot;
	TArray<TArray<uint8>> UndoStack;
	TArray<TArray<uint8>> RedoStack;
	bool bEditTransactionActive = false;
	bool bUndoRedoInProgress = false;

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
	UCurveFloat* SelectedCurve = nullptr;
	bool bCurveFitRequested = false;
	int32 SelectedCurveKeyId = -1;
	TArray<UCurveFloat*> HiddenCurveTracks;
	bool bCopyPreviousLOD = true;
	bool bShowParticleStats = true;
	char ModuleSearchText[128] = {};
	bool bPaused = false;
	bool bAssetDirty = false;
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
	bool bPreviewRenderStateDirty = false;
	float BackgroundColor[4] = { 0.06f, 0.06f, 0.07f, 1.0f };
};
