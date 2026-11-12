#pragma once

#include "AnimNodeBase.h"
#include <functional>
#include "Core/NameTypes.h"

// 규칙이 읽는 정보. 규칙은 읽기만 하고 상태를 바꾸지 않는다.
struct FAnimTransitionContext
{
    UAnimInstance* AnimInstance = nullptr;
    float StateElapsedTime = 0.f;        // 현재 상태에 들어온 뒤 지난 시간
    float StateTimeRemaining = -1.0f;      // 상태 안 시퀀스의 남은 시간 (없으면 -1)

    template <typename T> T* GetAnimInstance() const { return static_cast<T*>(AnimInstance); }
};
using FAnimTransitionRule = std::function<bool(const FAnimTransitionContext&)>;

// 상태 전이 규칙
struct FAnimTransitionDesc
{
    int32 From = INDEX_NONE;        // INDEX_NONE이면 Any State
    int32 To = INDEX_NONE;
    FAnimTransitionRule Rule;       // 비어 있고 bAutomatic이면 자동 규칙
	float CrossfadeDuration = 0.2f; // 전환 시 페이드 시간
    int32 Priority = 0;             // 작을수록 먼저 검사
    bool  bAutomatic = false;       // 남은 시간 <= CrossfadeDuration이면 전환
};


// 상태
struct FAnimStateDesc
{
    FName Name;
    FPoseLink Pose;

    // 상태 진입 시 상태 노드를 초기화 할 것인지
    bool bResetOnEntry = true;

    //콜백 함수 내에서 State, Transition을 등록하면 안된다.
    std::function<void(UAnimInstance*)> OnEnter;
    std::function<void(UAnimInstance*)> OnExit;
};

struct FAnimNode_StateMachine : public FAnimNode_Base
{
    // 등록 API
    int32 AddState(const FAnimStateDesc& Desc);
    int32 AddState(FName Name, FAnimNode_Base* Node);
    int32 AddTransition(const FAnimTransitionDesc& Desc);
    void  SetInitialState(int32 StateIndex);
    FName GetCurrentStateName() const;     // ImGui 디버그용
    int32 GetCurrentStateIndex() const;

    void Update(const FAnimationUpdateContext& Context) override;
    void Evaluate(FPoseContext& Output) override;
    void Initialize(const FAnimationInitializeContext& Context) override;
private:
    void ProcessState(UAnimInstance* AnimInstance);
    void ChangeState(const FAnimTransitionDesc& Transition, UAnimInstance* AnimInstance);

    TArray<FAnimStateDesc>      States;                 // 상태 목록
    TArray<FAnimTransitionDesc> Transitions;            // 전환 목록
    TArray<TArray<int32>>       ExitTransitions;        // 상태별 나가는 전환
	TArray<int32>               AnyStateTransitions;    // 모든 상태에서 나갈 수 있는 전환
    TArray<TArray<int32>>       BakedTransitions;       // 상태별 : Exit + Any 합친 목록

    int32 CurrentState = 0;
    int32 InitialState = 0;
    float ElapsedTime = 0.0f;

    struct FActiveBlend { int32 FromState; float Elapsed; float Duration; };
    TArray<FActiveBlend> ActiveBlends;

    int32 MaxTransitionsPerFrame = 3;
private:
    // 전환 규칙들을 Priority 순으로 정렬하여 Bake
    void Bake();
    bool bTransitionDirty = true;
	bool bFirstEnter = true; // 첫 진입 시 OnEnter 호출 여부
};
