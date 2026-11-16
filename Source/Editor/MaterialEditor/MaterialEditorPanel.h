#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"

class UMaterial;
class UMaterialInterface;
class UWorld;
class FSkyboxRenderer;
class UEditorEngine;

enum class EMaterialExpressionType : uint8;

class FMaterialEditorPanel : public IEditorPanel
{
public:
	FMaterialEditorPanel();
	~FMaterialEditorPanel() override;

	bool Init() override
	{
		SetOpen(false);
		return true;
	}
	void Tick(float DeltaTime) override;
	void OnRender() override;

	const char* GetPanelName() const override { return "Material Editor"; }
	bool IsRootPanel() const override { return true; }

	void OpenAsset(const FString& InPath);
	void NewMaterial(const FString& InPath, bool bParticleSprite);
	void NewMaterialInstance(const FString& InPath, const FString& ParentPath);

	void RenderPreview(UEditorEngine* Engine);
	void Release();
	void SaveIfDirty();

	bool HasFocusedWindow() const { return IsOpen() && bFocused; }

private:
	void SaveMaterial();
	void MarkMaterialChanged();

	void DrawMaterialGraph(UMaterial& Material);
	void DrawMaterialGraphPalette(UMaterial& Material);
	void DrawMaterialGraphCanvas(UMaterial& Material);
	void DrawMaterialGraphDetails(UMaterial& Material);
	void AddMaterialExpression(UMaterial& Material, EMaterialExpressionType Type, float X, float Y);
	void DrawPreview();

	FString Path;
	FString Status;

	UMaterialInterface* Asset = nullptr;
	UWorld* PreviewWorld = nullptr;

	FEditorViewportClient Client;
	TUniquePtr<FSkyboxRenderer> Skybox;

	float CameraSpeed = 1.0f;
	float GraphZoom = 1.0f;
	float GraphPanX = 0.0f;
	float GraphPanY = 0.0f;

	int32 SelectedNodeId = 0;
	int32 DraggingNodeId = 0;
	int32 PendingOutputNodeId = 0;
	int32 PendingOutputPin = 0;

	int32 SelectedConnectionOutputNodeId = 0;
	int32 SelectedConnectionOutputPin = 0;
	int32 SelectedConnectionInputNodeId = 0;
	int32 SelectedConnectionInputPin = 0;

	bool bDraggingConnection = false;
	bool bVisible = false;
	bool bFocused = false;
	bool bDirty = false;
	bool bMaterialThumbnailDirty = false;

	char SearchText[128]{};
};