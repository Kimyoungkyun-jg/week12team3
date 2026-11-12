#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Animation/AnimBlueprintData.h"
#include "Containers/Map.h"

class UAnimGraphInstance;
class UAnimSequence;
class IEditorTab;

class FAnimBlueprintEditor : public FSkeletalPreviewEditor
{
public:
    FAnimBlueprintEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InPreviewMeshPath);
    ~FAnimBlueprintEditor() override;
    FAnimBlueprintEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InAssetPath, FAnimBlueprintData InData);
    void RequestSave();
    bool Save();
    bool HasUnsavedChanges() const;
    const FString& GetSaveMessage() const;
    void Close() override;
    void HandleTabClose(IEditorTab& Tab);

    const char* GetEditorTypeName() const override { return "AnimBlueprint"; }

    FAnimBlueprintGraph* GetCurrentGraph();
    FAnimBlueprintNode* GetSelectedNode();
    FAnimBlueprintGraph* FindGraph(int32 GraphId);
    FAnimBlueprintNode* FindNode(int32 NodeId);
    FAnimBlueprintTransition* GetSelectedTransition();
    TArray<int32> GetGraphPath() const;
    bool NavigateToGraph(int32 GraphId);
    bool OpenNodeGraph(int32 NodeId);
    bool RenameNode(int32 NodeId, const FString& Name);
    void SelectLink(int32 LinkId);

    const FAnimBlueprintData& GetBlueprintData() const { return BlueprintData; }

    // Class Defaults (Unreal AnimBP Class Defaults > Root Motion). 바꾸면 저장 안 됨 표시가 뜨고 Save로 .animblueprint에 저장된다
    ERootMotionMode GetRootMotionMode() const { return BlueprintData.RootMotionMode; }
    void SetRootMotionMode(ERootMotionMode Mode) { BlueprintData.RootMotionMode = Mode; }

    void SelectNode(int32 NodeId);
    int32 AddSequencePlayer(const FAnimBlueprintPosition& Position);
    int32 AddPoseNode(EAnimBlueprintNodeType Type, const FAnimBlueprintPosition& Position);
    int32 AddState(const FAnimBlueprintPosition& Position);
    int32 AddAnyState(const FAnimBlueprintPosition& Position);
    bool IsPreviewStateActive(int32 NodeId) const;
    bool CanConnectPins(int32 FirstPinId, int32 SecondPinId);
    bool ConnectPins(int32 FirstPinId, int32 SecondPinId);

    TArray<FAnimBlueprintVariable>& GetVariables() { return BlueprintData.Variables;  }

    bool AddVariable(const FString& Name, EAnimBlueprintVariableType Type);
    bool RenameVariable(const FString& PreviousName, const FString& Name);
    void RemoveVariable(const FString& Name);

    const FString& GetVariableMessage() const { return VariableMessage; }
    const TArray<FAnimBlueprintVariable>& GetAppliedVariables() const { return AppliedVariables; }

    float GetPreviewFloat(const FString& Name) const;
    bool GetPreviewBool(const FString& Name) const;
    void SetPreviewFloat(const FString& Name, float Value);
    void SetPreviewBool(const FString& Name, bool bValue);

    bool CanConnectPosePins(int32 FirstPinId, int32 SecondPinId);
    bool ConnectPosePins(int32 FirstPinId, int32 SecondPinId);

    bool CanRemoveNode(int32 NodeId);
    bool RemoveNode(int32 NodeId);
    bool RemoveLink(int32 LinkId);

    void RequestApplyPreview();

    const FString& GetPreviewMessage() const
    {
        return PreviewMessage;
    }

protected:
    bool InitializeResources() override;
    void ShutdownResources() override;
    void TickBeforeWorld(float DeltaTime) override;
    TUniquePtr<IEditorTab> CreateTab() override;

private:
    void CreateInitialGraph();
    FAnimBlueprintNode* FindNodeByPin(int32 PinId);
    bool ResolvePoseConnection(int32 FirstPinId, int32 SecondPinId, FAnimBlueprintLink& OutConnection);
    bool ResolveStateConnection(int32 FirstPinId, int32 SecondPinId, FAnimBlueprintLink& OutConnection);

    int32 CreateChildGraph(int32 ParentGraphId, int32 OwnerNodeId, const FString& Name, EAnimBlueprintGraphType Type);
    void RemoveChildGraphs(int32 GraphId);

    bool HasPosePath(int32 FromNodeId, int32 ToNodeId);

    bool ValidateVariableName(const FString& Name, const FString& PreviousName);

    
    bool ApplyPreview();
    bool SetPreviewError(int32 NodeId, const FString& Message);

    // 루트 모션 미리보기 (Unreal 미리보기 Process Root Motion = Loop)
    // 게임에서는 CharacterMovement가 가져가는 이동량을 여기서 가져가 미리보기 메시를 옮긴다.
    // 상태 머신·블렌드가 섞이므로 시간으로 계산하지 않고 프레임마다 누적한다
    void ApplyPreviewRootMotion();
    void ResetPreviewRootMotion();
    FTransform PreviewBaseTransform;   // 루트 모션을 적용하기 전 메시 위치 (리셋 위치)
    bool bHasPreviewBase = false;

    FAnimBlueprintData BlueprintData;

    int32 CurrentGraphId = INDEX_NONE;
    int32 SelectedNodeId = INDEX_NONE;
    int32 SelectedLinkId = INDEX_NONE;

    TUniquePtr<UAnimGraphInstance> PreviewInstance;
    TMap<int32, TArray<int32>> PreviewNodeIndices;
    TArray<FAnimBlueprintVariable> AppliedVariables;
    FString VariableMessage;
    FString PreviewMessage;
    bool bApplyPreviewRequested = false;
    bool bSaveRequested = false;
    bool bDocumentLoaded = false;
    FString SavedSnapshot;
    FString SaveMessage;
};
