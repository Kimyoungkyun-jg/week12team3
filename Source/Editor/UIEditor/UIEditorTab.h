#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Containers/Array.h"
#include "Core/Types.h"

class FUIEditor;

class FUIEditorTab : public FAssetEditorTab
{
public:
	explicit FUIEditorTab(FUIEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);

	FUIEditor& UIEditor;
	// 도킹과 분리 판정은 에디터 타입과 무관하므로 공통 베이스 포인터로 들고 있습니다.
	TArray<TUniquePtr<FAssetEditorPanelBase>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
