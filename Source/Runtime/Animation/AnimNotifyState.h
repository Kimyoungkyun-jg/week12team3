#pragma once

#include "UObject/Object.h"
#include "Math/Color.h"
#include "AnimNotifyState.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;
struct FAnimNotifyEventReference;   // const 참조로만 받으므로 전방 선언으로 충분

UCLASS()
class UAnimNotifyState : public UObject
{
	GENERATED_BODY()
public:
	// 소유 시퀀스의 Notify 목록에서 자신을 지운다 (엔진 종료 때 시퀀스가 다시 delete하지 않게)
	virtual ~UAnimNotifyState() override;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference){}
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference){}
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference){}
};

// 클래스를 따로 만들지 않고 이름으로 받는 NotifyState.
// Begin/Tick/End를 메시의 AnimInstance::HandleNamedNotifyState*로 넘긴다 (Lua, AnimGraph에서 쓴다).
UCLASS()
class UAnimNotifyState_Named : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FString NotifyName;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};