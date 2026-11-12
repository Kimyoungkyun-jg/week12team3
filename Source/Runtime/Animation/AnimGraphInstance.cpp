#include "EnginePCH.h"
#include "AnimGraphInstance.h"
#include "AnimNode_SequencePlayer.h"
#include "AnimNode_BlendSpacePlayer.h"
#include "AnimNode_TwoWayBlend.h"
#include "AnimSequence.h"
#include "AnimSequenceFile.h"
#include "BlendSpace1D.h"
#include "BlendSpaceFile.h"
#include "Asset/AssetUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include <cmath>
#include <atomic>

namespace
{
    uint32 AllocateAnimGraphGeneration()
    {
        static std::atomic<uint32> NextGeneration = 0;
        return NextGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    }
}

UAnimGraphInstance::UAnimGraphInstance()
{
    Generation = AllocateAnimGraphGeneration();
}

void UAnimGraphInstance::ClearNotifyBindings()
{
    EndAllAnimNotifyStates();
    NotifyQueue.Reset();
    NotifyHandlers.Empty();
    NotifyStateHandlers.Empty();
}

UAnimGraphInstance::~UAnimGraphInstance()
{
	// 소멸 시점에는 메시가 이미 없을 수 있으므로 NotifyState End는 보내지 않는다
	ClearGraph();
}

void UAnimGraphInstance::ResetGraph()
{
	EndAllAnimNotifyStates();
	NotifyQueue.Reset();
	ClearGraph();

	FloatVariables.Empty();
	BoolVariables.Empty();
	NotifyHandlers.Empty();
	NotifyStateHandlers.Empty();
	Generation = AllocateAnimGraphGeneration();
}

void UAnimGraphInstance::ClearGraph()
{
	RootNode = nullptr;
	RootNodeIndex = INDEX_NONE;

	// 노드가 시퀀스를 가리키므로 노드부터 지운다. 노드 안의 규칙이 Lua 함수를 잡고 있을 수 있다.
	Nodes.Empty();
	StatePoseNodes.Empty();
	Bindings.Empty();

	for (UBlendSpace1D* BlendSpace : OwnedBlendSpaces)
		delete BlendSpace;
	OwnedBlendSpaces.Empty();

	// 시퀀스 소멸자가 소유한 Notify 객체도 지운다
	for (UAnimSequence* Sequence : OwnedSequences)
		delete Sequence;
	OwnedSequences.Empty();
}

// ---------------- 에셋 ----------------

UAnimSequence* UAnimGraphInstance::LoadSequence(const FString& Path)
{
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	UAnimSequence* Sequence = NewObject<UAnimSequence>(this);

	FString Error;
	if (!AnimSequenceFile::Read(std::filesystem::path(FilePath.empty() ? Path : FilePath), *Sequence, Error))
	{
		HTR_LOG(Error, "[AnimGraph] Animation load failed: {} ({})", Path, Error);
		delete Sequence;
		return nullptr;
	}

	OwnedSequences.Add(Sequence);
	return Sequence;
}

UBlendSpace1D* UAnimGraphInstance::CreateBlendSpace()
{
	UBlendSpace1D* BlendSpace = NewObject<UBlendSpace1D>(this);
	OwnedBlendSpaces.Add(BlendSpace);
	return BlendSpace;
}

UBlendSpace1D* UAnimGraphInstance::LoadBlendSpace(const FString& Path)
{
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	UBlendSpace1D* BlendSpace = NewObject<UBlendSpace1D>(this);

	FString Error;
	if (!BlendSpaceFile::Read(std::filesystem::path(FilePath.empty() ? Path : FilePath), *BlendSpace, Error))
	{
		HTR_LOG(Error, "[AnimGraph] BlendSpace load failed: {} ({})", Path, Error);
		delete BlendSpace;
		return nullptr;
	}

    // NotifyState objects can contain mutable state. Each graph owns sample sequences.
    const TArray<FBlendSample> Samples = BlendSpace->GetSampleData();

    while (!BlendSpace->GetSampleData().IsEmpty())
    {
        BlendSpace->RemoveSample(0);
    }

    for (const FBlendSample& Sample : Samples)
    {
        UAnimSequence* Sequence = Sample.Animation ? LoadSequence(Sample.Animation->GetAssetPath()) : nullptr;

        if (!Sequence || !BlendSpace->AddSample(Sequence, Sample.SampleValue))
        {
            HTR_LOG(Warning, "[AnimGraph] Failed to create per-instance BlendSpace samples: {}", Path);
            delete BlendSpace;
            return nullptr;
        }
    }

	OwnedBlendSpaces.Add(BlendSpace);
	return BlendSpace;
}

bool UAnimGraphInstance::OwnsSequence(const UAnimSequence* Sequence) const
{
	return Sequence && OwnedSequences.Find(const_cast<UAnimSequence*>(Sequence)) != -1;
}

bool UAnimGraphInstance::OwnsBlendSpace(const UBlendSpace1D* BlendSpace) const
{
	return BlendSpace && OwnedBlendSpaces.Find(const_cast<UBlendSpace1D*>(BlendSpace)) != -1;
}

// ---------------- 노드 ----------------

int32 UAnimGraphInstance::AddNode(std::unique_ptr<FAnimNode_Base> Node)
{
	Nodes.Add(std::move(Node));
	return Nodes.Num() - 1;
}

int32 UAnimGraphInstance::AddSequencePlayer(UAnimSequence* Sequence, bool bLooping, float PlayRate)
{
	auto Node = std::make_unique<FAnimNode_SequencePlayer>();
	Node->Sequence = Sequence;
	Node->bLooping = bLooping;
	Node->PlayRate = PlayRate;
	return AddNode(std::move(Node));
}

int32 UAnimGraphInstance::AddBlendSpacePlayer(UBlendSpace1D* BlendSpace, const FString& XVariable, float PlayRate)
{
	auto Node = std::make_unique<FAnimNode_BlendSpacePlayer>();
	Node->BlendSpace = BlendSpace;
	Node->PlayRate = PlayRate;
	const int32 Index = AddNode(std::move(Node));

	if (!XVariable.empty())
		Bindings.Add({ Index, EBindingTarget::BlendSpaceX, XVariable });
	return Index;
}

int32 UAnimGraphInstance::AddTwoWayBlend(int32 NodeA, int32 NodeB, const FString& AlphaVariable)
{
	if (!IsValidNode(NodeA) || !IsValidNode(NodeB))
		return INDEX_NONE;

	auto Node = std::make_unique<FAnimNode_TwoWayBlend>();
	Node->A.SetLinkNode(Nodes[NodeA].get());
	Node->B.SetLinkNode(Nodes[NodeB].get());
	const int32 Index = AddNode(std::move(Node));

	if (!AlphaVariable.empty())
		Bindings.Add({ Index, EBindingTarget::TwoWayBlendAlpha, AlphaVariable });
	return Index;
}

int32 UAnimGraphInstance::AddStateMachine()
{
	const int32 Index = AddNode(std::make_unique<FAnimNode_StateMachine>());
	StatePoseNodes[Index] = TArray<int32>();
	return Index;
}

bool UAnimGraphInstance::SetRoot(int32 NodeIndex)
{
	if (!IsValidNode(NodeIndex))
		return false;

	RootNodeIndex = NodeIndex;
	RootNode = Nodes[NodeIndex].get();

	FAnimationInitializeContext InitContext;
	RootNode->Initialize(InitContext);
	return true;
}

// ---------------- 상태 머신 ----------------

int32 UAnimGraphInstance::AddState(int32 MachineNode, const FName& Name, int32 PoseNode, bool bResetOnEntry,
	FStateCallback OnEnter, FStateCallback OnExit)
{
	FAnimNode_StateMachine* Machine = GetNode<FAnimNode_StateMachine>(MachineNode);
	if (!Machine || !IsValidNode(PoseNode) || PoseNode == MachineNode)
		return INDEX_NONE;

	FAnimStateDesc Desc;
	Desc.Name = Name;
	Desc.Pose.SetLinkNode(Nodes[PoseNode].get());
	Desc.bResetOnEntry = bResetOnEntry;
	Desc.OnEnter = std::move(OnEnter);
	Desc.OnExit = std::move(OnExit);

	const int32 StateIndex = Machine->AddState(Desc);
	StatePoseNodes[MachineNode].Add(PoseNode);
	return StateIndex;
}

int32 UAnimGraphInstance::AddTransition(int32 MachineNode, int32 FromState, int32 ToState, FAnimTransitionRule Rule,
	float CrossfadeDuration, int32 Priority)
{
	FAnimNode_StateMachine* Machine = GetNode<FAnimNode_StateMachine>(MachineNode);
	const TArray<int32>* States = StatePoseNodes.Find(MachineNode);
	if (!Machine || !States)
		return INDEX_NONE;

	const int32 NumStates = States->Num();
	const bool bValidFrom = FromState == INDEX_NONE || (FromState >= 0 && FromState < NumStates);
	if (!bValidFrom || ToState < 0 || ToState >= NumStates || FromState == ToState || !Rule || !std::isfinite(CrossfadeDuration))
		return INDEX_NONE;

	FAnimTransitionDesc Desc;
	Desc.From = FromState;
	Desc.To = ToState;
	Desc.Rule = std::move(Rule);
	Desc.CrossfadeDuration = FMath::Max(CrossfadeDuration, 0.0f);
	Desc.Priority = Priority;
	return Machine->AddTransition(Desc);
}

bool UAnimGraphInstance::SetInitialState(int32 MachineNode, int32 StateIndex)
{
	FAnimNode_StateMachine* Machine = GetNode<FAnimNode_StateMachine>(MachineNode);
	if (!Machine || GetStatePoseNode(MachineNode, StateIndex) == INDEX_NONE)
		return false;

	Machine->SetInitialState(StateIndex);
	return true;
}

int32 UAnimGraphInstance::GetStatePoseNode(int32 MachineNode, int32 StateIndex) const
{
	const TArray<int32>* States = StatePoseNodes.Find(MachineNode);
	if (!States || StateIndex < 0 || StateIndex >= States->Num())
		return INDEX_NONE;
	return (*States)[StateIndex];
}

FName UAnimGraphInstance::GetCurrentStateName(int32 MachineNode) const
{
	const FAnimNode_StateMachine* Machine = GetNode<FAnimNode_StateMachine>(MachineNode);
	return Machine ? Machine->GetCurrentStateName() : FName();
}

FAnimTransitionRule UAnimGraphInstance::MakeBoolRule(const FString& Variable, bool bExpected)
{
	return [this, Variable, bExpected](const FAnimTransitionContext&)
		{
			return GetBool(Variable) == bExpected;
		};
}

FAnimTransitionRule UAnimGraphInstance::MakeFloatRule(const FString& Variable, EAnimGraphCompare Compare, float Value)
{
	return [this, Variable, Compare, Value](const FAnimTransitionContext&)
		{
			const float Current = GetFloat(Variable);
			switch (Compare)
			{
			case EAnimGraphCompare::Less:         return Current < Value;
			case EAnimGraphCompare::LessEqual:    return Current <= Value;
			case EAnimGraphCompare::Greater:      return Current > Value;
			case EAnimGraphCompare::GreaterEqual: return Current >= Value;
			case EAnimGraphCompare::Equal:        return Current == Value;
			case EAnimGraphCompare::NotEqual:     return Current != Value;
			}
			return false;
		};
}

FAnimTransitionRule UAnimGraphInstance::MakeFinishedRule(int32 SequencePlayerNode)
{
	return [this, SequencePlayerNode](const FAnimTransitionContext&)
		{
			const FAnimNode_SequencePlayer* Player = GetNode<FAnimNode_SequencePlayer>(SequencePlayerNode);
			return Player && Player->IsFinished();
		};
}

UAnimGraphInstance::FStateCallback UAnimGraphInstance::MakeSetBoolAction(const FString& Variable, bool bValue)
{
	return [this, Variable, bValue](UAnimInstance*)
		{
			SetBool(Variable, bValue);
		};
}

UAnimGraphInstance::FStateCallback UAnimGraphInstance::MakeSetFloatAction(const FString& Variable, float Value)
{
	return [this, Variable, Value](UAnimInstance*)
		{
			SetFloat(Variable, Value);
		};
}

UAnimGraphInstance::FStateCallback UAnimGraphInstance::MakeNamedNotifyAction(const FName& NotifyName)
{
	return [this, NotifyName](UAnimInstance*)
		{
			HandleNamedNotify(NotifyName);
		};
}

UAnimGraphInstance::FStateCallback UAnimGraphInstance::CombineActions(TArray<FStateCallback> Actions)
{
	TArray<FStateCallback> Valid;
	for (FStateCallback& Action : Actions)
	{
		if (Action)
			Valid.Add(std::move(Action));
	}

	if (Valid.IsEmpty())
		return {};
	if (Valid.Num() == 1)
		return std::move(Valid[0]);

	return [Valid = std::move(Valid)](UAnimInstance* Instance)
		{
			for (const FStateCallback& Action : Valid)
				Action(Instance);
		};
}

// ---------------- 변수 ----------------

float UAnimGraphInstance::GetFloat(const FString& Name, float Default) const
{
	const float* Found = FloatVariables.Find(Name);
	return Found ? *Found : Default;
}

bool UAnimGraphInstance::GetBool(const FString& Name, bool bDefault) const
{
	const bool* Found = BoolVariables.Find(Name);
	return Found ? *Found : bDefault;
}

// ---------------- 이름 기반 Notify ----------------

void UAnimGraphInstance::BindNotify(const FName& Name, FNotifyCallback Callback)
{
	if (Callback)
		NotifyHandlers[Name.ToString()] = std::move(Callback);
	else
		NotifyHandlers.Remove(Name.ToString());
}

void UAnimGraphInstance::BindNotifyState(const FName& Name, FNotifyStateCallback OnBegin, FNotifyStateCallback OnTick, FNotifyStateCallback OnEnd)
{
	NotifyStateHandlers[Name.ToString()] = { std::move(OnBegin), std::move(OnTick), std::move(OnEnd) };
}

void UAnimGraphInstance::HandleNamedNotify(const FName& NotifyName)
{
	if (const FNotifyCallback* Callback = NotifyHandlers.Find(NotifyName.ToString()))
		(*Callback)();
}

void UAnimGraphInstance::HandleNamedNotifyStateBegin(const FName& NotifyName, float TotalDuration)
{
	const FNotifyStateHandlers* Handlers = NotifyStateHandlers.Find(NotifyName.ToString());
	if (Handlers && Handlers->OnBegin)
		Handlers->OnBegin(TotalDuration);
}

void UAnimGraphInstance::HandleNamedNotifyStateTick(const FName& NotifyName, float DeltaTime)
{
	const FNotifyStateHandlers* Handlers = NotifyStateHandlers.Find(NotifyName.ToString());
	if (Handlers && Handlers->OnTick)
		Handlers->OnTick(DeltaTime);
}

void UAnimGraphInstance::HandleNamedNotifyStateEnd(const FName& NotifyName)
{
	const FNotifyStateHandlers* Handlers = NotifyStateHandlers.Find(NotifyName.ToString());
	if (Handlers && Handlers->OnEnd)
		Handlers->OnEnd(0.0f);
}

// ---------------- 갱신 ----------------

void UAnimGraphInstance::NativeInitializeAnimation()
{
	// UAnimInstance::InitializeAnimation이 RootNode를 비운 뒤 부르므로, 조립해 둔 그래프를 다시 연결한다
	RootNode = IsValidNode(RootNodeIndex) ? Nodes[RootNodeIndex].get() : nullptr;
}

void UAnimGraphInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (APawn* Pawn = TryGetPawnOwner())
	{
		const FVector Velocity = Pawn->GetVelocity();
		SetFloat(GroundSpeedVariable, FVector(Velocity.X, Velocity.Y, 0.0f).Size() * WorldToCm);

		if (const ACharacter* Character = Cast<ACharacter>(Pawn))
			SetBool(IsFallingVariable, Character->IsFalling());
	}

	for (const FVariableBinding& Binding : Bindings)
	{
		const float Value = GetFloat(Binding.Variable);
		switch (Binding.Target)
		{
		case EBindingTarget::BlendSpaceX:
			if (auto* Player = GetNode<FAnimNode_BlendSpacePlayer>(Binding.NodeIndex))
				Player->X = Value;
			break;
		case EBindingTarget::TwoWayBlendAlpha:
			if (auto* Blend = GetNode<FAnimNode_TwoWayBlend>(Binding.NodeIndex))
				Blend->Alpha = Value;
			break;
		}
	}
}
