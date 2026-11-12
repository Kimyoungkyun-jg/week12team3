#pragma once

#include "Editor/AssetEditor/DockedAssetEditorTab.h"

class FAnimBlueprintEditor;

class FAnimBlueprintEditorTab : public FDockedAssetEditorTab
{
public:
    explicit FAnimBlueprintEditorTab(FAnimBlueprintEditor& InEditor);
    const char* GetTabName() const override;
    void DrawMenuBar() override;
    void OnRender(bool bActive) override;
    void OnCloseRequested() override;

private:
    void BeforeRender() override;
    void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;

    FAnimBlueprintEditor& Editor;
    mutable FString DisplayName;
};
