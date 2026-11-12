#include "EnginePCH.h"
#include "AnimInstance.h"
#include "AnimNodeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "UObject/Casts.h"
#include "UObject/UObjectGlobals.h"
#include "AnimTypes.h"
#include "AnimNotify.h"
#include "AnimNotifyState.h"

UAnimInstance::~UAnimInstance()
{
}

void UAnimInstance::InitializeAnimation()
{
	RootNode = nullptr;
	// 재초기화 시 이전 시퀀스는 이미 사라졌을 수 있으므로 End 없이 비운다
	ActiveAnimNotifyStates.Reset();
	NativeInitializeAnimation();
	if (RootNode)
	{
		FAnimationInitializeContext InitContext;
		RootNode->Initialize(InitContext);
	}
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	NotifyQueue.Reset();

	// 변수를 먼저 갱신해야 노드가 이번 프레임 값(Speed 등)으로 상태와 가중치를 정한다.
	NativeUpdateAnimation(DeltaSeconds);

	if (RootNode)
	{
		FAnimationUpdateContext Context(this, DeltaSeconds);
		RootNode->Update(Context);
	}
}

void UAnimInstance::EvaluateAnimation(FPoseContext& Output)
{
	if (RootNode)
	{
		RootNode->Evaluate(Output);
	}
	else
	{
		Output.ResetToRefPose();
	}
}

void UAnimInstance::TriggerAnimNotifies(float DeltaSeconds)
{
	USkeletalMeshComponent* SkeletalMeshComp = GetSkelMeshComponent();

	// 1) 이번 프레임 큐의 NotifyState를 지난 프레임 활성 목록과 비교한다.
	//    둘 다 있으면 계속 진행 중, 큐에만 있으면 새로 시작(Begin)이다.
	//    비교가 끝난 뒤 ActiveAnimNotifyStates에 남은 것은 이번 프레임에 빠진 것(End)이다.
	TArray<FAnimNotifyEventReference> NewActiveStates;

	// NotifyQueue.AnimNotifies와 같은 Index, Begin을 보낼지 여부 (TArray<bool>은 vector<bool> 프록시라 참조 반환이 안 된다)
	TArray<uint8> bIsNewState;
	bIsNewState.SetNum(NotifyQueue.AnimNotifies.Num());

	for (int32 i = 0; i < NotifyQueue.AnimNotifies.Num(); ++i)
	{
		bIsNewState[i] = 0;

		const FAnimNotifyEventReference& NotifyRef = NotifyQueue.AnimNotifies[i];
		const FAnimNotifyEvent* NotifyEvent = NotifyRef.GetNotify();
		if (!NotifyEvent || !NotifyEvent->NotifyStateClass)
			continue;

		const int32 ExistingIndex = ActiveAnimNotifyStates.Find(NotifyRef);
		if (ExistingIndex != -1)
			ActiveAnimNotifyStates.RemoveAtSwap(static_cast<uint32>(ExistingIndex));
		else
			bIsNewState[i] = 1;

		NewActiveStates.Add(NotifyRef);
	}

	// 2) 구간을 벗어났거나, 상태머신 State가 바뀌었거나, 가중치가 임계값 아래로 떨어져 빠진 것
	for (const FAnimNotifyEventReference& EndedRef : ActiveAnimNotifyStates)
	{
		if (const FAnimNotifyEvent* NotifyEvent = EndedRef.GetNotify())
			NotifyEvent->NotifyStateClass->NotifyEnd(SkeletalMeshComp, EndedRef.Source, EndedRef);
	}

	// 3) 단발 Notify와 NotifyState Begin을 큐 순서대로 보낸다
	for (int32 i = 0; i < NotifyQueue.AnimNotifies.Num(); ++i)
	{
		const FAnimNotifyEventReference& NotifyRef = NotifyQueue.AnimNotifies[i];
		const FAnimNotifyEvent* NotifyEvent = NotifyRef.GetNotify();
		if (!NotifyEvent)
			continue;

		if (NotifyEvent->NotifyStateClass)
		{
			if (bIsNewState[i] != 0)
				NotifyEvent->NotifyStateClass->NotifyBegin(SkeletalMeshComp, NotifyRef.Source, NotifyEvent->Duration, NotifyRef);
		}
		else if (NotifyEvent->Notify)
		{
			NotifyEvent->Notify->Notify(SkeletalMeshComp, NotifyRef.Source, NotifyRef);
		}
		// 이름 기반 Notify는 파생 AnimInstance가 HandleNamedNotify에서 처리한다
		else if (!NotifyEvent->NotifyName.IsNone())
		{
			HandleNamedNotify(NotifyEvent->NotifyName);
		}
	}

	// 4) 활성 목록을 교체하고, 이번 프레임에 활성인 모든 State에 Tick (Begin한 프레임 포함)
	ActiveAnimNotifyStates = std::move(NewActiveStates);
	for (const FAnimNotifyEventReference& ActiveRef : ActiveAnimNotifyStates)
	{
		ActiveRef.GetNotify()->NotifyStateClass->NotifyTick(SkeletalMeshComp, ActiveRef.Source, DeltaSeconds, ActiveRef);
	}
}

void UAnimInstance::EndAllAnimNotifyStates()
{
	USkeletalMeshComponent* SkeletalMeshComp = GetSkelMeshComponent();
	for (const FAnimNotifyEventReference& ActiveRef : ActiveAnimNotifyStates)
	{
		if (const FAnimNotifyEvent* NotifyEvent = ActiveRef.GetNotify())
			NotifyEvent->NotifyStateClass->NotifyEnd(SkeletalMeshComp, ActiveRef.Source, ActiveRef);
	}
	ActiveAnimNotifyStates.Reset();
}

void UAnimInstance::UninitializeAnimation()
{
	// 시퀀스가 아직 살아 있을 때 진행 중인 State를 닫는다 (Begin만 받고 End를 못 받는 일이 없게)
	EndAllAnimNotifyStates();
	NativeUninitializeAnimation();
	RootNode = nullptr;
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}

AActor* UAnimInstance::GetOwningActor() const
{
	USkeletalMeshComponent* OwnerComponent = GetSkelMeshComponent();
	return OwnerComponent ? OwnerComponent->GetOwner() : nullptr;
}

APawn* UAnimInstance::TryGetPawnOwner() const
{
	return Cast<APawn>(GetOwningActor());
}

void UAnimInstance::AddNotifies(const TArray<FAnimNotifyEventReference>& ActiveNotifies, const float Weight)
{
	NotifyQueue.AddAnimNotifies(ActiveNotifies, Weight);
}
FRootMotionMovementParams UAnimInstance::ConsumeExtractedRootMotion()
{
	// Unreal UAnimInstance::ConsumeExtractedRootMotion: 꺼내 주고 비운다
	FRootMotionMovementParams Result = ExtractedRootMotion;
	ExtractedRootMotion.Clear();

	// IgnoreRootMotion: 추출(→ root 고정)은 했지만 Actor로는 넘기지 않는다
	if (RootMotionMode == ERootMotionMode::IgnoreRootMotion)
	{
		Result.Clear();
	}
	return Result;
}
