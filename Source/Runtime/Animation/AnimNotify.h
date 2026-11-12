#pragma once

#include "UObject/Object.h"
#include "Math/Color.h"
#include "AnimNotify.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;
struct FAnimNotifyEventReference;   // const 참조로만 받으므로 전방 선언으로 충분

UCLASS()
class UAnimNotify : public UObject
{
	GENERATED_BODY()

public:
    // 소유 시퀀스의 Notify 목록에서 자신을 지운다 (엔진 종료 때 시퀀스가 다시 delete하지 않게)
    virtual ~UAnimNotify() override;

    virtual void Notify(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) {}
};

UCLASS()
class UAnimNotify_PlaySound : public UAnimNotify
{
    GENERATED_BODY()
public:

    UPROPERTY(EditAnywhere)
    FString SoundPath;

    UPROPERTY(EditAnywhere)
    float Volume = 1.f;

    virtual void Notify(USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};