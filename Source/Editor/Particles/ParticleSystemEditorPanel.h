#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"

class UObject;
class UWorld;
class UParticleSystem;
class UParticleSystemComponent;
class UParticleEmitter;
class UParticleModule;
class UEditorEngine;

class FParticleSystemEditorPanel : public IEditorPanel
{
public:
	~FParticleSystemEditorPanel() override { Release(); }

	bool Init() override
	{
		SetOpen(false);
		return true;
	}
	void Tick(float DeltaTime) override;
	void OnRender() override;

	const char* GetPanelName() const override { return "Particle System Editor"; }
	bool IsRootPanel() const override { return true; }

	void NewAsset(const FString& InPath);
	void OpenAsset(const FString& InPath);
	void RenderPreview(UEditorEngine* Engine);
	void Release();

	void Save();
	void SaveIfDirty()
	{
		if (IsOpen() && bDirty)
		{
			Save();
		}
	}

	bool IsDirty() const { return bDirty; }
	bool HasFocusedWindow() const { return IsOpen() && bFocused; }

private:
	void ReleasePreview();
	void StartPreview();
	void ResetPreview();

	void DrawToolbar();
	void DrawEmitterPanel();
	void DrawPreviewViewport();
	void DrawDetailsPanel();

	void SelectObject(UObject* Object, UParticleEmitter* Emitter);
	void AddModule(UClass* ModuleClass, const FName& ModuleName);

private:
	FString Path;
	FString Status;

	UParticleSystem* ParticleSystem = nullptr;
	UParticleSystemComponent* PreviewComponent = nullptr;
	UWorld* PreviewWorld = nullptr;

	UObject* SelectedObject = nullptr;
	UParticleEmitter* SelectedEmitter = nullptr;
	UParticleModule* SelectedModule = nullptr;

	FEditorViewportClient Client;

	bool bPlaying = true;
	bool bDirty = false;
	bool bVisible = false;
	bool bFocused = false;

	float SimulationSpeed = 1.0f;
	float CameraSpeed = 1.0f;
};