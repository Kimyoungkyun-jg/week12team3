#include "EnginePCH.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/BoneTransform.h"
#include "Animation/Skeleton.h"
#include "Asset/AnimSequence.h"
#include "Asset/BlendSpace.h"
#include "Math/EngineMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
bool BlendRootMotion(FAnimNode_Base* A, FAnimNode_Base* B, float Alpha, FMatrix& OutDelta)
{
	FMatrix DeltaA, DeltaB;
	const bool HasA = A && A->ConsumeRootMotion(DeltaA);
	const bool HasB = B && B->ConsumeRootMotion(DeltaB);
	if (!HasA && !HasB)
	{
		return false;
	}
	const FBoneTransform TransformA = HasA ? FBoneTransform::FromMatrix(DeltaA) : FBoneTransform::Identity;
	const FBoneTransform TransformB = HasB ? FBoneTransform::FromMatrix(DeltaB) : FBoneTransform::Identity;
	OutDelta = FBoneTransform::Blend(TransformA, TransformB, std::clamp(Alpha, 0.0f, 1.0f)).ToMatrix();
	return true;
}

// 이미 이 노드가 올린 Notify가 큐에 있는가. 같은 노드가 여러 경로로 연결되어도 Notify가 두 번 호출되지 않도록,
// 처음 도달한 경로의 가중치로 한 번만 올린다. 합성 노드는 기본 포즈(가중치가 큰 쪽)를 먼저 올린다.
bool HasGatheredFrom(const TArray<FAnimNotifyQueueItem>& Queue, const void* Node)
{
	for (const FAnimNotifyQueueItem& Item : Queue)
	{
		if (Item.SourceNode == Node)
		{
			return true;
		}
	}
	return false;
}

bool EvaluateSequenceFrame(UAnimSequence* Sequence, const USkeleton* Skeleton, int32 FrameIndex, TArray<FAnimBoneTransform>& OutPose)
{
	if (!Sequence || !Skeleton || Sequence->Skeleton != Skeleton)
	{
		return false;
	}
	const UAnimDataModel* Model = Sequence->GetDataModel();
	if (!Model)
	{
		return false;
	}
	float EvalTime = 0.0f;
	const float Fps = Model->FrameRate.AsDecimal();
	if (FrameIndex > 0 && Fps > 0.0f)
	{
		EvalTime = std::clamp(static_cast<float>(FrameIndex) / Fps, 0.0f, Model->PlayLength);
	}
	return Sequence->EvaluateLocalPoseTransforms(EvalTime, OutPose);
}

void BuildBranchMask(const FReferenceSkeleton& RefSkeleton, int32 StartBoneIndex, TArray<uint8>& OutMask)
{
	const int32 NumBones = RefSkeleton.GetNum();
	OutMask.Init(0, NumBones);
	if (StartBoneIndex < 0 || StartBoneIndex >= NumBones)
	{
		return;
	}
	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		if (BoneIdx == StartBoneIndex)
		{
			OutMask[BoneIdx] = 1;
			continue;
		}
		const int32 ParentIdx = RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
		if (ParentIdx >= 0 && ParentIdx < BoneIdx && OutMask[ParentIdx] != 0)
		{
			OutMask[BoneIdx] = 1;
		}
	}
}
}

void FAnimNode_SequencePlayer::Reset()
{
	CurrentTime = 0.0f;
	bPlaying = Sequence != nullptr;
	bHasPendingRootMotion = false;
	PendingNotifies.Reset();
	bNotifyIncludeStart = true;
}

bool FAnimNode_SequencePlayer::IsFinished() const
{
	return Sequence && Sequence->GetDataModel() && !bLoop && !bPlaying &&
		CurrentTime >= Sequence->GetDataModel()->PlayLength;
}

float FAnimNode_SequencePlayer::GetRemainingTime() const
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(PlayRate) || PlayRate <= 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f)
	{
		return std::numeric_limits<float>::infinity();
	}
	return std::max(0.0f, Sequence->GetDataModel()->PlayLength - CurrentTime) / (PlayRate * Sequence->RateScale);
}

void FAnimNode_SequencePlayer::Update(float DeltaSeconds)
{
	// 이전 프레임에 모은 Notify는 소비되었다고 보고 매 Update 시작에 비운다.
	PendingNotifies.Reset();

	if (!bPlaying || !Sequence || !Sequence->GetDataModel() ||
		!std::isfinite(PlayRate) || PlayRate < 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f ||
		!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float Length = Sequence->GetDataModel()->PlayLength;
	if (Length <= 0.0f || !std::isfinite(Length))
	{
		return;
	}

	const float NextTime = CurrentTime + DeltaSeconds * PlayRate * Sequence->RateScale;
	if (!std::isfinite(NextTime))
	{
		return;
	}

	const float PreviousTime = CurrentTime;
	int32 LoopCount = 0;
	if (NextTime < Length)
	{
		CurrentTime = NextTime;
	}
	else if (bLoop)
	{
		//delta time이 더해진 NextTime이 총 길이보다 256배 클 때 종료(오랜 기간 멈춰있어서 deltatime이 클 때..?)
		if (NextTime / Length > 256.0f)
		{
			return;
		}
		LoopCount = static_cast<int32>(NextTime / Length);
		CurrentTime = std::fmod(NextTime, Length);
	}
	else
	{
		CurrentTime = Length;
		bPlaying = false;
	}
	bHasPendingRootMotion = Sequence->ExtractRootMotion(PreviousTime, CurrentTime, LoopCount, PendingRootMotion);
	Sequence->GetNotifies(PreviousTime, CurrentTime, LoopCount, bNotifyIncludeStart, PendingNotifies);
	bNotifyIncludeStart = false;
}

void FAnimNode_SequencePlayer::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	if (PendingNotifies.IsEmpty() || HasGatheredFrom(OutQueue, this))
	{
		return;
	}
	for (const FAnimNotifyEvent& Event : PendingNotifies)
	{
		OutQueue.Add({ Event, ParentWeight, Sequence, this });
	}
}

bool FAnimNode_SequencePlayer::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!Sequence || !Skeleton || Sequence->Skeleton != Skeleton)
	{
		return false;
	}
	return Sequence->EvaluateLocalPoseTransforms(CurrentTime, OutPose);
}

bool FAnimNode_SequencePlayer::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!bHasPendingRootMotion)
	{
		return false;
	}
	OutDelta = PendingRootMotion;
	bHasPendingRootMotion = false;
	return true;
}

float FAnimNode_SequencePlayer::GetCycleSeconds() const
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(PlayRate) || PlayRate <= 0.0f ||
		!std::isfinite(Sequence->RateScale) || Sequence->RateScale <= 0.0f)
	{
		return 0.0f;
	}
	const float Length = Sequence->GetDataModel()->PlayLength;
	return Length > 0.0f && std::isfinite(Length) ? Length / (PlayRate * Sequence->RateScale) : 0.0f;
}

bool FAnimNode_SequencePlayer::GetNormalizedTime(float& OutTime) const
{
	if (!Sequence || !Sequence->GetDataModel() || Sequence->GetDataModel()->PlayLength <= 0.0f)
	{
		return false;
	}
	OutTime = std::clamp(CurrentTime / Sequence->GetDataModel()->PlayLength, 0.0f, 1.0f);
	return true;
}

void FAnimNode_SequencePlayer::SetNormalizedTime(float Time)
{
	if (!Sequence || !Sequence->GetDataModel() || !std::isfinite(Time))
	{
		return;
	}
	CurrentTime = std::clamp(Time, 0.0f, 1.0f) * Sequence->GetDataModel()->PlayLength;
}

void FAnimNode_TwoWayBlend::Reset()
{
	if (A)
	{
		A->Reset();
	}
	if (B)
	{
		B->Reset();
	}
}

void FAnimNode_TwoWayBlend::Update(float DeltaSeconds)
{
	// 가중치가 0인 자식도 시간을 진행한다. 멈춰 두면 다시 섞일 때 멈췄던 위치에서 이어져 동작이 튄다.
	const float WeightB = std::clamp(Alpha, 0.0f, 1.0f);
	FAnimNode_Base* Leader = WeightB >= 0.5f ? B : A;
	FAnimNode_Base* Follower = Leader == A ? B : A;
	if (!Leader)
	{
		std::swap(Leader, Follower);
	}
	if (!Leader)
	{
		return;
	}
	Leader->Update(DeltaSeconds);
	if (!Follower)
	{
		return;
	}
	const float LeaderCycle = Leader->GetCycleSeconds();
	const float FollowerCycle = Follower->GetCycleSeconds();
	float LeaderTime = 0.0f;
	if (bSyncByNormalizedTime && Leader->IsLooping() && Follower->IsLooping() &&
		LeaderCycle > 0.0f && FollowerCycle > 0.0f && Leader->GetNormalizedTime(LeaderTime))
	{
		// 리더가 이번 프레임에 진행한 주기 비율만큼 팔로워도 진행해 루트 모션을 같은 비율로 얻는다.
		// 그 뒤 위상을 리더에 맞춰 누적 오차와 처음 어긋남을 없앤다.
		Follower->Update(DeltaSeconds * FollowerCycle / LeaderCycle);
		Follower->SetNormalizedTime(LeaderTime);
	}
	else
	{
		Follower->Update(DeltaSeconds);
	}
}

bool FAnimNode_TwoWayBlend::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (Alpha <= 0.0f)
	{
		return A && A->Evaluate(Skeleton, OutPose);
	}
	if (Alpha >= 1.0f)
	{
		return B && B->Evaluate(Skeleton, OutPose);
	}
	TArray<FAnimBoneTransform> PoseA, PoseB;
	return A && B && A->Evaluate(Skeleton, PoseA) && B->Evaluate(Skeleton, PoseB) &&
		BlendAnimPoses(PoseA, PoseB, Alpha, OutPose);
}

bool FAnimNode_TwoWayBlend::ConsumeRootMotion(FMatrix& OutDelta)
{
	return BlendRootMotion(A, B, Alpha, OutDelta);
}

void FAnimNode_TwoWayBlend::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	// Evaluate와 같이 한쪽 가중치가 0이면 그쪽은 올리지 않는다. 가중치 0인 자식도 Update는 되므로 Notify는 모여 있다.
	const float WeightB = std::clamp(Alpha, 0.0f, 1.0f);
	if (A && WeightB < 1.0f)
	{
		A->GatherNotifies(ParentWeight * (1.0f - WeightB), OutQueue);
	}
	if (B && WeightB > 0.0f)
	{
		B->GatherNotifies(ParentWeight * WeightB, OutQueue);
	}
}

void FAnimNode_BlendSpacePlayer::Reset()
{
	NormalizedTime = 0.0f;
	bPlaying = BlendSpace != nullptr;
	bHasPendingRootMotion = false;
	PendingNotifies.Reset();
	bNotifyIncludeStart = true;
}

void FAnimNode_BlendSpacePlayer::Update(float DeltaSeconds)
{
	// 이전 프레임에 모은 Notify는 소비되었다고 보고 매 Update 시작에 비운다.
	PendingNotifies.Reset();

	if (!bPlaying || !BlendSpace || !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0f ||
		!std::isfinite(PlayRate) || PlayRate < 0.0f)
	{
		return;
	}
	const float Length = BlendSpace->GetEffectiveLength(InputX, InputY);
	if (Length <= 0.0f || !std::isfinite(Length))
	{
		return;
	}
	const float NextTime = NormalizedTime + DeltaSeconds * PlayRate / Length;
	if (!std::isfinite(NextTime))
	{
		return;
	}
	const float PreviousTime = NormalizedTime;
	int32 LoopCount = 0;
	if (NextTime < 1.0f)
	{
		NormalizedTime = NextTime;
	}
	else if (bLoop)
	{
		if (NextTime > 256.0f)
		{
			return;
		}
		LoopCount = static_cast<int32>(NextTime);
		NormalizedTime = std::fmod(NextTime, 1.0f);
	}
	else
	{
		NormalizedTime = 1.0f;
		bPlaying = false;
	}
	bHasPendingRootMotion = BlendSpace->ExtractRootMotion(InputX, InputY, PreviousTime,
		NormalizedTime, LoopCount, PendingRootMotion);
	BlendSpace->GetNotifies(InputX, InputY, PreviousTime, NormalizedTime, LoopCount, bNotifyIncludeStart, PendingNotifies);
	bNotifyIncludeStart = false;
}

void FAnimNode_BlendSpacePlayer::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	if (PendingNotifies.IsEmpty() || HasGatheredFrom(OutQueue, this))
	{
		return;
	}
	for (const FAnimNotifyQueueItem& Item : PendingNotifies)
	{
		FAnimNotifyQueueItem Gathered = Item;
		Gathered.Weight *= ParentWeight;
		Gathered.SourceNode = this;
		OutQueue.Add(std::move(Gathered));
	}
}

bool FAnimNode_BlendSpacePlayer::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	return BlendSpace && Skeleton && BlendSpace->Skeleton == Skeleton &&
		BlendSpace->Evaluate(InputX, InputY, NormalizedTime, OutPose);
}

float FAnimNode_BlendSpacePlayer::GetCycleSeconds() const
{
	if (!BlendSpace || !std::isfinite(PlayRate) || PlayRate <= 0.0f)
	{
		return 0.0f;
	}
	const float Length = BlendSpace->GetEffectiveLength(InputX, InputY);
	return Length > 0.0f && std::isfinite(Length) ? Length / PlayRate : 0.0f;
}

bool FAnimNode_BlendSpacePlayer::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!bHasPendingRootMotion)
	{
		return false;
	}
	OutDelta = PendingRootMotion;
	bHasPendingRootMotion = false;
	return true;
}

int32 FAnimNode_StateMachine::AddState(const FString& Name, FAnimNode_Base* PoseNode)
{
	if (!PoseNode)
	{
		return -1;
	}
	const int32 Index = States.Num();
	States.Add({ Name, PoseNode });
	if (CurrentState < 0)
	{
		CurrentState = Index;
	}
	return Index;
}

int32 FAnimNode_StateMachine::FindState(const FString& Name) const
{
	for (int32 Index = 0; Index < States.Num(); ++Index)
		if (States[Index].Name == Name) return Index;
	return -1;
}

bool FAnimNode_StateMachine::RequestTransition(int32 ToState, float BlendTime, bool bRestart)
{
	if (!States.IsValidIndex(ToState) || !std::isfinite(BlendTime) || BlendTime < 0.0f) return false;
	if (ToState == GetTargetState() && !bRestart) return true;
	// 섞는 도중이거나 지금 상태를 처음부터 다시 틀면 From 노드가 없다(또는 To와 같은 노드다).
	const bool bNeedSnapshot = NextState >= 0 || ToState == CurrentState;
	if (NextState >= 0) FinishTransition();
	bBlendFromSnapshot = bNeedSnapshot && BlendTime > 0.0f && !LastPose.IsEmpty();
	if (bBlendFromSnapshot) SnapshotPose = LastPose;
	NextState = ToState;
	TransitionDuration = BlendTime;
	TransitionElapsed = 0.0f;
	TransitionOnCompleted = {};
	if (States[ToState].Pose) States[ToState].Pose->Reset();
	if (BlendTime <= 0.0f) FinishTransition();
	return true;
}

bool FAnimNode_StateMachine::AddTransition(int32 FromState, int32 ToState,
	std::function<bool()> Condition, float BlendTime, std::function<void()> OnCompleted,
	std::function<void()> OnStarted)
{
	// FromState가 -1이면 어느 상태에서든 거는 전환(AnyState)이다.
	if ((FromState != -1 && !States.IsValidIndex(FromState)) || !States.IsValidIndex(ToState) ||
		FromState == ToState || !Condition || !std::isfinite(BlendTime) || BlendTime < 0.0f)
	{
		return false;
	}
	Transitions.Add({ FromState, ToState, std::move(Condition), BlendTime,
		std::move(OnCompleted), std::move(OnStarted) });
	return true;
}

void FAnimNode_StateMachine::FinishTransition()
{
	CurrentState = NextState;
	NextState = -1;
	bBlendFromSnapshot = false;
	SnapshotPose.Reset();
	std::function<void()> Completed = std::move(TransitionOnCompleted);
	TransitionOnCompleted = {};
	if (Completed)
	{
		Completed();
	}
}

void FAnimNode_StateMachine::Reset()
{
	CurrentState = States.IsEmpty() ? -1 : 0;
	NextState = -1;
	TransitionElapsed = 0.0f;
	TransitionDuration = 0.0f;
	TransitionOnCompleted = {};
	LastPose.Reset();
	SnapshotPose.Reset();
	bBlendFromSnapshot = false;
	for (const FState& State : States)
	{
		if (State.Pose)
		{
			State.Pose->Reset();
		}
	}
}

void FAnimNode_StateMachine::Update(float DeltaSeconds)
{
	if (!States.IsValidIndex(CurrentState) || !std::isfinite(DeltaSeconds))
	{
		return;
	}
	DeltaSeconds = std::max(0.0f, DeltaSeconds);
	// 스냅샷에서 섞는 중이면 현재 상태 포즈는 쓰이지 않는다. 같은 노드를 다시 트는 경우 두 번 진행하지 않도록 건너뛴다.
	const bool bCurrentUnused = NextState >= 0 && (bBlendFromSnapshot || NextState == CurrentState);
	if (!bCurrentUnused && States[CurrentState].Pose)
	{
		States[CurrentState].Pose->Update(DeltaSeconds);
	}
	const bool bWasTransitioning = NextState >= 0;
	if (bWasTransitioning)
	{
		if (States[NextState].Pose)
		{
			States[NextState].Pose->Update(DeltaSeconds);
		}
		TransitionElapsed += DeltaSeconds;
		if (TransitionElapsed >= TransitionDuration)
		{
			FinishTransition();
		}
	}
	for (const FTransition& Transition : Transitions) // anystate 검사
	{
		if (Transition.FromState != -1 || Transition.ToState == CurrentState ||
			Transition.ToState == NextState || !Transition.Condition())
		{
			continue;
		}
		bBlendFromSnapshot = NextState >= 0 && Transition.BlendTime > 0.0f && !LastPose.IsEmpty();
		SnapshotPose = bBlendFromSnapshot ? LastPose : TArray<FAnimBoneTransform>{};
		NextState = Transition.ToState;
		TransitionDuration = Transition.BlendTime;
		TransitionElapsed = DeltaSeconds;
		TransitionOnCompleted = Transition.OnCompleted;
		if (Transition.OnStarted)
		{
			Transition.OnStarted();
		}
		if (States[NextState].Pose)
		{
			States[NextState].Pose->Reset();
			States[NextState].Pose->Update(DeltaSeconds);
		}
		if (TransitionElapsed >= TransitionDuration)
		{
			FinishTransition();
		}
		return;
	}
	if (bWasTransitioning)
	{
		return;
	}
	for (const FTransition& Transition : Transitions)
	{
		if (Transition.FromState != CurrentState || !Transition.Condition())
		{
			continue;
		}
		NextState = Transition.ToState;
		TransitionDuration = Transition.BlendTime;
		TransitionElapsed = DeltaSeconds;
		TransitionOnCompleted = Transition.OnCompleted;
		if (Transition.OnStarted)
		{
			Transition.OnStarted();
		}
		if (States[NextState].Pose)
		{
			States[NextState].Pose->Reset();
			States[NextState].Pose->Update(DeltaSeconds);
		}
		if (TransitionElapsed >= TransitionDuration)
		{
			FinishTransition();
		}
		break;
	}
}

bool FAnimNode_StateMachine::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!States.IsValidIndex(CurrentState))
	{
		return false;
	}
	FAnimNode_Base* Current = States[CurrentState].Pose;
	bool bEvaluated = false;
	if (NextState < 0)
	{
		bEvaluated = Current && Current->Evaluate(Skeleton, OutPose);
	}
	else
	{
		FAnimNode_Base* Next = States[NextState].Pose;
		const float Alpha = TransitionElapsed / TransitionDuration;
		TArray<FAnimBoneTransform> From, To;
		if (bBlendFromSnapshot)
		{
			bEvaluated = Next && Next->Evaluate(Skeleton, To) && BlendAnimPoses(SnapshotPose, To, Alpha, OutPose);
		}
		else
		{
			bEvaluated = Current && Next && Current->Evaluate(Skeleton, From) && Next->Evaluate(Skeleton, To) &&
				BlendAnimPoses(From, To, Alpha, OutPose);
		}
	}
	if (bEvaluated)
	{
		LastPose = OutPose;
	}
	return bEvaluated;
}

bool FAnimNode_StateMachine::ConsumeRootMotion(FMatrix& OutDelta)
{
	if (!States.IsValidIndex(CurrentState))
	{
		return false;
	}
	FAnimNode_Base* Current = States[CurrentState].Pose;
	if (NextState < 0)
	{
		return Current && Current->ConsumeRootMotion(OutDelta);
	}
	const float Alpha = TransitionDuration > 0.0f ? TransitionElapsed / TransitionDuration : 1.0f;
	// 스냅샷에는 이동량이 없으므로 제자리(Identity)와 섞는다.
	if (bBlendFromSnapshot || NextState == CurrentState)
	{
		return BlendRootMotion(nullptr, States[NextState].Pose, Alpha, OutDelta);
	}
	return BlendRootMotion(Current, States[NextState].Pose, Alpha, OutDelta);
}

void FAnimNode_LayeredBoneBlend::Reset()
{
	if (!bGraphManaged)
	{
		if (BasePose) BasePose->Reset();
		if (BlendPose && BlendPose != BasePose) BlendPose->Reset();
	}
}

void FAnimNode_LayeredBoneBlend::Update(float DeltaSeconds)
{
	if (!bGraphManaged)
	{
		if (BasePose) BasePose->Update(DeltaSeconds);
		if (BlendPose && BlendPose != BasePose) BlendPose->Update(DeltaSeconds);
	}
}

bool FAnimNode_LayeredBoneBlend::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!BasePose || !Skeleton || !BasePose->Evaluate(Skeleton, OutPose)) return false;
	const float Weight = std::clamp(Alpha, 0.0f, 1.0f);
	if (Weight <= 0.0f || !BlendPose || StartBoneName.empty()) return true;
	const int32 StartBoneIndex = Skeleton->RefSkeleton.FindBoneIndex(StartBoneName);
	if (StartBoneIndex == INDEX_NONE) return true;
	TArray<FAnimBoneTransform> BlendPoseOut;
	if (!BlendPose->Evaluate(Skeleton, BlendPoseOut) || BlendPoseOut.Num() != OutPose.Num()) return true;
	TArray<uint8> BranchMask;
	BuildBranchMask(Skeleton->RefSkeleton, StartBoneIndex, BranchMask);
	const int32 NumBones = OutPose.Num();
	TArray<FQuat> BaseCSRot, BlendCSRot, ResultCSRot;
	if (bMeshSpaceRotationBlend)
	{
		BaseCSRot.SetNum(NumBones);
		BlendCSRot.SetNum(NumBones);
		ResultCSRot.SetNum(NumBones);
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
			const FQuat BaseLocal = OutPose[BoneIdx].Rotation.Normalized();
			const FQuat BlendLocal = BlendPoseOut[BoneIdx].Rotation.Normalized();
			BaseCSRot[BoneIdx] = ParentIdx >= 0 && ParentIdx < BoneIdx
				? (BaseCSRot[ParentIdx] * BaseLocal).Normalized() : BaseLocal;
			BlendCSRot[BoneIdx] = ParentIdx >= 0 && ParentIdx < BoneIdx
				? (BlendCSRot[ParentIdx] * BlendLocal).Normalized() : BlendLocal;
		}
	}
	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		if (BranchMask[BoneIdx] == 0)
		{
			if (bMeshSpaceRotationBlend)
				ResultCSRot[BoneIdx] = BaseCSRot[BoneIdx];
			continue;
		}

		OutPose[BoneIdx].Position = FMath::Lerp(OutPose[BoneIdx].Position, BlendPoseOut[BoneIdx].Position, Weight);
		OutPose[BoneIdx].Scale = FMath::Lerp(OutPose[BoneIdx].Scale, BlendPoseOut[BoneIdx].Scale, Weight);
		if (!bMeshSpaceRotationBlend)
		{
			OutPose[BoneIdx].Rotation = FMath::Slerp(OutPose[BoneIdx].Rotation, BlendPoseOut[BoneIdx].Rotation, Weight);
			continue;
		}

		const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
		const FQuat BlendedCS = FQuat::Slerp(BaseCSRot[BoneIdx], BlendCSRot[BoneIdx], Weight).Normalized();
		OutPose[BoneIdx].Rotation = ParentIdx >= 0 && ParentIdx < BoneIdx
			? (ResultCSRot[ParentIdx].UnitInverse() * BlendedCS).Normalized() : BlendedCS;
		ResultCSRot[BoneIdx] = BlendedCS;
	}
	return true;
}

bool FAnimNode_LayeredBoneBlend::ConsumeRootMotion(FMatrix& OutDelta)
{
	return BasePose && BasePose->ConsumeRootMotion(OutDelta);
}

void FAnimNode_Slot::Reset()
{
	if (!bGraphManaged && Source) Source->Reset();
}

void FAnimNode_Slot::Update(float DeltaSeconds)
{
	if (!bGraphManaged && Source) Source->Update(DeltaSeconds);
}

bool FAnimNode_Slot::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!Skeleton) return false;
	bool bHasSource = false;
	if (Source)
	{
		bHasSource = Source->Evaluate(Skeleton, OutPose);
	}
	if (!bHasSource)
	{
		const int32 NumRefBones = Skeleton->RefSkeleton.RefBonePose.Num();
		OutPose.SetNum(NumRefBones);
		for (int32 BoneIdx = 0; BoneIdx < NumRefBones; ++BoneIdx)
		{
			const FBoneTransform& Ref = Skeleton->RefSkeleton.RefBonePose[BoneIdx];
			OutPose[BoneIdx].Position = Ref.Translation;
			OutPose[BoneIdx].Rotation = Ref.Rotation;
			OutPose[BoneIdx].Scale = Ref.Scale;
		}
	}
	if (GraphInstance && !SlotName.empty())
	{
		GraphInstance->EvaluateSlotPose(SlotName, Skeleton, OutPose);
	}
	return !OutPose.IsEmpty();
}

bool FAnimNode_Slot::ConsumeRootMotion(FMatrix& OutDelta)
{
	return Source && Source->ConsumeRootMotion(OutDelta);
}

bool FAnimNode_AimOffset1D::SamplePoseAtPitch(const USkeleton* Skeleton, float SamplePitch, TArray<FAnimBoneTransform>& OutPose) const
{
	if (!Skeleton || Samples.IsEmpty()) return false;
	const float Clamped = std::clamp(SamplePitch, Asset.MinPitch, Asset.MaxPitch);
	if (Samples.Num() == 1 || Clamped <= Samples[0].Pitch)
	{
		return EvaluateSequenceFrame(Samples[0].Sequence, Skeleton, Samples[0].FrameIndex, OutPose);
	}
	const int32 LastIdx = Samples.Num() - 1;
	if (Clamped >= Samples[LastIdx].Pitch)
	{
		return EvaluateSequenceFrame(Samples[LastIdx].Sequence, Skeleton, Samples[LastIdx].FrameIndex, OutPose);
	}
	for (int32 Idx = 0; Idx < LastIdx; ++Idx)
	{
		const FSampleEntry& Low = Samples[Idx];
		const FSampleEntry& High = Samples[Idx + 1];
		if (Clamped >= Low.Pitch && Clamped <= High.Pitch)
		{
			const float Span = High.Pitch - Low.Pitch;
			if (Span <= 1.e-5f)
			{
				return EvaluateSequenceFrame(Low.Sequence, Skeleton, Low.FrameIndex, OutPose);
			}
			TArray<FAnimBoneTransform> PoseLow, PoseHigh;
			if (!EvaluateSequenceFrame(Low.Sequence, Skeleton, Low.FrameIndex, PoseLow) ||
				!EvaluateSequenceFrame(High.Sequence, Skeleton, High.FrameIndex, PoseHigh))
			{
				return false;
			}
			const float BlendWeight = std::clamp((Clamped - Low.Pitch) / Span, 0.0f, 1.0f);
			return BlendAnimPoses(PoseLow, PoseHigh, BlendWeight, OutPose);
		}
	}
	return EvaluateSequenceFrame(Samples[LastIdx].Sequence, Skeleton, Samples[LastIdx].FrameIndex, OutPose);
}

bool FAnimNode_AimOffset1D::EvaluateReferencePose(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) const
{
	if (BaseSequence && EvaluateSequenceFrame(BaseSequence, Skeleton, Asset.BaseFrameIndex, OutPose))
	{
		return true;
	}
	return SamplePoseAtPitch(Skeleton, 0.0f, OutPose);
}

void FAnimNode_AimOffset1D::Reset()
{
	if (!bGraphManaged && BasePose) BasePose->Reset();
}

void FAnimNode_AimOffset1D::Update(float DeltaSeconds)
{
	if (!bGraphManaged && BasePose) BasePose->Update(DeltaSeconds);
}

bool FAnimNode_AimOffset1D::Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose)
{
	if (!BasePose || !Skeleton || !BasePose->Evaluate(Skeleton, OutPose)) return false;
	const float Weight = std::clamp(Alpha, 0.0f, 1.0f);
	if (Weight <= 0.0f || Samples.IsEmpty()) return true;

	TArray<FAnimBoneTransform> RefPose, AimPose;
	if (!EvaluateReferencePose(Skeleton, RefPose) || !SamplePoseAtPitch(Skeleton, Pitch, AimPose))
	{
		return true;
	}
	const int32 NumBones = Skeleton->RefSkeleton.GetNum();
	if (OutPose.Num() != NumBones || RefPose.Num() != NumBones || AimPose.Num() != NumBones)
	{
		return true;
	}

	// 메시 스페이스 기준 회전 차이를 구해 입력 포즈 전체에 가산한다. 본 범위는 Layered Blend 노드가 정한다.
	TArray<FQuat> BaseCSRot;
	TArray<FQuat> RefCSRot;
	TArray<FQuat> AimCSRot;
	TArray<FQuat> NewCSRot;
	BaseCSRot.SetNum(NumBones);
	RefCSRot.SetNum(NumBones);
	AimCSRot.SetNum(NumBones);
	NewCSRot.SetNum(NumBones);

	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
		const FQuat BaseLocal = OutPose[BoneIdx].Rotation.Normalized();
		const FQuat RefLocal = RefPose[BoneIdx].Rotation.Normalized();
		const FQuat AimLocal = AimPose[BoneIdx].Rotation.Normalized();
		if (ParentIdx >= 0 && ParentIdx < BoneIdx)
		{
			BaseCSRot[BoneIdx] = (BaseCSRot[ParentIdx] * BaseLocal).Normalized();
			RefCSRot[BoneIdx] = (RefCSRot[ParentIdx] * RefLocal).Normalized();
			AimCSRot[BoneIdx] = (AimCSRot[ParentIdx] * AimLocal).Normalized();
		}
		else
		{
			BaseCSRot[BoneIdx] = BaseLocal;
			RefCSRot[BoneIdx] = RefLocal;
			AimCSRot[BoneIdx] = AimLocal;
		}

		const FQuat FullDeltaCS = (AimCSRot[BoneIdx] * RefCSRot[BoneIdx].UnitInverse()).Normalized();
		const FQuat WeightedDeltaCS = FQuat::Slerp(FQuat::Identity(), FullDeltaCS, Weight);
		NewCSRot[BoneIdx] = (WeightedDeltaCS * BaseCSRot[BoneIdx]).Normalized();
	}

	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		const int32 ParentIdx = Skeleton->RefSkeleton.RefBoneInfo[BoneIdx].ParentIndex;
		if (ParentIdx >= 0 && ParentIdx < BoneIdx)
		{
			OutPose[BoneIdx].Rotation = (NewCSRot[ParentIdx].UnitInverse() * NewCSRot[BoneIdx]).Normalized();
		}
		else
		{
			OutPose[BoneIdx].Rotation = NewCSRot[BoneIdx];
		}
	}
	return true;
}

bool FAnimNode_AimOffset1D::ConsumeRootMotion(FMatrix& OutDelta)
{
	return BasePose && BasePose->ConsumeRootMotion(OutDelta);
}

void FAnimNode_StateMachine::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	if (!States.IsValidIndex(CurrentState))
	{
		return;
	}
	const FAnimNode_Base* Current = States[CurrentState].Pose;
	if (NextState < 0)
	{
		if (Current)
		{
			Current->GatherNotifies(ParentWeight, OutQueue);
		}
		return;
	}
	const FAnimNode_Base* Next = States[NextState].Pose;
	// Update에서 현재 상태를 진행하지 않는 경우(스냅샷에서 섞는 중, 같은 상태를 처음부터 다시 트는 중)에는 현재 상태의 Notify가 갱신되지 않아 이전 프레임 것이 남아 있다.
	// 이때는 현재 상태를 올리지 않는다. 스냅샷에는 Notify가 없다.
	if (bBlendFromSnapshot || NextState == CurrentState)
	{
		if (Next)
		{
			// 스냅샷에서 섞을 때는 Evaluate와 같은 비율, 같은 노드를 다시 틀 때는 포즈가 그 노드 하나뿐이라 전체 가중치를 쓴다.
			const float Alpha = TransitionDuration > 0.0f ? std::clamp(TransitionElapsed / TransitionDuration, 0.0f, 1.0f) : 1.0f;
			Next->GatherNotifies(bBlendFromSnapshot ? ParentWeight * Alpha : ParentWeight, OutQueue);
		}
		return;
	}
	// 전이 중에는 Evaluate와 같은 비율로 양쪽 상태의 Notify를 올린다.
	// 전이가 이번 Update에서 끝났다면 CurrentState가 이미 새 상태이므로 이전 상태의 마지막 구간 Notify는 올라오지 않는다.
	const float Alpha = TransitionDuration > 0.0f ? std::clamp(TransitionElapsed / TransitionDuration, 0.0f, 1.0f) : 1.0f;
	if (Current)
	{
		Current->GatherNotifies(ParentWeight * (1.0f - Alpha), OutQueue);
	}
	if (Next)
	{
		Next->GatherNotifies(ParentWeight * Alpha, OutQueue);
	}
}

void FAnimNode_LayeredBoneBlend::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	// 기본 포즈는 전신, 블렌드 포즈는 StartBone 아래 가지에만 Alpha만큼 섞인다. 같은 노드를 두 번 올리지 않는다.
	if (BasePose)
	{
		BasePose->GatherNotifies(ParentWeight, OutQueue);
	}
	const float Weight = std::clamp(Alpha, 0.0f, 1.0f);
	if (BlendPose && BlendPose != BasePose && Weight > 0.0f)
	{
		BlendPose->GatherNotifies(ParentWeight * Weight, OutQueue);
	}
}

void FAnimNode_Slot::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	// 슬롯에 재생 중인 몽타주의 Notify는 아직 지원하지 않는다. 슬롯에 들어오는 입력 포즈의 Notify만 올린다.
	if (Source)
	{
		Source->GatherNotifies(ParentWeight, OutQueue);
	}
}

void FAnimNode_AimOffset1D::GatherNotifies(float ParentWeight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	// 에임 샘플은 프레임 하나를 고정으로 읽는 포즈라 재생 시간이 없다. 입력 포즈의 Notify만 올린다.
	if (BasePose)
	{
		BasePose->GatherNotifies(ParentWeight, OutQueue);
	}
}
