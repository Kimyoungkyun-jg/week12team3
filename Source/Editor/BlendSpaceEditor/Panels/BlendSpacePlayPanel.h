#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/BlendSpaceEditor/BlendSpaceEditor.h"
#include <vector>

class FBlendSpacePlayPanel : public TAssetEditorPanel<FBlendSpaceEditor>
{
public:
    explicit FBlendSpacePlayPanel(FBlendSpaceEditor& InEditor)
        : TAssetEditorPanel<FBlendSpaceEditor>(InEditor, "Preview Controls", "AnimSequencePanel")
    {
    }

protected:
    void DrawContent() override;

private:
    void DrawSamples();
    void RefreshAnimationList();

    bool bLoop = true;

    std::vector<FString> AnimationPaths;
    int SelectedAnimation = -1;
    bool bAnimationListInitialized = false;
    float SampleX = 0.0f;
    FString SampleMessage;
};
