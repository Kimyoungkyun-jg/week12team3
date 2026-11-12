#pragma once

#include "Animation/AnimNotifyState.h"
#include "AnimNotifyState_AttackWindow.generated.h"

class AActor;

// 공격 판정 구간. 구간 동안 매 프레임 전방으로 Sweep하고,
// 한 번의 공격(Begin~End)에서 같은 액터는 한 번만 맞힌다.
UCLASS()
class UAnimNotifyState_AttackWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	// 캡슐 중심 기준, 월드 단위
	UPROPERTY(EditAnywhere)
	float AttackHeight = 0.4f;   // 캡슐 중심에서 가슴 높이까지

	UPROPERTY(EditAnywhere)
	float AttackRange = 1.2f;    // 캡슐 중심에서 주먹 끝까지

	UPROPERTY(EditAnywhere)
	float AttackRadius = 0.25f;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

private:
	// 이번 공격에서 이미 맞힌 액터.
	// Notify 객체는 시퀀스가 소유하므로, 시퀀스를 여러 캐릭터가 공유하면 이 목록도 공유된다.
	// 지금은 MyAnimInstance가 캐릭터마다 시퀀스를 따로 로드하므로 문제없다.
	TArray<AActor*> HitActors;
};
