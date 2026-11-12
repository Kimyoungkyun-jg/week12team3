#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include <functional>
#include <memory>
#include "AnimGraphInstance.generated.h"

class UAnimSequence;
class UBlendSpace1D;

enum class EAnimGraphCompare : uint8
{
	Less,
	LessEqual,
	Greater,
	GreaterEqual,
	Equal,
	NotEqual,
};

// 런타임에 노드 그래프를 조립하는 AnimInstance.
// Lua 바인딩과 (예정) AnimGraph 노드 에디터가 같은 API로 그래프를 만든다.
//
// - 노드와 상태는 int32 Index로 가리킨다. 에디터의 노드 Id와 1:1로 대응시키기 쉽다.
// - 변수(Float/Bool)는 이름으로 읽고 쓴다. 노드 입력(BlendSpace X, Blend Alpha)은 변수에 바인딩한다 (UE 핀 바인딩).
// - 전환 규칙/상태 콜백은 std::function이다. 에디터용 데이터 규칙은 Make*Rule / Make*Action이 만든다.
// - 시퀀스는 인스턴스마다 따로 로드해 소유한다. Notify를 붙여도 다른 캐릭터와 섞이지 않는다.
// - 조립 순서: 에셋 로드 → 노드/상태/전환 추가 → SetRoot (SetRoot가 그래프를 초기화한다)
UCLASS()
class UAnimGraphInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
    UAnimGraphInstance();
    void ClearNotifyBindings();
	using FStateCallback = std::function<void(UAnimInstance*)>;
	using FNotifyCallback = std::function<void()>;
	using FNotifyStateCallback = std::function<void(float)>;   // Begin: 전체 길이, Tick: DeltaTime, End: 0

	// 매 프레임 자동으로 채우는 변수: 소유 Pawn의 수평 속도 (cm/s, UE GroundSpeed)
	static constexpr const char* GroundSpeedVariable = "GroundSpeed";
	// 매 프레임 자동으로 채우는 변수: 소유 Character가 공중이면 true (UE IsFalling, 절벽에서 떨어질 때도 켜진다)
	static constexpr const char* IsFallingVariable = "IsFalling";

	virtual ~UAnimGraphInstance() override;

	// 노드/상태/변수/콜백/소유 에셋을 전부 지운다. 바인드 포즈로 돌아간다.
	// 애니메이션 콜백(전환 규칙, Notify 핸들러) 안에서 부르면 안 된다.
	void ResetGraph();
	// ResetGraph마다 증가한다. 외부 핸들(Lua)이 지워진 그래프를 가리키는지 확인할 때 쓴다.
	uint32 GetGeneration() const { return Generation; }

	// ---------------- 에셋 (이 인스턴스가 소유) ----------------
	UAnimSequence* LoadSequence(const FString& Path);
	UBlendSpace1D* CreateBlendSpace();
	UBlendSpace1D* LoadBlendSpace(const FString& Path);
	bool OwnsSequence(const UAnimSequence* Sequence) const;
	bool OwnsBlendSpace(const UBlendSpace1D* BlendSpace) const;

	// ---------------- 노드 ----------------
	int32 AddSequencePlayer(UAnimSequence* Sequence, bool bLooping, float PlayRate = 1.0f);
	int32 AddBlendSpacePlayer(UBlendSpace1D* BlendSpace, const FString& XVariable, float PlayRate = 1.0f);
	int32 AddTwoWayBlend(int32 NodeA, int32 NodeB, const FString& AlphaVariable);
	int32 AddStateMachine();

	bool IsValidNode(int32 NodeIndex) const { return NodeIndex >= 0 && NodeIndex < Nodes.Num(); }
	int32 GetNumNodes() const { return Nodes.Num(); }

	template <typename T>
	T* GetNode(int32 NodeIndex) const
	{
		return IsValidNode(NodeIndex) ? dynamic_cast<T*>(Nodes[NodeIndex].get()) : nullptr;
	}

	// 그래프 출력 노드를 정하고 초기화한다. 조립이 끝난 뒤 마지막에 부른다.
	bool SetRoot(int32 NodeIndex);
	int32 GetRootIndex() const { return RootNodeIndex; }

	// ---------------- 상태 머신 ----------------
	// 실패하면 INDEX_NONE
	int32 AddState(int32 MachineNode, const FName& Name, int32 PoseNode, bool bResetOnEntry = true,
		FStateCallback OnEnter = {}, FStateCallback OnExit = {});
	// FromState가 INDEX_NONE이면 Any State 전환
	int32 AddTransition(int32 MachineNode, int32 FromState, int32 ToState, FAnimTransitionRule Rule,
		float CrossfadeDuration = 0.2f, int32 Priority = 0);
	bool SetInitialState(int32 MachineNode, int32 StateIndex);
	int32 GetStatePoseNode(int32 MachineNode, int32 StateIndex) const;
	FName GetCurrentStateName(int32 MachineNode) const;

	// 데이터로 표현되는 규칙/동작 (노드 에디터가 저장·복원할 수 있는 형태)
	FAnimTransitionRule MakeBoolRule(const FString& Variable, bool bExpected = true);
	FAnimTransitionRule MakeFloatRule(const FString& Variable, EAnimGraphCompare Compare, float Value);
	// SequencePlayer 노드가 루프 없이 끝까지 재생됐으면 true
	FAnimTransitionRule MakeFinishedRule(int32 SequencePlayerNode);
	FStateCallback MakeSetBoolAction(const FString& Variable, bool bValue);
	FStateCallback MakeSetFloatAction(const FString& Variable, float Value);
	// 이름 Notify를 보낸다. BindNotify 콜백이나 C++ HandleNamedNotify override가 받는다.
	FStateCallback MakeNamedNotifyAction(const FName& NotifyName);
	// 여러 동작을 순서대로 실행하는 하나의 콜백. 비어 있으면 빈 콜백을 돌려준다.
	static FStateCallback CombineActions(TArray<FStateCallback> Actions);

	// ---------------- 변수 ----------------
	void SetFloat(const FString& Name, float Value) { FloatVariables[Name] = Value; }
	float GetFloat(const FString& Name, float Default = 0.0f) const;
	void SetBool(const FString& Name, bool bValue) { BoolVariables[Name] = bValue; }
	bool GetBool(const FString& Name, bool bDefault = false) const;

	// ---------------- 이름 기반 Notify ----------------
	// 단발(Duration 0) 이름 Notify
	void BindNotify(const FName& Name, FNotifyCallback Callback);
	// UAnimNotifyState_Named. 비어 있는 콜백은 무시한다.
	void BindNotifyState(const FName& Name, FNotifyStateCallback OnBegin, FNotifyStateCallback OnTick, FNotifyStateCallback OnEnd);

	virtual void HandleNamedNotifyStateBegin(const FName& NotifyName, float TotalDuration) override;
	virtual void HandleNamedNotifyStateTick(const FName& NotifyName, float DeltaTime) override;
	virtual void HandleNamedNotifyStateEnd(const FName& NotifyName) override;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void HandleNamedNotify(const FName& NotifyName) override;

private:
	enum class EBindingTarget : uint8 { BlendSpaceX, TwoWayBlendAlpha };

	// 매 프레임 변수 값을 노드 입력에 써 넣는다
	struct FVariableBinding
	{
		int32 NodeIndex = INDEX_NONE;
		EBindingTarget Target = EBindingTarget::BlendSpaceX;
		FString Variable;
	};

	struct FNotifyStateHandlers
	{
		FNotifyStateCallback OnBegin;
		FNotifyStateCallback OnTick;
		FNotifyStateCallback OnEnd;
	};

	int32 AddNode(std::unique_ptr<FAnimNode_Base> Node);
	void ClearGraph();   // 노드와 소유 에셋 해제 (EndAllAnimNotifyStates는 호출하는 쪽에서)

	TArray<std::unique_ptr<FAnimNode_Base>> Nodes;
	TMap<int32, TArray<int32>> StatePoseNodes;   // 상태 머신 노드 → 상태별 포즈 노드
	TArray<FVariableBinding> Bindings;
	int32 RootNodeIndex = INDEX_NONE;

	TMap<FString, float> FloatVariables;
	TMap<FString, bool> BoolVariables;

	TMap<FString, FNotifyCallback> NotifyHandlers;
	TMap<FString, FNotifyStateHandlers> NotifyStateHandlers;

	TArray<UAnimSequence*> OwnedSequences;
	TArray<UBlendSpace1D*> OwnedBlendSpaces;

	uint32 Generation = 0;
};
