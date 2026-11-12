#pragma once

#include "Editor/AssetEditor/DockedAssetEditorTab.h"

class FBlendSpaceEditor;

class FBlendSpaceEditorTab : public FDockedAssetEditorTab
{
public:
    explicit FBlendSpaceEditorTab(FBlendSpaceEditor& InEditor);

private:
    void BeforeRender() override;
    void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;

    FBlendSpaceEditor& BlendSpaceEditor;
};
