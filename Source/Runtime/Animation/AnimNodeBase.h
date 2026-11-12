#pragma once

#include "Animation/BonePose.h"

class UAnimInstance;

// UE는 UAnimInstance 대신 FAnimInstanceProxy를 들고 있다. 이 엔진은 워커 스레드 평가가 없어 인스턴스를 직접 쓴다.
struct FAnimationUpdateContext
{
	FAnimationUpdateContext(UAnimInstance* InAnimInstance, float InDeltaTime)
		: AnimInstance(InAnimInstance), DeltaTime(InDeltaTime) {}

	float GetDeltaTime() const { return DeltaTime; }

	// 호출하는 쪽에 T의 정의가 include되어 있어야 한다
	template <typename T>
	T* GetAnimInstance() const { return static_cast<T*>(AnimInstance); }

	//BlendNode가 가중치를 줄일 때 사용
	FAnimationUpdateContext FractionalWeight(float WeightMultiplier) const
	{
		FAnimationUpdateContext Result(*this);
		Result.CurrentWeight = CurrentWeight * WeightMultiplier;
		return Result;
	}

	float GetFinalBlendWeight() const { return CurrentWeight; }

private:
	UAnimInstance* AnimInstance = nullptr;
	float DeltaTime = 0.0f;
	float CurrentWeight = 1.0f;
};

struct FPoseContext
{
	FPoseContext(UAnimInstance* InAnimInstance, const TArray<FBoneInfo>& InRefSkeleton)
		: AnimInstance(InAnimInstance), RefSkeleton(&InRefSkeleton)
	{
		Pose.SetNumBones(InRefSkeleton.Num());
	}

	// UE와 같이 포즈 값은 복사하지 않고 같은 스켈레톤용 출력 공간만 준비한다.
	// FPoseContext PoseA(Output) 직후의 Pose 값은 항등 트랜스폼이며 Evaluate가 채운다.
	FPoseContext(const FPoseContext& Source)
		: AnimInstance(Source.AnimInstance), RefSkeleton(Source.RefSkeleton)
	{
		Pose.SetNumBones(Source.Pose.GetNumBones());
	}
	FPoseContext& operator=(const FPoseContext&) = delete;

	template <typename T>
	T* GetAnimInstance() const { return static_cast<T*>(AnimInstance); }

	const TArray<FBoneInfo>& GetRefSkeleton() const { return *RefSkeleton; }
	void ResetToRefPose() { Pose.ResetToRefPose(*RefSkeleton); }

	FCompactPose Pose;

	FBlendedCurve Curve;

private:
	UAnimInstance* AnimInstance = nullptr;
	const TArray<FBoneInfo>* RefSkeleton = nullptr;
};

struct FAnimationInitializeContext
{

};

// 모든 노드의 뿌리. 프레임마다 Update -> Evaluate 순서로 불린다.
// UE의 Update_AnyThread / Evaluate_AnyThread에 해당한다.
struct FAnimNode_Base
{
	virtual ~FAnimNode_Base() = default;

	virtual void Update(const FAnimationUpdateContext& Context) {}
	// 출력은 항상 Output.Pose의 모든 본을 채운다. Rotation은 단위 쿼터니언이어야 한다.
	virtual void Evaluate(FPoseContext& Output) { Output.ResetToRefPose(); }
	virtual void Initialize(const FAnimationInitializeContext& Context) {}
};

// 다른 노드로 이어지는 입력 핀. 연결이 없으면 바인드 포즈를 낸다.
struct FPoseLink
{
	void SetLinkNode(FAnimNode_Base* InLinkedNode) { LinkedNode = InLinkedNode; }

	void Update(const FAnimationUpdateContext& Context)
	{
		if (LinkedNode)
			LinkedNode->Update(Context);
	}

	void Evaluate(FPoseContext& Output)
	{
		if (LinkedNode)
			LinkedNode->Evaluate(Output);
		else
			Output.ResetToRefPose();
	}

	void Initialize(const FAnimationInitializeContext& Context)
	{
		if (LinkedNode)
			LinkedNode->Initialize(Context);
	}

private:
	FAnimNode_Base* LinkedNode = nullptr;
};
