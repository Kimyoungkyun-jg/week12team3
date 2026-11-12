#include "EnginePCH.h"
#include "AnimBlueprintEditor.h"
#include "AnimBlueprintEditorTab.h"
#include "Animation/AnimBlueprintFile.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Core/Misc/Paths.h"
#include "Editor/AssetEditor/EditorManager.h"
#include <commdlg.h>

#include <cmath>
#include <cctype>

#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Render/SkeletalMesh.h"
#include "UObject/Casts.h"
#include "UObject/UObjectGlobals.h"

#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Core/NameTypes.h"

#include "Animation/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_TwoWayBlend.h"

#include "Components/SkeletalMeshComponent.h"

FAnimBlueprintEditor::FAnimBlueprintEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, 
    const FString& InPreviewMeshPath)
    : FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InPreviewMeshPath + ".animblueprint-preview")
{
    BlueprintData.PreviewMeshPath = InPreviewMeshPath;
}

FAnimBlueprintEditor::~FAnimBlueprintEditor()
{
}

bool FAnimBlueprintEditor::InitializeResources()
{
    const FString& PreviewPath = BlueprintData.PreviewMeshPath;
    const FString FilePath = FAssetUtils::ResolveAssetFilePath(PreviewPath);

    FString Error;
    Mesh = Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(FilePath.empty() ? PreviewPath : FilePath, Error));

    if (!Mesh)
    {
        HTR_LOG(Warning, "AnimBlueprint preview mesh failed: {} ({})", PreviewPath, Error);
        return false;
    }

    if (!InitializePreview())
    {
        return false;
    }

    if (!bDocumentLoaded)
    {
        CreateInitialGraph();
    }
    else
    {
        CurrentGraphId = BlueprintData.RootGraphId;
        FString Error;
        AnimBlueprintFile::ToJson(BlueprintData, SavedSnapshot, Error);
    }

    return true;
}

TUniquePtr<IEditorTab> FAnimBlueprintEditor::CreateTab()
{
    return MakeUnique<FAnimBlueprintEditorTab>(*this);
}

void FAnimBlueprintEditor::ShutdownResources()
{
    bApplyPreviewRequested = false;

    if (MeshComponent && PreviewInstance && MeshComponent->GetAnimInstance() == PreviewInstance.get())
    {
        MeshComponent->SetAnimInstance(nullptr);
    }

    PreviewInstance.reset();
    PreviewNodeIndices.Empty();
    AppliedVariables.Empty();
    PreviewMessage.clear();

    FSkeletalPreviewEditor::ShutdownResources();
}

void FAnimBlueprintEditor::RequestApplyPreview()
{
    bApplyPreviewRequested = true;
}

void FAnimBlueprintEditor::TickBeforeWorld(float DeltaTime)
{
    if (bSaveRequested)
    {
        bSaveRequested = false;
        Save();
    }

    if (bApplyPreviewRequested)
    {
        bApplyPreviewRequested = false;
        ApplyPreview();
    }

    ApplyPreviewRootMotion();
}

void FAnimBlueprintEditor::ApplyPreviewRootMotion()
{
    if (!MeshComponent)
    {
        return;
    }

    if (!bHasPreviewBase)
    {
        PreviewBaseTransform = MeshComponent->GetTransform();
        bHasPreviewBase = true;
    }

    // 1. 지난 Tick까지 쌓인 이동량(월드 공간)을 가져간다. 모드가 NoRootMotionExtraction / IgnoreRootMotion이면 없다
    if (!MeshComponent->HasPendingRootMotion())
    {
        return;
    }
    const FTransform Delta = MeshComponent->ConsumeRootMotion();

    // 2. 미리보기 메시를 그만큼 옮긴다
    FTransform Moved = MeshComponent->GetTransform();
    Moved.Location = Moved.Location + Delta.Location;
    Moved.Rotation = (Delta.Rotation * Moved.Rotation).Normalized();

    // 3. 처음 위치에서 너무 멀어지면 되돌린다 (Unreal은 바닥 범위를 넘으면 되돌린다)
    //    메시 단위(cm/m)에 상관없게 메시 키의 3배로 잡는다
    const FBox Bounds = MeshComponent->CalcBounds();
    const float MeshHeight = Bounds.Max.Z - Bounds.Min.Z;
    const float MaxPreviewDistance = (MeshHeight > 0.0f ? MeshHeight : 1.0f) * 3.0f;
    const FVector FromBase = Moved.Location - PreviewBaseTransform.Location;
    if (FromBase.X * FromBase.X + FromBase.Y * FromBase.Y > MaxPreviewDistance * MaxPreviewDistance)
    {
        Moved = PreviewBaseTransform;
    }
    MeshComponent->SetTransform(Moved);
}

void FAnimBlueprintEditor::ResetPreviewRootMotion()
{
    if (!MeshComponent)
    {
        return;
    }

    // 쌓여 있던 이동량은 버리고 처음 위치로 돌린다
    MeshComponent->ConsumeRootMotion();
    if (bHasPreviewBase)
    {
        MeshComponent->SetTransform(PreviewBaseTransform);
    }
}

bool FAnimBlueprintEditor::SetPreviewError(int32 NodeId, const FString& Message)
{
    PreviewMessage = Message;

    if (NodeId != INDEX_NONE)
    {
        PreviewMessage += " (ID " + std::to_string(NodeId) + ")";
    }

    return false;
}

bool FAnimBlueprintEditor::ApplyPreview()
{
    if (!MeshComponent || !Mesh)
    {
        return SetPreviewError(INDEX_NONE, "The preview mesh is not available.");
    }

    TUniquePtr<UAnimGraphInstance> NewInstance;
    AnimBlueprintCompiler::FNodeMap NewNodeIndices;

    if (!AnimBlueprintCompiler::Build(BlueprintData, *MeshComponent, NewInstance, NewNodeIndices, PreviewMessage))
    {
        return false;
    }

    MeshComponent->SetAnimInstance(NewInstance.get());
    PreviewInstance = std::move(NewInstance);
    PreviewNodeIndices = std::move(NewNodeIndices);
    AppliedVariables = BlueprintData.Variables;

    MeshComponent->RefreshAnimationPose();
    ResetPreviewRootMotion();   // 재생이 처음부터 다시 시작되므로 위치도 처음으로
    PreviewMessage = "Preview applied. Playback restarted from the beginning.";

    return true;
}

void FAnimBlueprintEditor::CreateInitialGraph()
{
    BlueprintData.Graphs.Empty();
    BlueprintData.Variables.Empty();
    BlueprintData.NextId = 1;
    SelectedNodeId = INDEX_NONE;

    FAnimBlueprintGraph Graph;
    Graph.Id = BlueprintData.AllocateId();
    Graph.Name = "AnimGraph";
    Graph.Type = EAnimBlueprintGraphType::Pose;

    FAnimBlueprintNode Player;
    Player.Id = BlueprintData.AllocateId();
    Player.Type = EAnimBlueprintNodeType::SequencePlayer;
    Player.Name = "Sequence Player";
    Player.Position.X = 40.0f;
    Player.Position.Y = 100.0f;
    Player.OutputPosePinId = BlueprintData.AllocateId();
    Graph.Nodes.Add(Player);

    FAnimBlueprintNode Output;
    Output.Id = BlueprintData.AllocateId();
    Output.Type = EAnimBlueprintNodeType::OutputPose;
    Output.Name = "Output Pose";
    Output.Position.X = 360.0f;
    Output.Position.Y = 100.0f;
    Output.InputPosePinId = BlueprintData.AllocateId();
    Graph.Nodes.Add(Output);

    BlueprintData.RootGraphId = Graph.Id;
    CurrentGraphId = Graph.Id;
    BlueprintData.Graphs.Add(Graph);

    FAnimBlueprintVariable GroundSpeed;
    GroundSpeed.Name = "GroundSpeed";
    GroundSpeed.Type = EAnimBlueprintVariableType::Float;
    BlueprintData.Variables.Add(GroundSpeed);
}

FAnimBlueprintGraph* FAnimBlueprintEditor::GetCurrentGraph()
{
    return FindGraph(CurrentGraphId);
}

FAnimBlueprintGraph* FAnimBlueprintEditor::FindGraph(int32 GraphId)
{
    for (FAnimBlueprintGraph& Graph : BlueprintData.Graphs)
    {
        if (Graph.Id == GraphId)
        {
            return &Graph;
        }
    }

    return nullptr;
}

FAnimBlueprintNode* FAnimBlueprintEditor::FindNode(int32 NodeId)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return nullptr;
    }

    for (FAnimBlueprintNode& Node : Graph->Nodes)
    {
        if (Node.Id == NodeId)
        {
            return &Node;
        }
    }

    return nullptr;
}

FAnimBlueprintNode* FAnimBlueprintEditor::GetSelectedNode()
{
    return FindNode(SelectedNodeId);
}

void FAnimBlueprintEditor::SelectNode(int32 NodeId)
{
    SelectedNodeId = INDEX_NONE;
    SelectedLinkId = INDEX_NONE;

    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return;
    }

    for (const FAnimBlueprintNode& Node : Graph->Nodes)
    {
        if (Node.Id == NodeId)
        {
            SelectedNodeId = NodeId;
            return;
        }
    }
}

int32 FAnimBlueprintEditor::AddSequencePlayer(const FAnimBlueprintPosition& Position)
{
    return AddPoseNode(EAnimBlueprintNodeType::SequencePlayer, Position);
}

int32 FAnimBlueprintEditor::AddPoseNode(EAnimBlueprintNodeType Type, const FAnimBlueprintPosition& Position)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph || Graph->Type != EAnimBlueprintGraphType::Pose)
    {
        return INDEX_NONE;
    }

    const int32 ParentGraphId = Graph->Id;

    FAnimBlueprintNode Node;
    Node.Type = Type;
    Node.Position = Position;

    switch (Type)
    {
    case EAnimBlueprintNodeType::SequencePlayer:
        Node.Name = "Sequence Player";
        break;

    case EAnimBlueprintNodeType::BlendSpacePlayer:
        Node.Name = "BlendSpace Player";
        break;

    case EAnimBlueprintNodeType::StateMachine:
        Node.Name = "State Machine";
        break;

    case EAnimBlueprintNodeType::TwoWayBlend:
        Node.Name = "Two Way Blend";
        Node.InputPosePinId = BlueprintData.AllocateId();
        Node.SecondInputPosePinId = BlueprintData.AllocateId();
        Node.FloatInput.Constant = 0.5f;
        break;

    default:
        return INDEX_NONE;
    }

    Node.Id = BlueprintData.AllocateId();
    Node.OutputPosePinId = BlueprintData.AllocateId();
    const int32 NodeId = Node.Id;

    if (Type == EAnimBlueprintNodeType::StateMachine)
    {
        Node.ChildGraphId = CreateChildGraph(ParentGraphId, NodeId, Node.Name, EAnimBlueprintGraphType::StateMachine);
        Graph = FindGraph(ParentGraphId);
    }

    Graph->Nodes.Add(Node);
    return NodeId;
}

FAnimBlueprintNode* FAnimBlueprintEditor::FindNodeByPin(int32 PinId)
{
    if (PinId <= 0)
    {
        return nullptr;
    }

    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return nullptr;
    }

    for (FAnimBlueprintNode& Node : Graph->Nodes)
    {
        if (Node.InputPosePinId == PinId 
            || Node.SecondInputPosePinId == PinId 
            || Node.OutputPosePinId == PinId 
            || Node.TransitionInputPinId == PinId 
            || Node.TransitionOutputPinId == PinId)
        {
            return &Node;
        }
    }

    return nullptr;
}

bool FAnimBlueprintEditor::ResolvePoseConnection(int32 FirstPinId, int32 SecondPinId, FAnimBlueprintLink& OutConnection)
{
    FAnimBlueprintNode* FirstNode = FindNodeByPin(FirstPinId);
    FAnimBlueprintNode* SecondNode = FindNodeByPin(SecondPinId);

    if (!FirstNode || !SecondNode || FirstNode == SecondNode)
    {
        return false;
    }

    const bool bFirstIsOutput = FirstNode->OutputPosePinId == FirstPinId;
    const bool bSecondIsOutput = SecondNode->OutputPosePinId == SecondPinId;

    if (bFirstIsOutput == bSecondIsOutput)
    {
        return false;
    }

    FAnimBlueprintNode* SourceNode = FirstNode;
    FAnimBlueprintNode* TargetNode = SecondNode;
    int32 SourcePinId = FirstPinId;
    int32 TargetPinId = SecondPinId;

    if (!bFirstIsOutput)
    {
        SourceNode = SecondNode;
        TargetNode = FirstNode;
        SourcePinId = SecondPinId;
        TargetPinId = FirstPinId;
    }

    if (SourceNode->OutputPosePinId <= 0 || SourceNode->OutputPosePinId != SourcePinId)
    {
        return false;
    }

    if (TargetNode->Type != EAnimBlueprintNodeType::OutputPose 
        && TargetNode->Type != EAnimBlueprintNodeType::StateResult 
        && TargetNode->Type != EAnimBlueprintNodeType::TwoWayBlend)
    {
        return false;
    }

    if (HasPosePath(TargetNode->Id, SourceNode->Id))
    {
        return false;
    }

    OutConnection.FromNodeId = SourceNode->Id;
    OutConnection.FromPinId = SourcePinId;
    OutConnection.ToNodeId = TargetNode->Id;
    OutConnection.ToPinId = TargetPinId;

    return true;
}

bool FAnimBlueprintEditor::CanConnectPosePins(int32 FirstPinId, int32 SecondPinId)
{
    FAnimBlueprintLink Connection;
    return ResolvePoseConnection(FirstPinId, SecondPinId, Connection);
}

bool FAnimBlueprintEditor::ConnectPosePins(int32 FirstPinId, int32 SecondPinId)
{
    FAnimBlueprintLink Connection;

    if (!ResolvePoseConnection(FirstPinId, SecondPinId, Connection))
    {
        return false;
    }

    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return false;
    }

    for (int32 Index = Graph->Links.Num() - 1; Index >= 0; --Index)
    {
        if (Graph->Links[Index].ToPinId == Connection.ToPinId)
        {
            Graph->Links.RemoveAt(Index);
        }
    }

    Connection.Id = BlueprintData.AllocateId();
    Graph->Links.Add(Connection);

    return true;
}

bool FAnimBlueprintEditor::CanRemoveNode(int32 NodeId)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return false;
    }

    for (const FAnimBlueprintNode& Node : Graph->Nodes)
    {
        if (Node.Id == NodeId)
        {
            return Node.Type == EAnimBlueprintNodeType::SequencePlayer 
                || Node.Type == EAnimBlueprintNodeType::BlendSpacePlayer 
                || Node.Type == EAnimBlueprintNodeType::TwoWayBlend 
                || Node.Type == EAnimBlueprintNodeType::StateMachine 
                || Node.Type == EAnimBlueprintNodeType::State || Node.Type == EAnimBlueprintNodeType::AnyState;
        }
    }

    return false;
}

bool FAnimBlueprintEditor::RemoveNode(int32 NodeId)
{
    if (!CanRemoveNode(NodeId))
    {
        return false;
    }

    FAnimBlueprintGraph* Graph = GetCurrentGraph();
    int32 ChildGraphId = INDEX_NONE;

    for (int32 Index = Graph->Links.Num() - 1; Index >= 0; --Index)
    {
        const FAnimBlueprintLink& Link = Graph->Links[Index];

        if (Link.FromNodeId == NodeId || Link.ToNodeId == NodeId)
        {
            Graph->Links.RemoveAt(Index);
        }
    }

    for (int32 Index = Graph->Transitions.Num() - 1; Index >= 0; --Index)
    {
        const FAnimBlueprintTransition& Transition = Graph->Transitions[Index];

        if (Transition.FromStateId == NodeId || Transition.ToStateId == NodeId)
        {
            Graph->Transitions.RemoveAt(Index);
        }
    }

    for (int32 Index = 0; Index < Graph->Nodes.Num(); ++Index)
    {
        if (Graph->Nodes[Index].Id == NodeId)
        {
            ChildGraphId = Graph->Nodes[Index].ChildGraphId;
            Graph->Nodes.RemoveAt(Index);
            break;
        }
    }

    SelectedNodeId = INDEX_NONE;
    SelectedLinkId = INDEX_NONE;

    if (ChildGraphId != INDEX_NONE)
    {
        RemoveChildGraphs(ChildGraphId);
    }

    return true;
}

bool FAnimBlueprintEditor::RemoveLink(int32 LinkId)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return false;
    }

    for (int32 Index = 0; Index < Graph->Links.Num(); ++Index)
    {
        if (Graph->Links[Index].Id == LinkId)
        {
            Graph->Links.RemoveAt(Index);
            SelectedLinkId = INDEX_NONE;
            return true;
        }
    }

    for (int32 Index = 0; Index < Graph->Transitions.Num(); ++Index)
    {
        if (Graph->Transitions[Index].Id == LinkId)
        {
            Graph->Transitions.RemoveAt(Index);
            SelectedLinkId = INDEX_NONE;
            return true;
        }
    }

    return false;
}

bool FAnimBlueprintEditor::HasPosePath(int32 FromNodeId, int32 ToNodeId)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return false;
    }

    TArray<int32> Pending;
    TArray<int32> Visited;
    Pending.Add(FromNodeId);

    while (!Pending.IsEmpty())
    {
        const int32 NodeId = Pending[Pending.Num() - 1];
        Pending.RemoveLast();

        if (NodeId == ToNodeId)
        {
            return true;
        }

        if (Visited.Find(NodeId) != INDEX_NONE)
        {
            continue;
        }

        Visited.Add(NodeId);

        for (const FAnimBlueprintLink& Link : Graph->Links)
        {
            if (Link.FromNodeId == NodeId)
            {
                Pending.Add(Link.ToNodeId);
            }
        }
    }

    return false;
}

bool FAnimBlueprintEditor::ValidateVariableName(const FString& Name, const FString& PreviousName)
{
    if (Name.empty() || Name.size() >= 128)
    {
        VariableMessage = "Use a variable name with 1 to 127 characters.";
        return false;
    }

    if (!std::isalpha(static_cast<unsigned char>(Name[0])) && Name[0] != '_')
    {
        VariableMessage = "Start the name with a letter or underscore.";
        return false;
    }

    for (unsigned char Character : Name)
    {
        if (!std::isalnum(Character) && Character != '_')
        {
            VariableMessage = "Use letters, numbers and underscores.";
            return false;
        }
    }

    for (const FAnimBlueprintVariable& Variable : BlueprintData.Variables)
    {
        if (Variable.Name == Name && Variable.Name != PreviousName)
        {
            VariableMessage = "A variable with this name already exists.";
            return false;
        }
    }

    VariableMessage.clear();
    return true;
}

bool FAnimBlueprintEditor::AddVariable(const FString& Name, EAnimBlueprintVariableType Type)
{
    if (!ValidateVariableName(Name, FString()))
    {
        return false;
    }

    FAnimBlueprintVariable Variable;
    Variable.Name = Name;
    Variable.Type = Type;
    BlueprintData.Variables.Add(Variable);
    return true;
}

bool FAnimBlueprintEditor::RenameVariable(const FString& PreviousName, const FString& Name)
{
    if (!ValidateVariableName(Name, PreviousName))
    {
        return false;
    }

    for (FAnimBlueprintVariable& Variable : BlueprintData.Variables)
    {
        if (Variable.Name == PreviousName)
        {
            Variable.Name = Name;

            for (FAnimBlueprintGraph& Graph : BlueprintData.Graphs)
            {
                for (FAnimBlueprintTransition& Transition : Graph.Transitions)
                {
                    if (Transition.Variable == PreviousName)
                    {
                        Transition.Variable = Name;
                    }
                }

                for (FAnimBlueprintNode& Node : Graph.Nodes)
                {
                    if (Node.FloatInput.Variable == PreviousName)
                    {
                        Node.FloatInput.Variable = Name;
                    }

                    for (FAnimBlueprintStateEvent* Event : { &Node.OnEnter, &Node.OnExit })
                    {
                        for (FAnimBlueprintVariableAssignment& Assignment : Event->Assignments)
                        {
                            if (Assignment.Variable == PreviousName)
                            {
                                Assignment.Variable = Name;
                            }
                        }
                    }
                }
            }

            return true;
        }
    }

    return false;
}

void FAnimBlueprintEditor::RemoveVariable(const FString& Name)
{
    for (int32 Index = 0; Index < BlueprintData.Variables.Num(); ++Index)
    {
        if (BlueprintData.Variables[Index].Name == Name)
        {
            BlueprintData.Variables.RemoveAt(Index);
            break;
        }
    }

    for (FAnimBlueprintGraph& Graph : BlueprintData.Graphs)
    {
        for (FAnimBlueprintTransition& Transition : Graph.Transitions)
        {
            if (Transition.Variable == Name)
            {
                Transition.Variable.clear();
                Transition.Rule = EAnimBlueprintTransitionRule::Disabled;
            }
        }

        for (FAnimBlueprintNode& Node : Graph.Nodes)
        {
            if (Node.FloatInput.Variable == Name)
            {
                Node.FloatInput.Variable.clear();
                Node.FloatInput.bUseVariable = false;
            }

            for (FAnimBlueprintStateEvent* Event : { &Node.OnEnter, &Node.OnExit })
            {
                for (int32 Index = Event->Assignments.Num() - 1; Index >= 0; --Index)
                {
                    if (Event->Assignments[Index].Variable == Name)
                    {
                        Event->Assignments.RemoveAt(Index);
                    }
                }
            }
        }
    }

    VariableMessage = "Variable removed. Bound inputs use constants and affected transitions are disabled. Apply Preview to update playback.";
}

float FAnimBlueprintEditor::GetPreviewFloat(const FString& Name) const
{
    return PreviewInstance ? PreviewInstance->GetFloat(Name) : 0.0f;
}

bool FAnimBlueprintEditor::GetPreviewBool(const FString& Name) const
{
    return PreviewInstance ? PreviewInstance->GetBool(Name) : false;
}

void FAnimBlueprintEditor::SetPreviewFloat(const FString& Name, float Value)
{
    if (PreviewInstance && std::isfinite(Value))
    {
        PreviewInstance->SetFloat(Name, Value);
    }
}

void FAnimBlueprintEditor::SetPreviewBool(const FString& Name, bool bValue)
{
    if (PreviewInstance)
    {
        PreviewInstance->SetBool(Name, bValue);
    }
}

int32 FAnimBlueprintEditor::CreateChildGraph(int32 ParentGraphId, int32 OwnerNodeId, const FString& Name, 
    EAnimBlueprintGraphType Type)
{
    FAnimBlueprintGraph Graph;
    Graph.Id = BlueprintData.AllocateId();
    Graph.Name = Name;
    Graph.Type = Type;
    Graph.ParentGraphId = ParentGraphId;
    Graph.OwnerNodeId = OwnerNodeId;

    FAnimBlueprintNode Root;
    Root.Id = BlueprintData.AllocateId();
    Root.Position.X = 40.0f;
    Root.Position.Y = 100.0f;

    if (Type == EAnimBlueprintGraphType::StateMachine)
    {
        Root.Type = EAnimBlueprintNodeType::Entry;
        Root.Name = "Entry";
        Root.TransitionOutputPinId = BlueprintData.AllocateId();
    }
    else
    {
        Root.Type = EAnimBlueprintNodeType::StateResult;
        Root.Name = "State Result";
        Root.Position.X = 360.0f;
        Root.InputPosePinId = BlueprintData.AllocateId();
    }

    Graph.Nodes.Add(Root);
    const int32 GraphId = Graph.Id;
    BlueprintData.Graphs.Add(Graph);
    return GraphId;
}

int32 FAnimBlueprintEditor::AddState(const FAnimBlueprintPosition& Position)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph || Graph->Type != EAnimBlueprintGraphType::StateMachine)
    {
        return INDEX_NONE;
    }

    const int32 ParentGraphId = Graph->Id;
    FAnimBlueprintNode State;
    State.Id = BlueprintData.AllocateId();
    State.Type = EAnimBlueprintNodeType::State;
    State.Name = "State " + std::to_string(State.Id);
    State.Position = Position;
    State.TransitionInputPinId = BlueprintData.AllocateId();
    State.TransitionOutputPinId = BlueprintData.AllocateId();
    State.ChildGraphId = CreateChildGraph(ParentGraphId, State.Id, State.Name, EAnimBlueprintGraphType::Pose);

    Graph = FindGraph(ParentGraphId);
    Graph->Nodes.Add(State);
    return State.Id;
}

TArray<int32> FAnimBlueprintEditor::GetGraphPath() const
{
    TArray<int32> ReversePath;
    int32 GraphId = CurrentGraphId;

    while (GraphId != INDEX_NONE && ReversePath.Find(GraphId) == INDEX_NONE)
    {
        const FAnimBlueprintGraph* Found = nullptr;

        for (const FAnimBlueprintGraph& Graph : BlueprintData.Graphs)
        {
            if (Graph.Id == GraphId)
            {
                Found = &Graph;
                break;
            }
        }

        if (!Found)
        {
            break;
        }

        ReversePath.Add(GraphId);
        GraphId = Found->ParentGraphId;
    }

    TArray<int32> Path;
    for (int32 Index = ReversePath.Num() - 1; Index >= 0; --Index)
    {
        Path.Add(ReversePath[Index]);
    }

    return Path;
}

bool FAnimBlueprintEditor::NavigateToGraph(int32 GraphId)
{
    if (!FindGraph(GraphId))
    {
        return false;
    }

    CurrentGraphId = GraphId;
    SelectedNodeId = INDEX_NONE;
    SelectedLinkId = INDEX_NONE;
    return true;
}

bool FAnimBlueprintEditor::OpenNodeGraph(int32 NodeId)
{
    const FAnimBlueprintNode* Node = FindNode(NodeId);

    if (!Node || (Node->Type != EAnimBlueprintNodeType::StateMachine && Node->Type != EAnimBlueprintNodeType::State))
    {
        return false;
    }

    return NavigateToGraph(Node->ChildGraphId);
}

bool FAnimBlueprintEditor::RenameNode(int32 NodeId, const FString& Name)
{
    FAnimBlueprintNode* Node = FindNode(NodeId);
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Node || !Graph || (Node->Type != EAnimBlueprintNodeType::State && Node->Type != EAnimBlueprintNodeType::StateMachine))
    {
        return false;
    }

    if (Name.size() >= 128 || Name.find_first_not_of(" \t\r\n") == FString::npos)
    {
        return SetPreviewError(NodeId, "Use a non-empty name with fewer than 128 bytes.");
    }

    for (const FAnimBlueprintNode& Other : Graph->Nodes)
    {
        if (Other.Id != NodeId && Other.Name == Name)
        {
            return SetPreviewError(NodeId, "A node with this name already exists in the graph.");
        }
    }

    Node->Name = Name;
    FAnimBlueprintGraph* Child = FindGraph(Node->ChildGraphId);

    if (Child)
    {
        Child->Name = Name;
    }

    return true;
}

void FAnimBlueprintEditor::SelectLink(int32 LinkId)
{
    SelectedNodeId = INDEX_NONE;
    SelectedLinkId = LinkId;
}

FAnimBlueprintTransition* FAnimBlueprintEditor::GetSelectedTransition()
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (Graph)
    {
        for (FAnimBlueprintTransition& Transition : Graph->Transitions)
        {
            if (Transition.Id == SelectedLinkId)
            {
                return &Transition;
            }
        }
    }

    return nullptr;
}

bool FAnimBlueprintEditor::ResolveStateConnection(int32 FirstPinId, int32 SecondPinId, FAnimBlueprintLink& OutConnection)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();
    FAnimBlueprintNode* First = FindNodeByPin(FirstPinId);
    FAnimBlueprintNode* Second = FindNodeByPin(SecondPinId);

    if (!Graph || Graph->Type != EAnimBlueprintGraphType::StateMachine || !First || !Second || First == Second)
    {
        return false;
    }

    const bool bFirstOutput = First->TransitionOutputPinId == FirstPinId;
    const bool bSecondOutput = Second->TransitionOutputPinId == SecondPinId;

    if (bFirstOutput == bSecondOutput)
    {
        return false;
    }

    const FAnimBlueprintNode* Source = bFirstOutput ? First : Second;
    const FAnimBlueprintNode* Target = bFirstOutput ? Second : First;

    if ((Source->Type != EAnimBlueprintNodeType::Entry 
        && Source->Type != EAnimBlueprintNodeType::State && Source->Type != EAnimBlueprintNodeType::AnyState) 
        || Target->Type != EAnimBlueprintNodeType::State)
    {
        return false;
    }

    for (const FAnimBlueprintTransition& Transition : Graph->Transitions)
    {
        if (Transition.FromStateId == Source->Id && Transition.ToStateId == Target->Id)
        {
            return false;
        }
    }

    OutConnection.FromNodeId = Source->Id;
    OutConnection.FromPinId = Source->TransitionOutputPinId;
    OutConnection.ToNodeId = Target->Id;
    OutConnection.ToPinId = Target->TransitionInputPinId;
    return true;
}

bool FAnimBlueprintEditor::CanConnectPins(int32 FirstPinId, int32 SecondPinId)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return false;
    }

    if (Graph->Type == EAnimBlueprintGraphType::Pose)
    {
        return CanConnectPosePins(FirstPinId, SecondPinId);
    }

    FAnimBlueprintLink Connection;
    return ResolveStateConnection(FirstPinId, SecondPinId, Connection);
}

bool FAnimBlueprintEditor::ConnectPins(int32 FirstPinId, int32 SecondPinId)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph)
    {
        return false;
    }

    if (Graph->Type == EAnimBlueprintGraphType::Pose)
    {
        return ConnectPosePins(FirstPinId, SecondPinId);
    }

    FAnimBlueprintLink Connection;

    if (!ResolveStateConnection(FirstPinId, SecondPinId, Connection))
    {
        return false;
    }

    Connection.Id = BlueprintData.AllocateId();
    const FAnimBlueprintNode* Source = FindNode(Connection.FromNodeId);

    if (Source->Type == EAnimBlueprintNodeType::Entry)
    {
        Graph->Links.Empty();
        Graph->Links.Add(Connection);
    }
    else
    {
        FAnimBlueprintTransition Transition;
        Transition.Id = Connection.Id;
        Transition.FromStateId = Connection.FromNodeId;
        Transition.ToStateId = Connection.ToNodeId;
        Transition.FromPinId = Connection.FromPinId;
        Transition.ToPinId = Connection.ToPinId;
        Graph->Transitions.Add(Transition);
    }

    return true;
}

void FAnimBlueprintEditor::RemoveChildGraphs(int32 GraphId)
{
    TArray<int32> RemovedIds;
    RemovedIds.Add(GraphId);

    for (int32 Index = 0; Index < RemovedIds.Num(); ++Index)
    {
        for (const FAnimBlueprintGraph& Graph : BlueprintData.Graphs)
        {
            if (Graph.ParentGraphId == RemovedIds[Index] && RemovedIds.Find(Graph.Id) == INDEX_NONE)
            {
                RemovedIds.Add(Graph.Id);
            }
        }
    }

    for (int32 Index = BlueprintData.Graphs.Num() - 1; Index >= 0; --Index)
    {
        if (RemovedIds.Find(BlueprintData.Graphs[Index].Id) != INDEX_NONE)
        {
            BlueprintData.Graphs.RemoveAt(Index);
        }
    }
}

int32 FAnimBlueprintEditor::AddAnyState(const FAnimBlueprintPosition& Position)
{
    FAnimBlueprintGraph* Graph = GetCurrentGraph();

    if (!Graph || Graph->Type != EAnimBlueprintGraphType::StateMachine)
    {
        return INDEX_NONE;
    }

    for (const FAnimBlueprintNode& Node : Graph->Nodes)
    {
        if (Node.Type == EAnimBlueprintNodeType::AnyState)
        {
            return Node.Id;
        }
    }

    FAnimBlueprintNode Node;
    Node.Id = BlueprintData.AllocateId();
    Node.Name = "Any State";
    Node.Type = EAnimBlueprintNodeType::AnyState;
    Node.Position = Position;
    Node.TransitionOutputPinId = BlueprintData.AllocateId();
    Graph->Nodes.Add(Node);
    return Node.Id;
}

bool FAnimBlueprintEditor::IsPreviewStateActive(int32 NodeId) const
{
    if (!PreviewInstance)
    {
        return false;
    }

    const TArray<int32>* PoseIndices = PreviewNodeIndices.Find(NodeId);

    if (!PoseIndices)
    {
        return false;
    }

    for (const FAnimBlueprintGraph& Graph : BlueprintData.Graphs)
    {
        if (Graph.Id != CurrentGraphId || Graph.Type != EAnimBlueprintGraphType::StateMachine)
        {
            continue;
        }

        const TArray<int32>* MachineIndices = PreviewNodeIndices.Find(Graph.OwnerNodeId);

        if (!MachineIndices)
        {
            return false;
        }

        for (int32 MachineIndex : *MachineIndices)
        {
            const FAnimNode_StateMachine* Machine = PreviewInstance->GetNode<FAnimNode_StateMachine>(MachineIndex);

            if (Machine)
            {
                const int32 CurrentPose = PreviewInstance->GetStatePoseNode(MachineIndex, Machine->GetCurrentStateIndex());

                if (PoseIndices->Find(CurrentPose) != INDEX_NONE)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

FAnimBlueprintEditor::FAnimBlueprintEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InAssetPath, FAnimBlueprintData InData)
    : FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
    , BlueprintData(std::move(InData))
    , bDocumentLoaded(true)
{
}

void FAnimBlueprintEditor::RequestSave()
{
    bSaveRequested = true;
}

const FString& FAnimBlueprintEditor::GetSaveMessage() const
{
    return SaveMessage;
}

bool FAnimBlueprintEditor::HasUnsavedChanges() const
{
    FString Snapshot;
    FString Error;
    return !AnimBlueprintFile::ToJson(BlueprintData, Snapshot, Error) || Snapshot != SavedSnapshot;
}

bool FAnimBlueprintEditor::Save()
{
    FString TargetPath = GetAssetPath();

    if (!AnimBlueprintFile::IsAnimBlueprintPath(TargetPath))
    {
        wchar_t Buffer[MAX_PATH] = L"NewAnimBlueprint.animblueprint";
        const FString MeshFile = FAssetUtils::ResolveAssetFilePath(BlueprintData.PreviewMeshPath);
        const std::wstring Directory = FPaths::FromUtf8(MeshFile).parent_path().wstring();
        OPENFILENAMEW Dialog = {};
        Dialog.lStructSize = sizeof(Dialog);
        Dialog.hwndOwner = GetActiveWindow();
        Dialog.lpstrFile = Buffer;
        Dialog.nMaxFile = static_cast<DWORD>(std::size(Buffer));
        Dialog.lpstrFilter = L"Animation Blueprint (*.animblueprint)\0*.animblueprint\0";
        Dialog.lpstrDefExt = L"animblueprint";
        Dialog.lpstrInitialDir = Directory.c_str();
        Dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;

        if (!GetSaveFileNameW(&Dialog))
        {
            return false;
        }

        TargetPath = FAssetUtils::GetAssetKey(FPaths::ToUtf8(std::filesystem::path(Buffer)));
    }

    const FString Resolved = FAssetUtils::ResolveAssetFilePath(TargetPath);
    const std::filesystem::path FilePath = FPaths::FromUtf8(Resolved.empty() ? TargetPath : Resolved);

    if (!AnimBlueprintFile::Write(FilePath, BlueprintData, SaveMessage))
    {
        return false;
    }

    SetAssetPath(TargetPath);
    FString Error;
    AnimBlueprintFile::ToJson(BlueprintData, SavedSnapshot, Error);
    SaveMessage = "AnimBlueprint saved.";
    return true;
}

void FAnimBlueprintEditor::Close()
{
    if (IsCloseRequested())
    {
        return;
    }

    if (HasUnsavedChanges())
    {
        const int Choice = MessageBoxW(GetActiveWindow(), L"Save changes to this Animation Blueprint before closing?", L"Unsaved Animation Blueprint", MB_YESNOCANCEL | MB_ICONQUESTION);

        if (Choice == IDCANCEL || (Choice == IDYES && !Save()))
        {
            return;
        }
    }

    FAssetEditor::Close();
}

void FAnimBlueprintEditor::HandleTabClose(IEditorTab& Tab)
{
    Close();

    // The tab container removes a tab before delivering its close callback.
    if (!IsCloseRequested())
    {
        EditorManager.AddEditorTab(&Tab);
        EditorManager.ActivateEditorTab(&Tab);
    }
}
