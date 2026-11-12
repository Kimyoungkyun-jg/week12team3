#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Panels/StaticMeshEditorPanel.h"

class FStaticMeshEditor;

class FStaticMeshEditorTab : public FAssetEditorTab
{
public:
	explicit FStaticMeshEditorTab(FStaticMeshEditor& InEditor);

	// 탭의 메뉴 바를 그립니다.
	void DrawMenuBar() override;

	// 탭의 렌더링을 수행합니다.
	void OnRender(bool bActive) override;

private:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);

	FStaticMeshEditor& MeshEditor;
	TArray<TUniquePtr<FStaticMeshEditorPanel>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
