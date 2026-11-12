#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "MyAnimInstance.generated.h"

UCLASS()
class UMyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// 공격 중이면 무시한다. false 복귀는 Attack 상태의 OnExit가 맡는다
	void RequestAttack() { if (!bIsAttacking) bIsAttacking = true; }
	bool IsAttacking() const { return bIsAttacking; }
	// false 복귀는 Hit 상태의 OnExit가 맡는다
	void SetIsHit(bool bInIsHit) { bIsHit = bInIsHit; }

private:
	float Speed = 0.0f;
	bool bIsAttacking = false;
	bool bIsHit = false;

	// 상태마다 포즈를 내는 노드
	FAnimNode_StateMachine     StateMachine;
	FAnimNode_BlendSpacePlayer LocomotionPlayer;   // Locomotion: Idle / Walk / Run Blend Space
	FAnimNode_SequencePlayer   AttackPlayer;       // Attack: 공격 클립 한 번 재생
	FAnimNode_SequencePlayer   HitPlayer;          // Hit: 피격 클립 한 번 재생

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
