#pragma once

#include "AnimTypes.h"
#include "AnimNotifyQueue.generated.h"

class UAnimSequenceBase;

USTRUCT()
struct FAnimNotifyEventReference
{
    GENERATED_BODY()

    FAnimNotifyEventReference() = default;
    FAnimNotifyEventReference(const FAnimNotifyEvent* InNotify, const UAnimSequenceBase* InSource)
        : Notify(InNotify), Source(InSource) {}

    const FAnimNotifyEvent* Notify = nullptr;
    const UAnimSequenceBase* Source = nullptr;

    const FAnimNotifyEvent* GetNotify() const { return Notify; }

    friend bool operator==(const FAnimNotifyEventReference& L, const FAnimNotifyEventReference& R)
    {
        return L.Notify == R.Notify;
    }
};

USTRUCT()
struct FAnimNotifyContext
{
	GENERATED_BODY()
	FAnimNotifyContext() = default;
	explicit FAnimNotifyContext(bool bIsLoop, const UAnimSequenceBase *Sequence) : bIsLooping(bIsLoop), AnimSequenceBase(Sequence) {}

    bool bIsLooping = false;
    bool bHasFinished = false;
	TArray<FAnimNotifyEventReference> ActiveNotifies;
    const UAnimSequenceBase* AnimSequenceBase = nullptr;
};


USTRUCT()
struct FAnimNotifyQueue
{
    GENERATED_BODY()

    void AddAnimNotifies(bool bSrcIsLeader, const TArray<FAnimNotifyEventReference>& NewNotifies, const float InstanceWeight);
    void AddAnimNotifies(const TArray<FAnimNotifyEventReference>& NewNotifies, const float InstanceWeight) { AddAnimNotifies(true, NewNotifies, InstanceWeight); }
    void Reset() { AnimNotifies.Reset(); }

    TArray<FAnimNotifyEventReference> AnimNotifies;

private:
    void AddAnimNotifiesToDest(bool bSrcIsLeader, const TArray<FAnimNotifyEventReference>& NewNotifies, TArray<FAnimNotifyEventReference>& DestArray, const float InstanceWeight);
};
