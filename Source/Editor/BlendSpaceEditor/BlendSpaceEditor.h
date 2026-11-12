#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"

class UBlendSpace1D;

class FBlendSpaceEditor : public FSkeletalPreviewEditor
{
public:
    FBlendSpaceEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
        const FString& InAssetPath, UBlendSpace1D* InBlendSpace);

    const char* GetEditorTypeName() const override { return "BlendSpace"; }
    bool Save();
    const FString& GetMessage() const { return Message; }
    UBlendSpace1D* GetBlendSpace() const { return BlendSpace; }

protected:
    bool InitializeResources() override;
    void ShutdownResources() override;
    TUniquePtr<IEditorTab> CreateTab() override;

private:
    UBlendSpace1D* BlendSpace = nullptr;
    FString Message;
};