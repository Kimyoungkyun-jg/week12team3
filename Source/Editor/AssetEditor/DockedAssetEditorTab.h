#pragma once

#include "AssetEditorTab.h"
#include "AssetEditorPanel.h"
#include "Containers/Array.h"

class FDockedAssetEditorTab : public FAssetEditorTab
{
public:
    FDockedAssetEditorTab(FAssetEditor& InEditor, const char* DockSpaceName, const char* PanelClassName);

    void DrawMenuBar() override;
    void OnRender(bool bActive) override;

protected:
    void RegisterPanel(TUniquePtr<FAssetEditorPanelBase> Panel);
    virtual void BeforeRender()
    {
    }
    virtual void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) = 0;

    TArray<TUniquePtr<FAssetEditorPanelBase>> Panels;

private:
    FAssetEditor& Editor;
    ImGuiID DockSpaceId = 0;
    ImGuiID PanelClassId = 0;
    bool bLayoutInitialized = false;
};
