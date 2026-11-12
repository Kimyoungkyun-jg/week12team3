#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "SkeletalPreviewEditor.h"

class FSkeletalPreviewViewportPanel : public TAssetEditorPanel<FSkeletalPreviewEditor>
{
public:
    explicit FSkeletalPreviewViewportPanel(FSkeletalPreviewEditor& InEditor)
        : TAssetEditorPanel<FSkeletalPreviewEditor>(InEditor, "Viewport", "AnimSequencePanel")
    {
    }

protected:
    void DrawContent() override;

};
