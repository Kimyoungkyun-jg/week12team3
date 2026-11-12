#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Math/MathUtility.h"
#include "Animation/AnimBlueprintData.h"
#include <vector>

class FAnimBlueprintEditor;
struct FAnimBlueprintNode;
struct FAnimBlueprintFloatInput;

class FAnimBlueprintDetailsPanel : public TAssetEditorPanel<FAnimBlueprintEditor>
{
public:
    explicit FAnimBlueprintDetailsPanel(FAnimBlueprintEditor& InEditor);

protected:
    void DrawContent() override;

private:
    void DrawAssetSelector(FString& AssetPath, bool bBlendSpace);
    void RefreshAssetList(bool bBlendSpace);
    void DrawFloatInput(const char* Label, FAnimBlueprintFloatInput& Input, bool bAlpha);
    void DrawClassDefaults();
    void DrawVariables();
    void DrawPreviewVariables();
    void DrawTransitionDetails(FAnimBlueprintTransition& Transition);
    void DrawVariableSelector(const char* Label, FString& Name, EAnimBlueprintVariableType Type);
    // State OnEnter/OnExit: 이벤트 이름 + 변수 대입 목록
    void DrawStateEvent(const char* Label, FAnimBlueprintStateEvent& Event);

    std::vector<FString> AssetPaths;
    FString AssetListMessage;
    char NewVariableName[128] = {};
    int NewVariableType = 0;
    FString RenamingVariable;
    char RenameBuffer[128] = {};
    int32 NamingNodeId = INDEX_NONE;
    char NodeNameBuffer[128] = {};
};
