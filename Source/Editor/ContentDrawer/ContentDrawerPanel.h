#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/BlendSpaceEditor/BlendSpaceAssetCreator.h"
#include "Editor/AnimBlueprintEditor/AnimBlueprintAssetCreator.h"

#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);

	// .widget 파일을 더블클릭하면 불린다 (UI 에디터가 연다).
	void SetOpenWidgetCallback(std::function<void(const FString&)> InCallback) { OnOpenWidget = std::move(InCallback); }
	void SetCreateWidgetCallback(std::function<void(const FString&)> InCallback) { OnCreateWidget = std::move(InCallback); }
	// OBJ 또는 베이크된 FBX 메시를 더블클릭하면 Viewer를 엽니다.
	void SetOpenMeshCallback(std::function<void(const FString&)> InCallback) { OnOpenMesh = std::move(InCallback); }
	void SetBlendSpaceCreatedCallback(std::function<void(const FString&)> InCallback)
	{
		BlendSpaceCreator.SetCreatedCallback(std::move(InCallback));
	}

    void SetAnimBlueprintCreatedCallback(std::function<void(const FString&)> InCallback)
    {
        AnimBlueprintCreator.SetCreatedCallback(std::move(InCallback));
    }

private:
	std::function<void(const FString&)> OnOpenWidget;
	std::function<void(const FString&)> OnCreateWidget;
	std::function<void(const FString&)> OnOpenMesh;
	char NewAssetName[64] = "NewWidget";
	bool bOpenCreateWidget = false;

	fs::path AssetRootPath = "Assets";
	fs::path CurrentPath = "Assets";

	const FString FolderIconPath = "Engine/Icons/Folder.png";
	const FString FileIconPath = "Engine/Icons/File.png";

	FBlendSpaceAssetCreator BlendSpaceCreator;
    FAnimBlueprintAssetCreator AnimBlueprintCreator;
};
