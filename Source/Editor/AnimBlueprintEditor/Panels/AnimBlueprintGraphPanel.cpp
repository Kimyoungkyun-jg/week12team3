#include "EnginePCH.h"
#include "AnimBlueprintGraphPanel.h"
#include "Editor/AnimBlueprintEditor/AnimBlueprintEditor.h"
#include <cstring>

namespace NodeEditor = ax::NodeEditor;

FAnimBlueprintGraphPanel::FAnimBlueprintGraphPanel(FAnimBlueprintEditor& InEditor)
    : TAssetEditorPanel<FAnimBlueprintEditor>(InEditor, "AnimGraph", "AnimSequencePanel")
{
}

FAnimBlueprintGraphPanel::~FAnimBlueprintGraphPanel()
{
    for (auto& Pair : GraphViews)
    {
        if (Pair.second.Context)
        {
            NodeEditor::DestroyEditor(Pair.second.Context);
        }
    }
}

bool FAnimBlueprintGraphPanel::Init()
{
    return true;
}

void FAnimBlueprintGraphPanel::DrawGraphPath()
{
    const TArray<int32> Path = Editor.GetGraphPath();

    for (int32 Index = 0; Index < Path.Num(); ++Index)
    {
        FAnimBlueprintGraph* Graph = Editor.FindGraph(Path[Index]);

        if (!Graph)
        {
            continue;
        }

        if (Index > 0)
        {
            ImGui::SameLine();
            ImGui::TextUnformatted(">");
            ImGui::SameLine();
        }

        ImGui::PushID(Graph->Id);
        const bool bNavigate = ImGui::SmallButton(Graph->Name.c_str());
        ImGui::PopID();

        if (bNavigate)
        {
            Editor.NavigateToGraph(Graph->Id);
            break;
        }
    }
}

void FAnimBlueprintGraphPanel::RemoveDeletedGraphViews()
{
    TArray<int32> RemovedIds;

    for (auto& Pair : GraphViews)
    {
        if (!Editor.FindGraph(Pair.first))
        {
            NodeEditor::DestroyEditor(Pair.second.Context);
            RemovedIds.Add(Pair.first);
        }
    }

    for (int32 Id : RemovedIds)
    {
        GraphViews.Remove(Id);
    }
}

void FAnimBlueprintGraphPanel::DrawContent()
{
    DrawGraphPath();
    FAnimBlueprintGraph* Graph = Editor.GetCurrentGraph();

    if (!Graph)
    {
        ImGui::TextUnformatted("Graph is not available.");
        return;
    }

    if (ImGui::Button("Save"))
    {
        Editor.RequestSave();
    }

    ImGui::SameLine();

    if (ImGui::Button("Apply Preview"))
    {
        Editor.RequestApplyPreview();
    }

    ImGui::SameLine();
    ImGui::Text("%s | Nodes: %d", Graph->Type == EAnimBlueprintGraphType::Pose ? "Pose Graph" : "State Machine", Graph->Nodes.Num());

    const FString& PreviewMessage = Editor.GetPreviewMessage();
    const FString& SaveMessage = Editor.GetSaveMessage();

    if (!SaveMessage.empty())
    {
        ImGui::TextWrapped("%s", SaveMessage.c_str());
    }

    if (!PreviewMessage.empty())
    {
        ImGui::TextWrapped("%s", PreviewMessage.c_str());
    }

    ImGui::Separator();
    const ImVec2 AvailableSize = ImGui::GetContentRegionAvail();

    if (AvailableSize.x < 1.0f || AvailableSize.y < 1.0f)
    {
        return;
    }

    CurrentView = &GraphViews.FindOrAdd(Graph->Id);

    if (!CurrentView->Context)
    {
        NodeEditor::Config Config;
        Config.SettingsFile = nullptr;
        CurrentView->Owner = &Editor;
        CurrentView->GraphId = Graph->Id;
        CurrentView->bInitialViewApplied = !Graph->EditorSettings.empty();
        Config.UserPointer = CurrentView;
        Config.SaveSettings = &FAnimBlueprintGraphPanel::SaveViewSettings;
        Config.LoadSettings = &FAnimBlueprintGraphPanel::LoadViewSettings;
        CurrentView->Context = NodeEditor::CreateEditor(&Config);
    }

    NodeEditorContext = CurrentView->Context;

    if (!NodeEditorContext)
    {
        ImGui::TextUnformatted("Failed to create the graph view.");
        return;
    }

    NodeEditor::EditorContext* PreviousContext = NodeEditor::GetCurrentEditor();
    NodeEditor::SetCurrentEditor(NodeEditorContext);
    NodeEditor::Begin("AnimBlueprintCanvas");

    DrawNodes(*Graph);
    DrawLinks(*Graph);
    HandleLinkCreation();
    HandleDeletion();
    DrawCreationMenu();

    const NodeEditor::NodeId DoubleClickedNode = NodeEditor::GetDoubleClickedNode();
    NodeEditor::End();

    if (!CurrentView->bInitialViewApplied)
    {
        NodeEditor::NavigateToContent(0.0f);
        CurrentView->bInitialViewApplied = true;
    }

    // Creating or deleting child graphs can move the graph array.
    Graph = Editor.GetCurrentGraph();

    if (Graph)
    {
        UpdateNodePositions(*Graph);
        UpdateSelection();
    }

    NodeEditor::SetCurrentEditor(PreviousContext);
    CurrentView = nullptr;
    NodeEditorContext = nullptr;
    RemoveDeletedGraphViews();

    if (DoubleClickedNode)
    {
        Editor.OpenNodeGraph(static_cast<int32>(DoubleClickedNode.Get()));
    }
}

void FAnimBlueprintGraphPanel::DrawNodes(FAnimBlueprintGraph& Graph)
{
    for (const FAnimBlueprintNode& Node : Graph.Nodes)
    {
        const NodeEditor::NodeId NodeId(Node.Id);

        if (CurrentView->PositionedNodeIds.find(Node.Id) == CurrentView->PositionedNodeIds.end())
        {
            NodeEditor::SetNodePosition(NodeId, ImVec2(Node.Position.X, Node.Position.Y));
            CurrentView->PositionedNodeIds.insert(Node.Id);
        }

        const bool bCurrentState = Node.Type == EAnimBlueprintNodeType::State && Editor.IsPreviewStateActive(Node.Id);

        if (bCurrentState)
        {
            NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, ImVec4(0.12f, 0.35f, 0.18f, 1.0f));
            NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBorder, ImVec4(0.3f, 1.0f, 0.5f, 1.0f));
        }

        NodeEditor::BeginNode(NodeId);

        ImGui::TextUnformatted(Node.Name.c_str());
        if (bCurrentState)
        {
            ImGui::TextUnformatted("Current State");
        }
        ImGui::Separator();

        if (Node.TransitionInputPinId != INDEX_NONE)
        {
            NodeEditor::BeginPin(NodeEditor::PinId(Node.TransitionInputPinId), NodeEditor::PinKind::Input);
            ImGui::TextUnformatted("o In");
            NodeEditor::EndPin();
        }

        if (Node.TransitionOutputPinId != INDEX_NONE)
        {
            ImGui::Dummy(ImVec2(100.0f, 0.0f));
            ImGui::SameLine();
            NodeEditor::BeginPin(NodeEditor::PinId(Node.TransitionOutputPinId), NodeEditor::PinKind::Output);
            ImGui::TextUnformatted("Out o");
            NodeEditor::EndPin();
        }

        if (Node.Type == EAnimBlueprintNodeType::State)
        {
            for (const FAnimBlueprintLink& Link : Graph.Links)
            {
                if (Link.ToNodeId == Node.Id)
                {
                    ImGui::TextUnformatted("Initial State");
                    break;
                }
            }
        }

        if (Node.InputPosePinId != INDEX_NONE)
        {
            NodeEditor::BeginPin(NodeEditor::PinId(Node.InputPosePinId), NodeEditor::PinKind::Input);
            ImGui::TextUnformatted(Node.Type == EAnimBlueprintNodeType::TwoWayBlend ? "o A" : "o Pose");
            NodeEditor::EndPin();
        }

        if (Node.SecondInputPosePinId != INDEX_NONE)
        {
            NodeEditor::BeginPin(NodeEditor::PinId(Node.SecondInputPosePinId), NodeEditor::PinKind::Input);
            ImGui::TextUnformatted("o B");
            NodeEditor::EndPin();
        }

        if (Node.OutputPosePinId != INDEX_NONE)
        {
            ImGui::Dummy(ImVec2(100.0f, 0.0f));
            ImGui::SameLine();

            NodeEditor::BeginPin(NodeEditor::PinId(Node.OutputPosePinId), NodeEditor::PinKind::Output);
            ImGui::TextUnformatted("Pose o");
            NodeEditor::EndPin();
        }

        ImGui::Dummy(ImVec2(160.0f, 12.0f));

        NodeEditor::EndNode();

        if (bCurrentState)
        {
            NodeEditor::PopStyleColor(2);
        }
    }
}

void FAnimBlueprintGraphPanel::DrawLinks(const FAnimBlueprintGraph& Graph)
{
    for (const FAnimBlueprintLink& Link : Graph.Links)
    {
        const ImVec4 Color = Graph.Type == EAnimBlueprintGraphType::StateMachine ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        NodeEditor::Link(NodeEditor::LinkId(Link.Id), NodeEditor::PinId(Link.FromPinId), NodeEditor::PinId(Link.ToPinId), Color);
    }

    for (const FAnimBlueprintTransition& Transition : Graph.Transitions)
    {
        const NodeEditor::LinkId LinkId(Transition.Id);
        const ImVec4 Color = Transition.Rule == EAnimBlueprintTransitionRule::Disabled ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
        NodeEditor::Link(LinkId, NodeEditor::PinId(Transition.FromPinId), NodeEditor::PinId(Transition.ToPinId), Color);

        if (NodeEditor::GetHoveredLink() == LinkId)
        {
            NodeEditor::Flow(LinkId);
        }
    }
}

void FAnimBlueprintGraphPanel::HandleLinkCreation()
{
    if (NodeEditor::BeginCreate())
    {
        NodeEditor::PinId FirstPin;
        NodeEditor::PinId SecondPin;

        if (NodeEditor::QueryNewLink(&FirstPin, &SecondPin))
        {
            if (FirstPin && SecondPin)
            {
                const int32 FirstPinId = static_cast<int32>(FirstPin.Get());
                const int32 SecondPinId = static_cast<int32>(SecondPin.Get());

                if (Editor.CanConnectPins(FirstPinId, SecondPinId))
                {
                    if (NodeEditor::AcceptNewItem())
                    {
                        Editor.ConnectPins(FirstPinId, SecondPinId);
                    }
                }
                else
                {
                    NodeEditor::RejectNewItem();
                }
            }
        }
    }

    NodeEditor::EndCreate();
}

void FAnimBlueprintGraphPanel::HandleDeletion()
{
    if (NodeEditor::BeginDelete())
    {
        NodeEditor::LinkId LinkId;

        while (NodeEditor::QueryDeletedLink(&LinkId))
        {
            if (NodeEditor::AcceptDeletedItem())
            {
                Editor.RemoveLink(static_cast<int32>(LinkId.Get()));
            }
        }

        NodeEditor::NodeId NodeId;

        while (NodeEditor::QueryDeletedNode(&NodeId))
        {
            const int32 Id = static_cast<int32>(NodeId.Get());

            if (Editor.CanRemoveNode(Id))
            {
                if (NodeEditor::AcceptDeletedItem())
                {
                    Editor.RemoveNode(Id);
                    CurrentView->PositionedNodeIds.erase(Id);
                }
            }
            else
            {
                NodeEditor::RejectDeletedItem();
            }
        }
    }

    NodeEditor::EndDelete();
}

void FAnimBlueprintGraphPanel::DrawCreationMenu()
{
    const bool bOpenMenu = NodeEditor::ShowBackgroundContextMenu();

    // Restore screen-space mouse coordinates before converting to canvas space.
    NodeEditor::Suspend();

    if (bOpenMenu)
    {
        const ImVec2 Position = NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
        CreationPosition.X = Position.x;
        CreationPosition.Y = Position.y;
    }

    if (bOpenMenu)
    {
        ImGui::OpenPopup("CreateAnimBlueprintNode");
    }

    if (ImGui::BeginPopup("CreateAnimBlueprintNode"))
    {
        const FAnimBlueprintGraph* Graph = Editor.GetCurrentGraph();
        const bool bStateMachine = Graph && Graph->Type == EAnimBlueprintGraphType::StateMachine;

        if (bStateMachine)
        {
            bool bHasAnyState = false;

            for (const FAnimBlueprintNode& Node : Graph->Nodes)
            {
                if (Node.Type == EAnimBlueprintNodeType::AnyState)
                {
                    bHasAnyState = true;
                    break;
                }
            }

            if (ImGui::MenuItem("State"))
            {
                Editor.AddState(CreationPosition);
            }

            if (ImGui::MenuItem("Any State", nullptr, false, !bHasAnyState))
            {
                Editor.AddAnyState(CreationPosition);
            }
        }
        else
        {
            if (ImGui::MenuItem("Sequence Player"))
            {
                Editor.AddSequencePlayer(CreationPosition);
            }

            if (ImGui::MenuItem("BlendSpace Player"))
            {
                Editor.AddPoseNode(EAnimBlueprintNodeType::BlendSpacePlayer, CreationPosition);
            }

            if (ImGui::MenuItem("Two Way Blend"))
            {
                Editor.AddPoseNode(EAnimBlueprintNodeType::TwoWayBlend, CreationPosition);
            }

            if (ImGui::MenuItem("State Machine"))
            {
                Editor.AddPoseNode(EAnimBlueprintNodeType::StateMachine, CreationPosition);
            }
        }

        ImGui::EndPopup();
    }

    NodeEditor::Resume();
}

void FAnimBlueprintGraphPanel::UpdateNodePositions(FAnimBlueprintGraph& Graph)
{
    for (FAnimBlueprintNode& Node : Graph.Nodes)
    {
        if (CurrentView->PositionedNodeIds.find(Node.Id) == CurrentView->PositionedNodeIds.end())
        {
            continue;
        }

        const ImVec2 Position = NodeEditor::GetNodePosition(NodeEditor::NodeId(Node.Id));
        Node.Position.X = Position.x;
        Node.Position.Y = Position.y;
    }
}

void FAnimBlueprintGraphPanel::UpdateSelection()
{
    NodeEditor::NodeId SelectedNodes[1];
    const int32 SelectedCount = NodeEditor::GetSelectedNodes(SelectedNodes, 1);

    if (SelectedCount > 0)
    {
        Editor.SelectNode(static_cast<int32>(SelectedNodes[0].Get()));
        return;
    }

    NodeEditor::LinkId SelectedLinks[1];

    if (NodeEditor::GetSelectedLinks(SelectedLinks, 1) > 0)
    {
        Editor.SelectLink(static_cast<int32>(SelectedLinks[0].Get()));
    }
    else
    {
        Editor.SelectNode(INDEX_NONE);
    }
}

bool FAnimBlueprintGraphPanel::SaveViewSettings(const char* Data, size_t Size, NodeEditor::SaveReasonFlags Reason, void* UserPointer)
{
    FGraphView* View = static_cast<FGraphView*>(UserPointer);
    FAnimBlueprintGraph* Graph = View->Owner->FindGraph(View->GraphId);

    if (!Graph)
    {
        return true;
    }

    const json Settings = json::parse(FString(Data, Size), nullptr, false);

    if (Settings.is_discarded() || !Settings.contains("view"))
    {
        return false;
    }

    // Positions belong to our nodes. Keep only navigation, without transient selection.
    Graph->EditorSettings = json({ { "view", Settings.at("view") } }).dump();
    return true;
}

size_t FAnimBlueprintGraphPanel::LoadViewSettings(char* Data, void* UserPointer)
{
    FGraphView* View = static_cast<FGraphView*>(UserPointer);
    const FAnimBlueprintGraph* Graph = View->Owner->FindGraph(View->GraphId);

    if (!Graph)
    {
        return 0;
    }

    if (Data && !Graph->EditorSettings.empty())
    {
        std::memcpy(Data, Graph->EditorSettings.data(), Graph->EditorSettings.size());
    }

    return Graph->EditorSettings.size();
}
