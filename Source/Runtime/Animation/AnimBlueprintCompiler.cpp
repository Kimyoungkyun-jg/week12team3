#include "EnginePCH.h"
#include "AnimBlueprintCompiler.h"
#include "AnimBlueprintFile.h"
#include "AnimGraphInstance.h"
#include "AnimSequence.h"
#include "BlendSpace1D.h"
#include "AnimNode_SequencePlayer.h"
#include "AnimNode_BlendSpacePlayer.h"
#include "AnimNode_TwoWayBlend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include <cmath>

namespace
{
    class FAnimBlueprintCompiler
    {
    public:
        FAnimBlueprintCompiler(const FAnimBlueprintData& Data, const USkeletalMesh* InMesh)
            : BlueprintData(Data)
            , Mesh(InMesh)
        {
        }

        bool Build(UAnimGraphInstance& Instance, AnimBlueprintCompiler::FNodeMap& NodeIndices);
        FString Error;

    private:
        const FAnimBlueprintGraph* FindGraph(int32 Id) const;
        bool SetError(int32 NodeId, const FString& Message);
        bool ValidateSequence(UAnimSequence* Sequence, int32 NodeId);
        bool ValidateFloatInput(const FAnimBlueprintNode& Node);
        int32 BuildPoseInput(const FAnimBlueprintGraph& Graph, int32 NodeId, int32 PinId, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices);
        int32 BuildPoseNode(const FAnimBlueprintGraph& Graph, int32 NodeId, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices);
        int32 BuildPoseGraph(const FAnimBlueprintGraph& Graph, EAnimBlueprintNodeType ResultType, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices);
        int32 BuildStateMachine(const FAnimBlueprintGraph& ParentGraph, const FAnimBlueprintNode& MachineNode, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices);
        bool BuildTransitions(const FAnimBlueprintGraph& Graph, int32 MachineIndex, UAnimGraphInstance& Instance, const TMap<int32, int32>& StateIndices);
        bool BuildStateEvent(const FAnimBlueprintNode& State, const FAnimBlueprintStateEvent& Event, const char* What, UAnimGraphInstance& Instance, UAnimGraphInstance::FStateCallback& OutCallback);
        const FAnimBlueprintVariable* FindVariable(const FString& Name) const;

        const FAnimBlueprintData& BlueprintData;
        const USkeletalMesh* Mesh = nullptr;
    };

    const FAnimBlueprintGraph* FAnimBlueprintCompiler::FindGraph(int32 Id) const
    {
        for (const FAnimBlueprintGraph& Graph : BlueprintData.Graphs)
        {
            if (Graph.Id == Id)
            {
                return &Graph;
            }
        }

        return nullptr;
    }

    bool FAnimBlueprintCompiler::SetError(int32 NodeId, const FString& Message)
    {
        Error = Message;

        if (NodeId != INDEX_NONE)
        {
            Error += " (ID " + std::to_string(NodeId) + ")";
        }

        return false;
    }

    bool FAnimBlueprintCompiler::ValidateSequence(UAnimSequence* Sequence, int32 NodeId)
    {
        if (!Sequence || !Sequence->GetDataModel())
        {
            return SetError(NodeId, "Failed to load animation.");
        }

        const UAnimDataModel* Model = Sequence->GetDataModel();
        const float Length = Model->GetPlayLength();

        if (!std::isfinite(Length) || Length <= 0.0f || Model->GetBoneAnimationTracks().IsEmpty())
        {
            return SetError(NodeId, "The animation has no playable bone tracks.");
        }

        for (const FBoneAnimationTrack& Track : Model->GetBoneAnimationTracks())
        {
            if (Mesh->FindBoneIndex(Track.Name) < 0)
            {
                return SetError(NodeId, "Animation bone does not match the skeletal mesh: " + Track.Name);
            }
        }

        return true;
    }

    bool FAnimBlueprintCompiler::ValidateFloatInput(const FAnimBlueprintNode& Node)
    {
        const FAnimBlueprintFloatInput& Input = Node.FloatInput;

        if (!Input.bUseVariable)
        {
            if (!std::isfinite(Input.Constant))
            {
                return SetError(Node.Id, "The input constant must be finite.");
            }

            if (Node.Type == EAnimBlueprintNodeType::TwoWayBlend && (Input.Constant < 0.0f || Input.Constant > 1.0f))
            {
                return SetError(Node.Id, "Alpha must be between zero and one.");
            }

            return true;
        }

        for (const FAnimBlueprintVariable& Variable : BlueprintData.Variables)
        {
            if (Variable.Name == Input.Variable && Variable.Type == EAnimBlueprintVariableType::Float)
            {
                return true;
            }
        }

        return SetError(Node.Id, "Select a valid Float variable for the input.");
    }

    int32 FAnimBlueprintCompiler::BuildPoseInput(const FAnimBlueprintGraph& Graph, int32 NodeId, int32 PinId, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices)
    {
        const FAnimBlueprintLink* InputLink = nullptr;

        for (const FAnimBlueprintLink& Link : Graph.Links)
        {
            if (Link.ToNodeId == NodeId && Link.ToPinId == PinId)
            {
                if (InputLink)
                {
                    SetError(NodeId, "A pose input has multiple connections.");
                    return INDEX_NONE;
                }

                InputLink = &Link;
            }
        }

        if (!InputLink || PinId <= 0)
        {
            SetError(NodeId, "Connect every required pose input in graph: " + Graph.Name);
            return INDEX_NONE;
        }

        for (const FAnimBlueprintNode& Source : Graph.Nodes)
        {
            if (Source.Id == InputLink->FromNodeId && Source.OutputPosePinId > 0 && Source.OutputPosePinId == InputLink->FromPinId)
            {
                return BuildPoseNode(Graph, Source.Id, Instance, Visiting, NodeIndices);
            }
        }

        SetError(NodeId, "The connected pose output is invalid.");
        return INDEX_NONE;
    }

    int32 FAnimBlueprintCompiler::BuildPoseNode(const FAnimBlueprintGraph& Graph, int32 NodeId, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices)
    {
        if (Visiting.Find(NodeId) != INDEX_NONE)
        {
            SetError(NodeId, "A pose graph cannot contain a cycle.");
            return INDEX_NONE;
        }

        const FAnimBlueprintNode* Node = nullptr;

        for (const FAnimBlueprintNode& Candidate : Graph.Nodes)
        {
            if (Candidate.Id == NodeId)
            {
                Node = &Candidate;
                break;
            }
        }

        if (!Node)
        {
            SetError(NodeId, "The connected node no longer exists.");
            return INDEX_NONE;
        }

        Visiting.Add(NodeId);
        int32 RuntimeIndex = INDEX_NONE;

        if (Node->Type == EAnimBlueprintNodeType::SequencePlayer || Node->Type == EAnimBlueprintNodeType::BlendSpacePlayer)
        {
            if (!std::isfinite(Node->PlayRate) || Node->PlayRate < 0.0f)
            {
                SetError(NodeId, "Play Rate must be finite and non-negative.");
                return INDEX_NONE;
            }
        }

        switch (Node->Type)
        {
        case EAnimBlueprintNodeType::OutputPose:
        case EAnimBlueprintNodeType::StateResult:
            RuntimeIndex = BuildPoseInput(Graph, NodeId, Node->InputPosePinId, Instance, Visiting, NodeIndices);
            break;

        case EAnimBlueprintNodeType::StateMachine:
            RuntimeIndex = BuildStateMachine(Graph, *Node, Instance, Visiting, NodeIndices);
            break;

        case EAnimBlueprintNodeType::SequencePlayer:
        {
            if (Node->SequencePath.empty())
            {
                SetError(NodeId, "Select an animation for the Sequence Player.");
                return INDEX_NONE;
            }

            UAnimSequence* Sequence = Instance.LoadSequence(Node->SequencePath);

            if (!ValidateSequence(Sequence, NodeId))
            {
                return INDEX_NONE;
            }

            RuntimeIndex = Instance.AddSequencePlayer(Sequence, Node->bLooping, Node->PlayRate);
            break;
        }

        case EAnimBlueprintNodeType::BlendSpacePlayer:
        {
            if (!ValidateFloatInput(*Node))
            {
                return INDEX_NONE;
            }

            if (Node->BlendSpacePath.empty())
            {
                SetError(NodeId, "Select a BlendSpace for the BlendSpace Player.");
                return INDEX_NONE;
            }

            UBlendSpace1D* BlendSpace = Instance.LoadBlendSpace(Node->BlendSpacePath);

            if (!BlendSpace || BlendSpace->GetSampleData().IsEmpty())
            {
                SetError(NodeId, "The BlendSpace could not be loaded or has no samples.");
                return INDEX_NONE;
            }

            for (const FBlendSample& Sample : BlendSpace->GetSampleData())
            {
                if (!std::isfinite(Sample.SampleValue.X) || !ValidateSequence(Sample.Animation, NodeId))
                {
                    SetError(NodeId, "The BlendSpace contains invalid or incompatible samples.");
                    return INDEX_NONE;
                }
            }

            const FString Variable = Node->FloatInput.bUseVariable ? Node->FloatInput.Variable : FString();
            RuntimeIndex = Instance.AddBlendSpacePlayer(BlendSpace, Variable, Node->PlayRate);

            FAnimNode_BlendSpacePlayer* Player = Instance.GetNode<FAnimNode_BlendSpacePlayer>(RuntimeIndex);
            Player->bLooping = Node->bLooping;
            Player->X = Node->FloatInput.Constant;
            break;
        }

        case EAnimBlueprintNodeType::TwoWayBlend:
        {
            if (!ValidateFloatInput(*Node))
            {
                return INDEX_NONE;
            }

            const int32 NodeA = BuildPoseInput(Graph, NodeId, Node->InputPosePinId, Instance, Visiting, NodeIndices);

            if (NodeA == INDEX_NONE)
            {
                return INDEX_NONE;
            }

            const int32 NodeB = BuildPoseInput(Graph, NodeId, Node->SecondInputPosePinId, Instance, Visiting, NodeIndices);

            if (NodeB == INDEX_NONE)
            {
                return INDEX_NONE;
            }

            const FString Variable = Node->FloatInput.bUseVariable ? Node->FloatInput.Variable : FString();
            RuntimeIndex = Instance.AddTwoWayBlend(NodeA, NodeB, Variable);
            Instance.GetNode<FAnimNode_TwoWayBlend>(RuntimeIndex)->Alpha = Node->FloatInput.Constant;
            break;
        }

        default:
            SetError(NodeId, "This node type is not supported by the animation runtime.");
            return INDEX_NONE;
        }

        Visiting.RemoveLast();

        if (RuntimeIndex != INDEX_NONE)
        {
            // Each use gets its own runtime node so shared branches do not advance one player twice.
            NodeIndices[NodeId].Add(RuntimeIndex);
        }

        return RuntimeIndex;
    }

    int32 FAnimBlueprintCompiler::BuildPoseGraph(const FAnimBlueprintGraph& Graph, EAnimBlueprintNodeType ResultType, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices)
    {
        if (Graph.Type != EAnimBlueprintGraphType::Pose)
        {
            SetError(Graph.OwnerNodeId, "A state must contain a pose graph.");
            return INDEX_NONE;
        }

        const FAnimBlueprintNode* ResultNode = nullptr;

        for (const FAnimBlueprintNode& Node : Graph.Nodes)
        {
            if (Node.Type == EAnimBlueprintNodeType::OutputPose || Node.Type == EAnimBlueprintNodeType::StateResult)
            {
                if (Node.Type != ResultType || ResultNode)
                {
                    SetError(Node.Id, "The pose graph must have exactly one matching result node.");
                    return INDEX_NONE;
                }

                ResultNode = &Node;
            }
        }

        if (!ResultNode)
        {
            SetError(Graph.OwnerNodeId, "The pose graph result is missing: " + Graph.Name);
            return INDEX_NONE;
        }

        return BuildPoseNode(Graph, ResultNode->Id, Instance, Visiting, NodeIndices);
    }

    int32 FAnimBlueprintCompiler::BuildStateMachine(const FAnimBlueprintGraph& ParentGraph, const FAnimBlueprintNode& MachineNode, UAnimGraphInstance& Instance, TArray<int32>& Visiting, TMap<int32, TArray<int32>>& NodeIndices)
    {
        const FAnimBlueprintGraph* MachineGraph = FindGraph(MachineNode.ChildGraphId);

        if (!MachineGraph || MachineGraph->Type != EAnimBlueprintGraphType::StateMachine || MachineGraph->ParentGraphId != ParentGraph.Id || MachineGraph->OwnerNodeId != MachineNode.Id)
        {
            SetError(MachineNode.Id, "The State Machine graph is missing or belongs to another node.");
            return INDEX_NONE;
        }

        const FAnimBlueprintNode* Entry = nullptr;
        int32 StateCount = 0;
        int32 AnyStateCount = 0;

        for (const FAnimBlueprintNode& Node : MachineGraph->Nodes)
        {
            if (Node.Type == EAnimBlueprintNodeType::Entry)
            {
                if (Entry)
                {
                    SetError(Node.Id, "A State Machine must have exactly one Entry.");
                    return INDEX_NONE;
                }

                Entry = &Node;
            }
            else if (Node.Type == EAnimBlueprintNodeType::State)
            {
                ++StateCount;
            }
            else if (Node.Type == EAnimBlueprintNodeType::AnyState)
            {
                ++AnyStateCount;

                if (AnyStateCount > 1)
                {
                    SetError(Node.Id, "Only one Any State node is allowed.");
                    return INDEX_NONE;
                }
            }
            else
            {
                SetError(Node.Id, "Only Entry, State and Any State nodes are allowed inside a State Machine.");
                return INDEX_NONE;
            }
        }

        if (!Entry || Entry->TransitionOutputPinId <= 0 || StateCount == 0)
        {
            SetError(MachineNode.Id, "A State Machine needs an Entry and at least one State.");
            return INDEX_NONE;
        }

        if (MachineGraph->Links.Num() != 1)
        {
            SetError(Entry->Id, "Connect Entry to exactly one initial State.");
            return INDEX_NONE;
        }

        const FAnimBlueprintLink& InitialLink = MachineGraph->Links[0];

        if (InitialLink.FromNodeId != Entry->Id || InitialLink.FromPinId != Entry->TransitionOutputPinId)
        {
            SetError(Entry->Id, "The initial State connection must start at Entry.");
            return INDEX_NONE;
        }

        const int32 MachineIndex = Instance.AddStateMachine();
        TMap<int32, int32> StateIndices;
        TArray<FName> StateNames;

        for (const FAnimBlueprintNode& State : MachineGraph->Nodes)
        {
            if (State.Type != EAnimBlueprintNodeType::State)
            {
                continue;
            }

            const FName StateName(State.Name);

            if (State.Name.find_first_not_of(" \t\r\n") == FString::npos || StateNames.Find(StateName) != INDEX_NONE)
            {
                SetError(State.Id, "State names must be non-empty and unique within the State Machine.");
                return INDEX_NONE;
            }

            StateNames.Add(StateName);
            const FAnimBlueprintGraph* StateGraph = FindGraph(State.ChildGraphId);

            if (!StateGraph || StateGraph->ParentGraphId != MachineGraph->Id || StateGraph->OwnerNodeId != State.Id)
            {
                SetError(State.Id, "The State pose graph is missing or belongs to another node.");
                return INDEX_NONE;
            }

            const int32 PoseIndex = BuildPoseGraph(*StateGraph, EAnimBlueprintNodeType::StateResult,
                Instance, Visiting, NodeIndices);

            if (PoseIndex == INDEX_NONE)
            {
                return INDEX_NONE;
            }

            UAnimGraphInstance::FStateCallback OnEnter;
            UAnimGraphInstance::FStateCallback OnExit;

            if (!BuildStateEvent(State, State.OnEnter, "OnEnter", Instance, OnEnter)
                || !BuildStateEvent(State, State.OnExit, "OnExit", Instance, OnExit))
            {
                return INDEX_NONE;
            }

            const int32 StateIndex = Instance.AddState(MachineIndex, StateName, PoseIndex, State.bResetOnEntry, std::move(OnEnter), std::move(OnExit));

            if (StateIndex == INDEX_NONE)
            {
                SetError(State.Id, "Failed to register the State pose.");
                return INDEX_NONE;
            }

            StateIndices.Add(State.Id, StateIndex);
            NodeIndices[State.Id].Add(PoseIndex);

            if (State.Id == InitialLink.ToNodeId)
            {
                if (State.TransitionInputPinId <= 0 || InitialLink.ToPinId != State.TransitionInputPinId)
                {
                    SetError(State.Id, "Entry is connected to an invalid State input pin.");
                    return INDEX_NONE;
                }
            }
        }

        const int32* InitialStateIndex = StateIndices.Find(InitialLink.ToNodeId);

        if (!InitialStateIndex || !Instance.SetInitialState(MachineIndex, *InitialStateIndex))
        {
            SetError(Entry->Id, "Entry must point to a State in this State Machine.");
            return INDEX_NONE;
        }

        if (!BuildTransitions(*MachineGraph, MachineIndex, Instance, StateIndices))
        {
            return INDEX_NONE;
        }

        return MachineIndex;
    }

    const FAnimBlueprintVariable* FAnimBlueprintCompiler::FindVariable(const FString& Name) const
    {
        for (const FAnimBlueprintVariable& Variable : BlueprintData.Variables)
        {
            if (Variable.Name == Name)
            {
                return &Variable;
            }
        }

        return nullptr;
    }

    // 변수 대입을 순서대로 실행한 뒤 이벤트 이름 Notify를 보낸다.
    // 핸들러가 바뀐 변수 값을 볼 수 있도록 대입을 먼저 한다.
    bool FAnimBlueprintCompiler::BuildStateEvent(const FAnimBlueprintNode& State, const FAnimBlueprintStateEvent& Event, const char* What, UAnimGraphInstance& Instance, UAnimGraphInstance::FStateCallback& OutCallback)
    {
        TArray<UAnimGraphInstance::FStateCallback> Actions;

        for (const FAnimBlueprintVariableAssignment& Assignment : Event.Assignments)
        {
            const FAnimBlueprintVariable* Variable = FindVariable(Assignment.Variable);

            if (!Variable)
            {
                return SetError(State.Id, FString("Select a valid variable for every ") + What + " assignment in State: " + State.Name);
            }

            if (Variable->Type == EAnimBlueprintVariableType::Bool)
            {
                Actions.Add(Instance.MakeSetBoolAction(Variable->Name, Assignment.bBoolValue));
            }
            else
            {
                if (!std::isfinite(Assignment.FloatValue))
                {
                    return SetError(State.Id, FString(What) + " assigns a non-finite value in State: " + State.Name);
                }

                Actions.Add(Instance.MakeSetFloatAction(Variable->Name, Assignment.FloatValue));
            }
        }

        const size_t First = Event.EventName.find_first_not_of(" \t\r\n");

        if (First != FString::npos)
        {
            const size_t Last = Event.EventName.find_last_not_of(" \t\r\n");
            Actions.Add(Instance.MakeNamedNotifyAction(FName(Event.EventName.substr(First, Last - First + 1))));
        }

        OutCallback = UAnimGraphInstance::CombineActions(std::move(Actions));
        return true;
    }

    bool FAnimBlueprintCompiler::BuildTransitions(const FAnimBlueprintGraph& Graph, int32 MachineIndex, UAnimGraphInstance& Instance, const TMap<int32, int32>& StateIndices)
    {
        for (const FAnimBlueprintTransition& Transition : Graph.Transitions)
        {
            const FAnimBlueprintNode* Source = nullptr;
            const FAnimBlueprintNode* Target = nullptr;

            for (const FAnimBlueprintNode& Node : Graph.Nodes)
            {
                if (Node.Id == Transition.FromStateId)
                {
                    Source = &Node;
                }

                if (Node.Id == Transition.ToStateId)
                {
                    Target = &Node;
                }
            }

            if (!Source || !Target || Target->Type != EAnimBlueprintNodeType::State || Source == Target)
            {
                return SetError(Transition.Id, "The transition endpoints are invalid.");
            }

            if ((Source->Type != EAnimBlueprintNodeType::State
                && Source->Type != EAnimBlueprintNodeType::AnyState)
                || Source->TransitionOutputPinId != Transition.FromPinId
                || Target->TransitionInputPinId != Transition.ToPinId)
            {
                return SetError(Transition.Id, "The transition pins are invalid.");
            }

            if (Transition.Rule == EAnimBlueprintTransitionRule::Disabled)
            {
                continue;
            }

            if (!std::isfinite(Transition.CrossfadeDuration) || Transition.CrossfadeDuration < 0.0f)
            {
                return SetError(Transition.Id, "Crossfade duration must be finite and non-negative.");
            }

            const int32* ToIndex = StateIndices.Find(Target->Id);
            const int32* SourceIndex = StateIndices.Find(Source->Id);
            const bool bAnyState = Source->Type == EAnimBlueprintNodeType::AnyState;

            if (!ToIndex || (!bAnyState && !SourceIndex))
            {
                return SetError(Transition.Id, "The transition State is not registered.");
            }

            const int32 FromIndex = bAnyState ? INDEX_NONE : *SourceIndex;
            FAnimTransitionRule Rule;

            if (Transition.Rule == EAnimBlueprintTransitionRule::Finished)
            {
                if (bAnyState)
                {
                    return SetError(Transition.Id, "Finished requires a specific source State.");
                }

                const int32 PoseIndex = Instance.GetStatePoseNode(MachineIndex, FromIndex);
                const FAnimNode_SequencePlayer* Player = Instance.GetNode<FAnimNode_SequencePlayer>(PoseIndex);

                if (!Player || Player->bLooping)
                {
                    return SetError(Transition.Id, "Finished requires the source State to output a non-looping Sequence Player directly.");
                }

                Rule = Instance.MakeFinishedRule(PoseIndex);
            }
            else
            {
                const FAnimBlueprintVariable* MatchedVariable = nullptr;

                for (const FAnimBlueprintVariable& Variable : BlueprintData.Variables)
                {
                    if (Variable.Name == Transition.Variable)
                    {
                        MatchedVariable = &Variable;
                        break;
                    }
                }

                if (!MatchedVariable)
                {
                    return SetError(Transition.Id, "Select a variable for the transition condition.");
                }

                if (Transition.Rule == EAnimBlueprintTransitionRule::Bool)
                {
                    if (MatchedVariable->Type != EAnimBlueprintVariableType::Bool)
                    {
                        return SetError(Transition.Id, "This transition requires a Bool variable.");
                    }

                    Rule = Instance.MakeBoolRule(Transition.Variable, Transition.bExpected);
                }
                else if (Transition.Rule == EAnimBlueprintTransitionRule::Float)
                {
                    if (MatchedVariable->Type != EAnimBlueprintVariableType::Float || !std::isfinite(Transition.Value))
                    {
                        return SetError(Transition.Id, "This transition requires a Float variable and a finite comparison value.");
                    }

                    EAnimGraphCompare Compare;

                    switch (Transition.Compare)
                    {
                    case EAnimBlueprintCompare::Less:
                        Compare = EAnimGraphCompare::Less;
                        break;

                    case EAnimBlueprintCompare::LessEqual:
                        Compare = EAnimGraphCompare::LessEqual;
                        break;

                    case EAnimBlueprintCompare::Greater:
                        Compare = EAnimGraphCompare::Greater;
                        break;

                    case EAnimBlueprintCompare::GreaterEqual:
                        Compare = EAnimGraphCompare::GreaterEqual;
                        break;

                    case EAnimBlueprintCompare::Equal:
                        Compare = EAnimGraphCompare::Equal;
                        break;

                    case EAnimBlueprintCompare::NotEqual:
                        Compare = EAnimGraphCompare::NotEqual;
                        break;

                    default:
                        return SetError(Transition.Id, "The comparison operator is invalid.");
                    }

                    Rule = Instance.MakeFloatRule(Transition.Variable, Compare, Transition.Value);
                }
                else
                {
                    return SetError(Transition.Id, "The transition rule type is invalid.");
                }
            }

            if (Instance.AddTransition(MachineIndex, FromIndex, *ToIndex, std::move(Rule), Transition.CrossfadeDuration, Transition.Priority) == INDEX_NONE)
            {
                return SetError(Transition.Id, "Failed to register the transition.");
            }
        }

        return true;
    }

    bool FAnimBlueprintCompiler::Build(UAnimGraphInstance& Instance, AnimBlueprintCompiler::FNodeMap& NodeIndices)
    {
        for (const FAnimBlueprintVariable& Variable : BlueprintData.Variables)
        {
            if (Variable.Type == EAnimBlueprintVariableType::Float)
            {
                Instance.SetFloat(Variable.Name, Variable.DefaultFloat);
            }
            else
            {
                Instance.SetBool(Variable.Name, Variable.bDefaultBool);
            }
        }

        const FAnimBlueprintGraph* Root = FindGraph(BlueprintData.RootGraphId);
        TArray<int32> Visiting;
        const int32 RootIndex = BuildPoseGraph(*Root, EAnimBlueprintNodeType::OutputPose, Instance, Visiting, NodeIndices);

        if (RootIndex == INDEX_NONE)
        {
            return false;
        }

        if (!Instance.SetRoot(RootIndex))
        {
            return SetError(INDEX_NONE, "Failed to initialize the animation graph.");
        }

        Instance.UpdateAnimation(0.0f);
        return true;
    }
}

bool AnimBlueprintCompiler::Build(const FAnimBlueprintData& Data, USkeletalMeshComponent& Component, TUniquePtr<UAnimGraphInstance>& OutInstance, FNodeMap& OutNodeIndices, FString& OutError)
{
    if (!Component.GetSkeletalMesh())
    {
        OutError = "Assign a skeletal mesh before loading an Animation Blueprint.";
        return false;
    }

    if (!AnimBlueprintFile::Validate(Data, OutError))
    {
        return false;
    }

    TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(&Component));
    FNodeMap NodeIndices;
    FAnimBlueprintCompiler Compiler(Data, Component.GetSkeletalMesh());

    if (!Compiler.Build(*Instance, NodeIndices))
    {
        OutError = Compiler.Error;
        return false;
    }

    Instance->SetRootMotionMode(Data.RootMotionMode);   // AnimBP Class Defaults 값
    OutInstance = std::move(Instance);
    OutNodeIndices = std::move(NodeIndices);
    OutError.clear();
    return true;
}
