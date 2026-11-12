#pragma once

#include "UObject/Object.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimationAsset.h"
#include "AnimInstance.generated.h"

struct FAnimNode_Base;
struct FPoseContext;
class AActor;
class APawn;
class USkeletalMeshComponent;

// Unreal ERootMotionMode. 이 AnimInstance(캐릭터)가 시퀀스의 루트 모션을 쓸지 정한다.
// 시퀀스 설정(Enable Root Motion)은 "클립에 루트 모션이 있다", 이 모드는 "이 캐릭터가 그걸 쓴다"
// 기본값은 하위 AnimInstance 생성자에서(언리얼 AnimBP Class Defaults), 런타임엔 SetRootMotionMode로 바꾼다
UENUM()
enum class ERootMotionMode : uint8
{
	NoRootMotionExtraction,    // 추출 안 함, root 고정 안 함 → 애니메이션 그대로
	IgnoreRootMotion,          // 추출·root 고정은 하지만 버림 → 제자리 재생
	RootMotionFromEverything,  // 추출·root 고정·Actor 이동 (기본값)
};

UCLASS()
class UAnimInstance : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UAnimInstance() override;

	void InitializeAnimation();   
	void UpdateAnimation(float DeltaSeconds);
	void EvaluateAnimation(FPoseContext& Output);
	void TriggerAnimNotifies(float DeltaSeconds);
	void UninitializeAnimation(); 

	USkeletalMeshComponent* GetSkelMeshComponent() const;
	AActor* GetOwningActor() const;
	APawn* TryGetPawnOwner() const;

	FAnimNode_Base* GetRootNode() const { return RootNode; }

	void AddNotifies(const TArray<FAnimNotifyEventReference>& ActiveNotifies, const float Weight);

	// 루트 모션: 노드가 Update 중에 뽑은 delta를 모으고, SkeletalMeshComponent가 한 번에 가져간다 (Notify와 같은 흐름)
	void AddRootMotion(const FTransform& Delta) { ExtractedRootMotion.Accumulate(Delta); }
	FRootMotionMovementParams ConsumeExtractedRootMotion();

	ERootMotionMode GetRootMotionMode() const { return RootMotionMode; }
	void SetRootMotionMode(ERootMotionMode InMode) { RootMotionMode = InMode; }
	// 노드가 루트 모션을 뽑아야 하는가 (추출 여부 = root 고정 여부)
	bool ShouldExtractRootMotion() const { return RootMotionMode != ERootMotionMode::NoRootMotionExtraction; }
	// 이름 기반 NotifyState(UAnimNotifyState_Named)가 부른다. 파생 AnimInstance가 이름으로 처리한다.
	virtual void HandleNamedNotifyStateBegin(const FName& NotifyName, float TotalDuration) {}
	virtual void HandleNamedNotifyStateTick(const FName& NotifyName, float DeltaTime) {}
	virtual void HandleNamedNotifyStateEnd(const FName& NotifyName) {}

protected:
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}
	virtual void NativeUninitializeAnimation() {}
	// 클래스 없이 이름만 있는 Notify(FAnimNotifyEvent::NotifyName)를 받는다
	virtual void HandleNamedNotify(const FName& NotifyName) {}

	// 진행 중인 NotifyState에 End를 보내고 비운다. 시퀀스를 지우기 전에 불러야 한다.
	void EndAllAnimNotifyStates();

protected:
	FAnimNode_Base* RootNode = nullptr;
	FAnimNotifyQueue NotifyQueue;
	FRootMotionMovementParams ExtractedRootMotion;  // root 부모 공간 기준 누적값
	ERootMotionMode RootMotionMode = ERootMotionMode::RootMotionFromEverything;

private:
	// 지난 프레임까지 Begin을 받고 아직 End를 받지 않은 NotifyState들.
	// 이번 프레임 큐와 비교해 새로 들어온 것은 Begin, 사라진 것은 End를 보낸다 (UE ActiveAnimNotifyState).
	// 주의: 시퀀스의 Notify 배열 원소를 포인터로 가리키므로 재생 중에 시퀀스 Notify를 편집하면 안 된다.
	TArray<FAnimNotifyEventReference> ActiveAnimNotifyStates;
};
