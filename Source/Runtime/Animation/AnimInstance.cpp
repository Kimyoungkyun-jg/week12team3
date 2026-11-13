#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotify.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Render/SkeletalMesh.h"

UAnimInstance::~UAnimInstance()
{
	for (FAnimNode_Base* Node : OwnedNodes)
	{
		delete Node;
	}
}

USkeletalMeshComponent* UAnimInstance::GetOwningComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}

AActor* UAnimInstance::GetOwningActor() const
{
	USkeletalMeshComponent* Component = GetOwningComponent();
	return Component ? Component->GetOwner() : nullptr;
}

APawn* UAnimInstance::TryGetPawnOwner() const
{
	return Cast<APawn>(GetOwningActor());
}

void UAnimInstance::InitializeAnimation()
{
	RootNode = nullptr;
	for (FAnimNode_Base* Node : OwnedNodes)
	{
		delete Node;
	}
	OwnedNodes.Reset();
	NativeInitializeAnimation();
	if (RootNode)
	{
		RootNode->Reset();
	}
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	// 호출되지 않고 남은 Notify가 다음 프레임에 섞이지 않도록 먼저 비운다.
	NotifyQueue.Reset();
	// 이번에 애니메이션이 진행한 시간. 구간형 Notify의 시작·끝 판정에 쓴다 (0이면 판정하지 않는다).
	LastUpdateDelta = std::isfinite(DeltaSeconds) ? std::max(0.0f, DeltaSeconds) : 0.0f;
	//입력 변수 계산
	NativeUpdateAnimation(DeltaSeconds);
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (Mesh && Mesh->Skeleton && RootNode)
	{
		RootNode->Update(DeltaSeconds);
		// 전파: 트리의 Notify를 가중치와 함께 모은다. 호출은 TriggerAnimNotifies에서 한다.
		RootNode->GatherNotifies(1.0f, NotifyQueue);
	}
}

void UAnimInstance::AddNotifyQueueItems(const TArray<FAnimNotifyQueueItem>& Items)
{
	for (const FAnimNotifyQueueItem& Item : Items) NotifyQueue.Add(Item);
}

void UAnimInstance::TriggerAnimNotifies()
{
	USkeletalMeshComponent* Component = GetOwningComponent();
	if (!Component)
	{
		NotifyQueue.Reset();
		ActiveNotifyStates.Reset();
		return;
	}
	// 애니메이션이 진행하지 않은 프레임(일시정지, 스크럽)에는 모은 Notify가 없다. 이때 "이번 프레임에 안 보였다"를 이유로
	// 진행 중인 구간형을 끝내면 일시정지만 해도 끝나 버리므로, 구간형의 시작·끝 판정을 건너뛴다.
	const bool bAdvanced = LastUpdateDelta > 0.0f;
	if (NotifyQueue.IsEmpty() && (ActiveNotifyStates.IsEmpty() || !bAdvanced))
	{
		return;
	}
	const UWorld* World = Component->GetWorld();
	const bool bGameWorld = World && World->IsGameWorld();

	// Notify가 호출 중에 애니메이션 상태를 바꾸거나 객체를 파괴해도 순회가 깨지지 않도록 큐를 옮겨 두고 시작한다.
	TArray<FAnimNotifyQueueItem> Queue = std::move(NotifyQueue);
	NotifyQueue.Reset();
	for (FActiveNotifyState& Active : ActiveNotifyStates)
	{
		Active.bSeen = false;
	}

	// 항목은 큐 순서대로 처리한다. 루프하는 클립에서 한 프레임에 "끝남"과 "다시 시작"이 연달아 올 수 있어 키로 합치면 안 된다.
	for (const FAnimNotifyQueueItem& Item : Queue)
	{
		if (!IsValid(Component))
		{
			break;
		}
		if (Item.Event.IsState())
		{
			if (bAdvanced)
			{
				ProcessNotifyStateItem(Item, Component, bGameWorld);
			}
			continue;
		}
		UAnimNotify* Notify = Item.Event.Notify;
		if (!Notify || Item.Weight < Item.Event.TriggerWeightThreshold)
		{
			continue;
		}
		if (!bGameWorld && !Notify->CanTriggerInEditorWorld())
		{
			continue;
		}
		FAnimNotifyContext Context;
		Context.MeshComponent = Component;
		Context.Sequence = Item.Source;
		Context.Event = &Item.Event;
		Context.Weight = Item.Weight;
		Notify->Notify(Context);
	}

	// 이번 프레임에 한 번도 올라오지 않은 구간형은 중간에 끊긴 것이다 (가중치가 사라짐, 다른 위치로 이동, 노드가 바뀜).
	if (bAdvanced)
	{
		for (int32 Index = ActiveNotifyStates.Num() - 1; Index >= 0; --Index)
		{
			if (ActiveNotifyStates[Index].bSeen)
			{
				continue;
			}
			// 호출 중에 배열이 바뀌어도 되도록 복사해 두고 먼저 목록에서 뺀다.
			const FActiveNotifyState Ended = ActiveNotifyStates[Index];
			ActiveNotifyStates.RemoveAt(Index);
			if (!IsValid(Component) || !Ended.State)
			{
				continue;
			}
			FAnimNotifyContext Context;
			Context.MeshComponent = Component;
			Context.Sequence = Ended.Source;
			Context.Event = &Ended.Event;
			Context.Weight = Ended.Weight;
			Context.bInterrupted = true;
			Ended.State->NotifyEnd(Context);
		}
	}
}

void UAnimInstance::ProcessNotifyStateItem(const FAnimNotifyQueueItem& Item, USkeletalMeshComponent* Component, bool bGameWorld)
{
	UAnimNotifyState* State = Item.Event.NotifyState;
	if (!State)
	{
		return;
	}
	const bool bAllowed = bGameWorld || State->CanTriggerInEditorWorld();

	int32 ActiveIndex = INDEX_NONE;
	for (int32 Index = 0; Index < ActiveNotifyStates.Num(); ++Index)
	{
		if (ActiveNotifyStates[Index].State == State && ActiveNotifyStates[Index].SourceNode == Item.SourceNode)
		{
			ActiveIndex = Index;
			break;
		}
	}

	FAnimNotifyContext Context;
	Context.MeshComponent = Component;
	Context.Sequence = Item.Source;
	Context.Event = &Item.Event;
	Context.Weight = Item.Weight;
	Context.DeltaTime = LastUpdateDelta;

	if (ActiveIndex == INDEX_NONE)
	{
		// 가중치 문턱은 시작할 때만 본다. 시작한 뒤에는 가중치가 문턱 근처에서 흔들려도 시작과 끝이 매 프레임 반복되지 않도록 유지한다.
		if (!bAllowed || Item.Weight < Item.Event.TriggerWeightThreshold)
		{
			return;
		}
		FActiveNotifyState NewActive;
		NewActive.State = State;
		NewActive.SourceNode = Item.SourceNode;
		ActiveIndex = ActiveNotifyStates.Num();
		ActiveNotifyStates.Add(NewActive);
		State->NotifyBegin(Context);
		if (!IsValid(Component))
		{
			return;
		}
		// 호출 중에 배열이 바뀌었을 수 있으니 다시 찾는다.
		ActiveIndex = INDEX_NONE;
		for (int32 Index = 0; Index < ActiveNotifyStates.Num(); ++Index)
		{
			if (ActiveNotifyStates[Index].State == State && ActiveNotifyStates[Index].SourceNode == Item.SourceNode)
			{
				ActiveIndex = Index;
				break;
			}
		}
		if (ActiveIndex == INDEX_NONE)
		{
			return;
		}
	}

	FActiveNotifyState& Active = ActiveNotifyStates[ActiveIndex];
	Active.Event = Item.Event;
	Active.Source = Item.Source;
	Active.Weight = Item.Weight;
	Active.bSeen = true;

	if (Item.Event.bStateEnds)
	{
		// 끝 시각이 이번 구간 안에 있다. Tick 없이 바로 끝낸다. 목록에서 먼저 빼고 부른다.
		const FActiveNotifyState Ended = Active;
		ActiveNotifyStates.RemoveAt(ActiveIndex);
		Context.Event = &Ended.Event;
		State->NotifyEnd(Context);
		return;
	}
	State->NotifyTick(Context);
}

bool UAnimInstance::EvaluatePose(TArray<FMatrix>& OutLocalPose)
{
	OutLocalPose.Reset();
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton)
	{
		return false;
	}
	TArray<FAnimBoneTransform> Pose;
	if (!RootNode || !RootNode->Evaluate(Mesh->Skeleton, Pose) || Pose.Num() != Mesh->Skeleton->RefSkeleton.GetNum())
	{
		return false;
	}
	OutLocalPose.SetNum(Pose.Num());
	for (int32 Index = 0; Index < Pose.Num(); ++Index)
	{
		const FAnimBoneTransform& Bone = Pose[Index];
		OutLocalPose[Index] = MakeAnimMatrix(Bone.Position, Bone.Rotation, Bone.Scale);
	}
	return true;
}

bool UAnimInstance::ConsumeRootMotion(FMatrix& OutDelta)
{
	return RootNode && RootNode->ConsumeRootMotion(OutDelta);
}
