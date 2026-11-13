#pragma once

#include "Containers/Array.h"
#include "Animation/AnimNodeBase.h"
#include "Math/Matrix.h"
#include "UObject/Object.h"
#include <utility>
#include "AnimInstance.generated.h"

class AActor;
class APawn;
class USkeletalMeshComponent;

UCLASS()
class UAnimInstance : public UObject
{
	GENERATED_BODY()
public:
	virtual ~UAnimInstance() override;
	USkeletalMeshComponent* GetOwningComponent() const;
	AActor* GetOwningActor() const;
	virtual APawn* TryGetPawnOwner() const;

	void InitializeAnimation();
	void UpdateAnimation(float DeltaSeconds);
	virtual bool EvaluatePose(TArray<FMatrix>& OutLocalPose);
	bool ConsumeRootMotion(FMatrix& OutDelta);
	// UpdateAnimation에서 노드 트리로부터 모은 Notify를 호출한다. 포즈와 본 행렬 갱신이 끝난 뒤(TickComponent 끝)에 불러야 한다.
	void TriggerAnimNotifies();

protected:
	template <typename T, typename... Args>
	T* CreateNode(Args&&... Arguments)
	{
		T* Node = new T(std::forward<Args>(Arguments)...);
		OwnedNodes.Add(Node);
		return Node;
	}
	FAnimNode_Base* RootNode = nullptr;
	void AddNotifyQueueItems(const TArray<FAnimNotifyQueueItem>& Items);
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}
private:
	TArray<FAnimNode_Base*> OwnedNodes;
	// 이번 프레임의 UpdateAnimation에서 모은 Notify. TriggerAnimNotifies가 소비한다.
	TArray<FAnimNotifyQueueItem> NotifyQueue;
	// 이번 UpdateAnimation에서 애니메이션이 진행한 시간(초). 0이면 일시정지나 스크럽이라 구간형 Notify의 시작·끝을 판정하지 않는다.
	float LastUpdateDelta = 0.0f;
	// 시작했고 아직 끝나지 않은 구간형 Notify. 상태 객체는 에셋이 공유하므로 컴포넌트마다 여기서 호출 상태를 관리한다.
	// 키는 (상태 객체, 올린 노드)다. 끝낼 때 Context를 만들 수 있도록 마지막 이벤트와 시퀀스를 들고 있다.
	struct FActiveNotifyState
	{
		UAnimNotifyState* State = nullptr;
		const void* SourceNode = nullptr;
		FAnimNotifyEvent Event;
		const UAnimSequence* Source = nullptr;
		float Weight = 1.0f;
		bool bSeen = false;
	};
	TArray<FActiveNotifyState> ActiveNotifyStates;
	// 구간형 항목 하나를 처리한다. 시작하지 않았으면 NotifyBegin, 아니면 NotifyTick, 끝 시각이 구간 안이면 NotifyEnd.
	void ProcessNotifyStateItem(const FAnimNotifyQueueItem& Item, USkeletalMeshComponent* Component, bool bGameWorld);
};
