#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Animation/AnimBlueprintData.h"
#include "imgui_node_editor.h"
#include "Containers/Map.h"

#include <unordered_set>

class FAnimBlueprintEditor;

class FAnimBlueprintGraphPanel : public TAssetEditorPanel<FAnimBlueprintEditor>
{
public:
    explicit FAnimBlueprintGraphPanel(FAnimBlueprintEditor& InEditor);
    ~FAnimBlueprintGraphPanel() override;

    bool Init() override;

protected:
    void DrawContent() override;

private:
    struct FGraphView
    {
        ax::NodeEditor::EditorContext* Context = nullptr;
        std::unordered_set<int32> PositionedNodeIds;
        bool bInitialViewApplied = false;
        FAnimBlueprintEditor* Owner = nullptr;
        int32 GraphId = INDEX_NONE;
    };

    void DrawGraphPath();
    static bool SaveViewSettings(const char* Data, size_t Size, ax::NodeEditor::SaveReasonFlags Reason, void* UserPointer);
    static size_t LoadViewSettings(char* Data, void* UserPointer);
    void RemoveDeletedGraphViews();
    void DrawNodes(FAnimBlueprintGraph& Graph);
    void DrawLinks(const FAnimBlueprintGraph& Graph);

    void HandleLinkCreation();
    void HandleDeletion();
    void DrawCreationMenu();

    void UpdateNodePositions(FAnimBlueprintGraph& Graph);
    void UpdateSelection();

    ax::NodeEditor::EditorContext* NodeEditorContext = nullptr;

    TMap<int32, FGraphView> GraphViews;
    FGraphView* CurrentView = nullptr;
    FAnimBlueprintPosition CreationPosition;

};
