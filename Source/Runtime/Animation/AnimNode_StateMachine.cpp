#include "EnginePCH.h"
#include "AnimNode_StateMachine.h"
#include <algorithm>
#include "AnimationRuntime.h"

int32 FAnimNode_StateMachine::AddState(const FAnimStateDesc& Desc)
{
	const int32 StateIndex = States.Add(Desc);
	ExitTransitions.Add({});
	
	//BakeTransitions 배열이 상태 갯수 만큼 할당되므로 다시 bake 한다.
	bTransitionDirty = true;
	return StateIndex;
}

//콜백(OnEnter, OnExit)이 필요한 경우,  FAnimNode_StateMachine::AddState(const FAnimStateDesc& Desc) 사용
int32 FAnimNode_StateMachine::AddState(FName Name, FAnimNode_Base* Node)
{
	FAnimStateDesc State;
	State.Name = Name;
	State.Pose.SetLinkNode(Node);

	return AddState(State);
}

int32 FAnimNode_StateMachine::AddTransition(const FAnimTransitionDesc& Desc)
{
	assert(Desc.From == INDEX_NONE || (Desc.From >= 0 && Desc.From < States.Num()));
	assert(Desc.To >= 0 && Desc.To < States.Num());
	assert(Desc.From != Desc.To && "From == To Transition is not allowed");

	const int32 TransitionIndex = Transitions.Num();
	Transitions.Add(Desc);

	// From 상태의 ExitTransitions에 추가. Any State이면 AnyStateTransitions에 추가
	TArray<int32>& List = (Desc.From == INDEX_NONE) ? AnyStateTransitions : ExitTransitions[Desc.From];
	List.Add(TransitionIndex);

	bTransitionDirty = true;
	return TransitionIndex;
}

void FAnimNode_StateMachine::SetInitialState(int32 StateIndex)
{
	assert(StateIndex >= 0 && StateIndex < States.Num());
	InitialState = StateIndex;
    CurrentState = StateIndex;
    ActiveBlends.Empty();
	ElapsedTime = 0.f;
	bFirstEnter = true;
}

FName FAnimNode_StateMachine::GetCurrentStateName() const
{
	if (States.Num() == 0) return FName();
    return States[CurrentState].Name;
}

int32 FAnimNode_StateMachine::GetCurrentStateIndex() const
{
    return States.IsEmpty() ? INDEX_NONE : CurrentState;
}

void FAnimNode_StateMachine::Update(const FAnimationUpdateContext& Context)
{
    if (States.IsEmpty())
    {
        return;
    }

    UAnimInstance* AnimInstance = Context.GetAnimInstance<UAnimInstance>();

    if (bFirstEnter)
    {
        if (States[CurrentState].OnEnter)
        {
            States[CurrentState].OnEnter(AnimInstance);
        }

        bFirstEnter = false;
    }

    if (bTransitionDirty)
    {
        Bake();
        bTransitionDirty = false;
    }

    ProcessState(AnimInstance);
    const float DeltaTime = Context.GetDeltaTime();
    int32 LastCompletedBlend = INDEX_NONE;

    for (int32 Index = 0; Index < ActiveBlends.Num(); ++Index)
    {
        FActiveBlend& Blend = ActiveBlends[Index];
        Blend.Elapsed += DeltaTime;

        if (Blend.Elapsed >= Blend.Duration)
        {
            LastCompletedBlend = Index;
        }
    }

    if (LastCompletedBlend != INDEX_NONE)
    {
        ActiveBlends.RemoveAt(0, LastCompletedBlend + 1);
    }

    TArray<float> StateWeights;
    StateWeights.SetNum(States.Num());

    for (float& Weight : StateWeights)
    {
        Weight = 0.0f;
    }

    if (ActiveBlends.IsEmpty())
    {
        StateWeights[CurrentState] = 1.0f;
    }
    else
    {
        StateWeights[ActiveBlends[0].FromState] = 1.0f;

        for (int32 Index = 0; Index < ActiveBlends.Num(); ++Index)
        {
            const FActiveBlend& Blend = ActiveBlends[Index];
            const float Alpha = FMath::Clamp(Blend.Elapsed / Blend.Duration, 0.0f, 1.0f);
            const int32 TargetState = Index + 1 < ActiveBlends.Num() 
                                ? ActiveBlends[Index + 1].FromState 
                                : CurrentState;

            for (float& Weight : StateWeights)
            {
                Weight *= 1.0f - Alpha;
            }

            StateWeights[TargetState] += Alpha;
        }
    }

    for (int32 StateIndex = 0; StateIndex < States.Num(); ++StateIndex)
    {
        if (StateWeights[StateIndex] > 0.0f)
        {
            States[StateIndex].Pose.Update(Context.FractionalWeight(StateWeights[StateIndex]));
        }
    }

    ElapsedTime += DeltaTime;
}

void FAnimNode_StateMachine::Evaluate(FPoseContext& Output)
{
    if (States.IsEmpty())
    {
        Output.ResetToRefPose();
        return;
    }

    if (ActiveBlends.IsEmpty())
    {
        States[CurrentState].Pose.Evaluate(Output);
        return;
    }

    FPoseContext Blended(Output);
    States[ActiveBlends[0].FromState].Pose.Evaluate(Blended);

    for (int32 Index = 0; Index < ActiveBlends.Num(); ++Index)
    {
        const FActiveBlend& Blend = ActiveBlends[Index];
        const float Alpha = FMath::Clamp(Blend.Elapsed / Blend.Duration, 0.0f, 1.0f);
        const int32 TargetState = Index + 1 < ActiveBlends.Num() ? ActiveBlends[Index + 1].FromState : CurrentState;
        FPoseContext Target(Output);
        FPoseContext Combined(Output);
        States[TargetState].Pose.Evaluate(Target);

        FAnimationRuntime::BlendTwoPosesTogether(Blended.Pose, Target.Pose, Blended.Curve, Target.Curve, 1.0f - Alpha, Combined.Pose, Combined.Curve);
        Blended.Pose = std::move(Combined.Pose);
        Blended.Curve = std::move(Combined.Curve);
    }

    Output.Pose = std::move(Blended.Pose);
    Output.Curve = std::move(Blended.Curve);
}

void FAnimNode_StateMachine::Initialize(const FAnimationInitializeContext& Context)
{
	CurrentState = InitialState;
    ActiveBlends.Empty();
	if (States.Num() > 0)
	{
		States[CurrentState].Pose.Initialize(Context);
	}

	ElapsedTime = 0.f;
	bFirstEnter = true;
}

//상태 전환 검사 및 처리
void FAnimNode_StateMachine::ProcessState(UAnimInstance* AnimInstance)
{
	FAnimTransitionContext TransitionContext;
	TransitionContext.AnimInstance = AnimInstance;

	//TODO : 상태 안 시퀀스의 남은 시간 계산 필요
    //AnimBlueprint의 Sequence Finished는 Sequence의 IsFinished만 검사한다.
	TransitionContext.StateTimeRemaining = -1.0f;

	for (int32 Step = 0; Step < MaxTransitionsPerFrame; ++Step)
	{
		// 새 상태 기준으로 갱신
		TransitionContext.StateElapsedTime = ElapsedTime;

		const FAnimTransitionDesc* Taken = nullptr;
		for (int32 TransitionIndex : BakedTransitions[CurrentState])
		{
			const FAnimTransitionDesc& T = Transitions[TransitionIndex];

			// 전환 규칙 검사
			// TODO : StateTimeRemaining이 작동하기 전까지 bAutomatic은 무시하고 Rule만 검사하도록 한다.
			//const bool bPass = Transition.bAutomatic
			//	? (TransitionContext.StateTimeRemaining >= 0.f && TransitionContext.StateTimeRemaining <= Transition.CrossfadeDuration)
			//	: (Transition.Rule && Transition.Rule(TransitionContext));

			if (T.Rule && T.Rule(TransitionContext))
			{ 
				Taken = &T;
				break;
			}
		}

		// 더 넘어갈 곳이 없으면 종료
		if (!Taken) break; 
		ChangeState(*Taken, AnimInstance);
	}
}

void FAnimNode_StateMachine::ChangeState(const FAnimTransitionDesc& Transition, UAnimInstance* AnimInstance)
{
	HTR_LOG(Info, "[AnimStateMachine] {} -> {} ({}, after {:.2f}s, crossfade {:.2f}s)",
		States[CurrentState].Name.ToString(), States[Transition.To].Name.ToString(),
		Transition.From == INDEX_NONE ? "Any State" : "Transition",
		ElapsedTime, Transition.CrossfadeDuration);

	if (States[CurrentState].OnExit)
		States[CurrentState].OnExit(AnimInstance);

    if (Transition.CrossfadeDuration > 0.0f)
    {
        ActiveBlends.Add({ CurrentState, 0.0f, Transition.CrossfadeDuration });
    }
    else
    {
        ActiveBlends.Empty();
    }

	CurrentState = Transition.To;
	ElapsedTime = 0.0f;

	//Entry 진입 시 상태를 리셋한다.
	if (States[CurrentState].bResetOnEntry)
	{
		// Reset the state
		FAnimationInitializeContext InitializeContext;
		States[CurrentState].Pose.Initialize(InitializeContext);
	}

	if (States[CurrentState].OnEnter)
		States[CurrentState].OnEnter(AnimInstance);
}

void FAnimNode_StateMachine::Bake()
{
	BakedTransitions.SetNum(States.Num());
	for (int32 S = 0; S < States.Num(); ++S)
	{
		TArray<int32>& List = BakedTransitions[S];

		List = ExitTransitions[S];
		for (int32 AnyIndex : AnyStateTransitions)
			if (Transitions[AnyIndex].To != S)      // 자기 자신으로 가는 Any 전환은 제외
				List.Add(AnyIndex);

		// Priority 기준 stable sort
		std::stable_sort(List.begin(), List.end(), [this](int32 a, int32 b)
		{
			return Transitions[a].Priority < Transitions[b].Priority;
		});
	}
}
