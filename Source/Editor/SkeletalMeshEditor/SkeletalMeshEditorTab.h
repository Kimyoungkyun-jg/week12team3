#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Panels/SkeletalMeshEditorPanel.h"

class FSkeletalMeshEditor;

class FSkeletalMeshEditorTab : public FAssetEditorTab
{
public:
	explicit FSkeletalMeshEditorTab(FSkeletalMeshEditor& InEditor);

	// 탭의 메뉴 바를 그립니다.
	void DrawMenuBar() override;

	// 탭의 렌더링을 수행합니다.
	void OnRender(bool bActive) override;

private:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);

	FSkeletalMeshEditor& MeshEditor;
	TArray<TUniquePtr<FSkeletalMeshEditorPanel>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
