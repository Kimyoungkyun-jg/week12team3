#include "EnginePCH.h"
#include "AnimNotifyQueue.h"

void FAnimNotifyQueue::AddAnimNotifies(bool bSrcIsLeader, const TArray<FAnimNotifyEventReference>& NewNotifies, const float InstanceWeight)
{
	AddAnimNotifiesToDest(bSrcIsLeader, NewNotifies, AnimNotifies, InstanceWeight);
}

void FAnimNotifyQueue::AddAnimNotifiesToDest(bool bSrcIsLeader, const TArray<FAnimNotifyEventReference>& NewNotifies, TArray<FAnimNotifyEventReference>& DestArray, const float InstanceWeight)
{
	// for now there is no filter whatsoever, it just adds everything requested
	for (const FAnimNotifyEventReference& NotifyRef : NewNotifies)
	{
		const FAnimNotifyEvent* Notify = NotifyRef.GetNotify();

		if (Notify && bSrcIsLeader)
		{
			// TODO : PassFiltering = LOD 필터, LOD가 낮은 캐릭터의 Notify를 끈다.
			// TODO : PassChanceOfTriggering = 확률 발동, Event가 확률이 있을 때(NotifyTriggerChance) 확률적으로 통과
			if (Notify->TriggerWeightThreshold <= InstanceWeight
				//&& PassesFiltering(Notify) && PassesChanceOfTriggering(Notify)
				)
			{
				// TODO : Context 기능을 추가할 때, 여기서 Context 안의 조건을 검사
				//const UE::Anim::IAnimEventsFilterContext* ContextData = Notify->bCanBeFilteredViaRequest ? NotifyRef.GetContextData<UE::Anim::IAnimEventsFilterContext>() : nullptr;
				//const bool bPassedScopeFilter = ContextData ? !ContextData->ShouldFilterNotify(NotifyRef) : true;

				//if (bPassedScopeFilter)
				//{
					// AnimNotifyState instance는 한 번만 더한다.
					Notify->NotifyStateClass ? DestArray.AddUnique(NotifyRef) : DestArray.Add(NotifyRef);
				//}
			}
		}
	}
}

