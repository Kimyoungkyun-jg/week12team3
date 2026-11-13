#pragma once

#include "Animation/AnimInstance.h"
#include <vector>
#include "LuaAnimInstance.generated.h"

// 상태 전환을 Lua 스크립트가 직접 정하는 AnimInstance. C++은 상태별 포즈 재생과 블렌드만 맡는다.
// 상태 머신에 조건부 전환은 하나도 넣지 않는다. 전환은 스크립트가 TransitionTo로 건다.
// 등록한 상태 정의는 따로 들고 있어 메시가 바뀌어 노드를 다시 만들어도 남는다.
UCLASS()
class ULuaAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	bool AddSequenceState(const FString& Name, const FString& AssetPath, bool bLoop, float PlayRate);
	bool AddBlendSpaceState(const FString& Name, const FString& AssetPath, bool bLoop, float PlayRate);
	bool HasState(const FString& Name) const;

	// 이미 그 상태이거나 그 상태로 가는 중이면 bRestart일 때만 처음부터 다시 튼다.
	bool TransitionTo(const FString& Name, float BlendTime, bool bRestart);
	// Blend Space 상태의 입력. 시퀀스 상태면 false.
	bool SetInput(const FString& Name, float X, float Y);

	// 전환 중이면 들어가는 중인 상태
	FString GetState() const;
	bool IsInTransition() const;
	// 시퀀스 상태 전용. 반복 재생이거나 다른 종류면 끝나지 않는다(false, 무한대).
	bool IsFinished(const FString& Name) const;
	float GetRemainingTime(const FString& Name) const;
	// 남은 재생 시간이 Time 이하인가. 클립 길이의 절반보다 일찍 참이 되지는 않는다.
	bool IsNearEnd(const FString& Name, float Time) const;

	// 소유 캐릭터의 이동 정보. 캐릭터가 아니면 0/false.
	float GetSpeed() const;
	float GetDirection() const;
	bool IsGrounded() const;
	bool IsFalling() const;
	uint32 GetJumpCounter() const;

protected:
	void NativeInitializeAnimation() override;
	// 포즈를 진행하기 직전에 스크립트의 AnimTick(dt)을 부른다. 같은 프레임 안에서 판단 → 재생 순서가 보장된다.
	void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	enum class EPoseKind : uint8 { Sequence, BlendSpace };
	struct FStateDef
	{
		FString Name;
		EPoseKind Kind = EPoseKind::Sequence;
		FString AssetPath;
		bool bLoop = false;
		float PlayRate = 1.0f;
	};
	// 상태 머신의 상태 번호와 같은 순서다.
	struct FStateNodes
	{
		FAnimNode_SequencePlayer* Sequence = nullptr;
		FAnimNode_BlendSpacePlayer* BlendSpace = nullptr;
	};

	bool AddState(const FStateDef& Def);
	bool BuildStateNode(const FStateDef& Def);
	const FStateNodes* FindNodes(const FString& Name) const;

	std::vector<FStateDef> Defs;
	std::vector<FStateNodes> Nodes;
	FAnimNode_StateMachine* Machine = nullptr;
	// 노드를 다시 만든 뒤 되돌아갈 상태. 다시 만들면 상태 머신이 첫 상태로 초기화되기 때문이다.
	FString RequestedState;
	bool bRestoreRequestedState = false;
};
