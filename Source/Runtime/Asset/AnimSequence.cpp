#include "EnginePCH.h"
#include "Asset/AnimSequence.h"
#include "Animation/AnimNotify.h"
#include "Asset/AnimTransform.h"
#include "Math/EngineMath.h"
#include "Animation/Skeleton.h"
#include "Animation/BoneTransform.h"

#include <algorithm>
#include <cmath>

int32 UAnimSequence::GetFrameIndex(float TimeSeconds) const
{
	if (!DataModel || DataModel->NumberOfKeys <= 0 || DataModel->FrameRate.AsDecimal() <= 0.0f ||
		!std::isfinite(TimeSeconds))
	{
		return -1;
	}
	const double Sample = static_cast<double>(std::max(0.0f, TimeSeconds)) * DataModel->FrameRate.AsDecimal();
	if (Sample >= DataModel->NumberOfKeys - 1)
	{
		return DataModel->NumberOfKeys - 1;
	}
	return static_cast<int32>(Sample);
}

int32 UAnimSequence::GetRootBoneIndex() const
{
	if (!Skeleton || Skeleton->RefSkeleton.GetNum() == 0)
	{
		return -1;
	}
	return Skeleton->RefSkeleton.RefBoneInfo[0].ParentIndex < 0 ? 0 : -1;
}

bool UAnimSequence::SampleRawRoot(float TimeSeconds, FAnimBoneTransform& OutRoot) const
{
	TArray<FAnimBoneTransform> Pose;
	const int32 Root = GetRootBoneIndex();
	if (Root < 0 || !EvaluateLocalPoseTransforms(TimeSeconds, Pose, false) || !Pose.IsValidIndex(Root))
	{
		return false;
	}
	OutRoot = Pose[Root];
	return true;
}

UAnimNotify* UAnimSequence::AddNotify(const FString& EventName, const FString& NotifyClassName, float TriggerTime)
{
	//노티파이 객체 생성해서 추가

	UClass* NotifyClass = FindClass(NotifyClassName);
	if (!NotifyClass || !NotifyClass->IsChildOf(UAnimNotify::StaticClass()) || !NotifyClass->Constructor)
	{
		HTR_LOG(Warning, "[AnimNotify] Cannot add '{}': '{}' is not a concrete UAnimNotify class", EventName, NotifyClassName);
		return nullptr;
	}
	UAnimNotify* Notify = NewObject<UAnimNotify>(this, NotifyClass);
	if (!Notify)
	{
		return nullptr;
	}
	FAnimNotifyEvent Event;
	Event.Name = EventName;
	Event.TriggerTime = DataModel ? std::clamp(TriggerTime, 0.0f, DataModel->PlayLength) : std::max(0.0f, TriggerTime);
	Event.Notify = Notify;
	Notifies.Add(Event);
	return Notify;
}

void UAnimSequence::ClampNotifyStateRange(float& InOutStart, float& InOutDuration) const
{
	const float Length = DataModel ? DataModel->PlayLength : std::max(0.0f, InOutStart + InOutDuration);
	const float MinDuration = std::min(GetMinNotifyStateDuration(), Length);
	InOutDuration = std::isfinite(InOutDuration) ? std::max(InOutDuration, MinDuration) : MinDuration;
	InOutStart = std::isfinite(InOutStart) ? std::clamp(InOutStart, 0.0f, std::max(0.0f, Length - MinDuration)) : 0.0f;
	InOutDuration = std::min(InOutDuration, std::max(MinDuration, Length - InOutStart));
}

UAnimNotifyState* UAnimSequence::AddNotifyState(const FString& EventName, const FString& StateClassName, float StartTime, float Duration)
{
	UClass* StateClass = FindClass(StateClassName);
	if (!StateClass || !StateClass->IsChildOf(UAnimNotifyState::StaticClass()) || !StateClass->Constructor)
	{
		HTR_LOG(Warning, "[AnimNotify] Cannot add '{}': '{}' is not a concrete UAnimNotifyState class", EventName, StateClassName);
		return nullptr;
	}
	UAnimNotifyState* State = NewObject<UAnimNotifyState>(this, StateClass);
	if (!State)
	{
		return nullptr;
	}
	ClampNotifyStateRange(StartTime, Duration);
	FAnimNotifyEvent Event;
	Event.Name = EventName;
	Event.TriggerTime = StartTime;
	Event.Duration = Duration;
	Event.NotifyState = State;
	Notifies.Add(Event);
	return State;
}

void UAnimSequence::ExportNotifies(TArray<FAnimNotifyRecord>& OutRecords) const
{
	OutRecords.Reset();
	for (const FAnimNotifyEvent& Event : Notifies)
	{
		// 구간형과 점 Notify 모두 저장한다. 둘 다 없는 항목만 건너뛴다.
		UObject* Object = Event.IsState() ? static_cast<UObject*>(Event.NotifyState) : static_cast<UObject*>(Event.Notify);
		if (!Object)
		{
			continue;
		}
		FAnimNotifyRecord Record;
		Record.Name = Event.Name;
		Record.TriggerTime = Event.TriggerTime;
		Record.TriggerWeightThreshold = Event.TriggerWeightThreshold;
		Record.Duration = Event.IsState() ? Event.Duration : 0.0f;
		Record.ClassName = Object->GetClass()->Name;

		json Properties = json::object();
		Object->Serialize(Properties, false);
		// 속성 문자열에 잘못된 UTF-8이 있어도 저장이 예외로 끝나지 않게 대체 문자로 바꾼다.
		Record.Properties = Properties.dump(-1, ' ', false, json::error_handler_t::replace);
		OutRecords.Add(std::move(Record));
	}
}

void UAnimSequence::ImportNotifies(const TArray<FAnimNotifyRecord>& Records)
{
	Notifies.Reset();
	for (const FAnimNotifyRecord& Record : Records)
	{
		// 클래스가 UAnimNotifyState이면 구간형, 아니면 점 Notify로 만든다.
		const UClass* RecordClass = FindClass(Record.ClassName);
		UObject* Notify = nullptr;
		if (RecordClass && RecordClass->IsChildOf(UAnimNotifyState::StaticClass()))
		{
			Notify = AddNotifyState(Record.Name, Record.ClassName, Record.TriggerTime, Record.Duration);
		}
		else
		{
			Notify = AddNotify(Record.Name, Record.ClassName, Record.TriggerTime);
		}
		if (!Notify)
		{
			continue;
		}
		Notifies.Last().TriggerWeightThreshold = Record.TriggerWeightThreshold;

		if (Record.Properties.empty())
		{
			continue;
		}
		// 속성 하나가 형식이 맞지 않으면 json이 예외를 던진다. 그 Notify는 기본값으로 둔다.
		try
		{
			json Properties = json::parse(Record.Properties);
			Notify->Serialize(Properties, true);
		}
		catch (const std::exception& Exception)
		{
			HTR_LOG(Warning, "[AnimNotify] Cannot read properties of '{}' ({}): {}", Record.Name, Record.ClassName, Exception.what());
		}
	}
	if (!Records.IsEmpty())
	{
		HTR_LOG(Info, "[AnimNotify] loaded {} of {} notifies from file", Notifies.Num(), Records.Num());
	}
}

void UAnimSequence::GetNotifyQueueItems(float PreviousTime, float CurrentTime, int32 LoopCount, bool bIncludeStart,
	float Weight, TArray<FAnimNotifyQueueItem>& OutQueue) const
{
	TArray<FAnimNotifyEvent> Events;
	GetNotifies(PreviousTime, CurrentTime, LoopCount, bIncludeStart, Events);
	for (const FAnimNotifyEvent& Event : Events)
	{
		OutQueue.Add({ Event, Weight, this });
	}
}

void UAnimSequence::GetNotifies(float PreviousTime, float CurrentTime, int32 LoopCount, bool bIncludeStart, TArray<FAnimNotifyEvent>& OutNotifies) const
{
	if (!DataModel || Notifies.IsEmpty() || LoopCount < 0 || !std::isfinite(PreviousTime) || !std::isfinite(CurrentTime))
	{
		return;
	}
	const float Length = DataModel->PlayLength;
	if (Length <= 0.0f)
	{
		return;
	}

	// 시작 제외, 끝 포함
	// 0초에서부터 시작할 때는 시작 포함, 끝 포함으로
	// [Start, End] 또는 (Start, End] 구간의 Notify를 시간순으로 추가한다.
	const auto CollectSegment = [&](float Start, float End, bool bInclusiveStart)
	{
		const int32 FirstAdded = OutNotifies.Num();
		for (const FAnimNotifyEvent& Event : Notifies)
		{
			if (Event.IsState())
			{
				// 구간형: 구간 [TriggerTime, TriggerTime + Duration]이 이번 구간과 겹치면 올린다.
				// 이미 끝난 구간(끝이 Start 이하)은 올리지 않는다. 끝이 이번 구간 안이면 bStateEnds를 켠다.
				const float StateEnd = Event.TriggerTime + Event.Duration;
				if (StateEnd > Start && Event.TriggerTime <= End)
				{
					OutNotifies.Add(Event);
					OutNotifies.Last().bStateEnds = StateEnd <= End;
				}
				continue;
			}
			const bool bAfterStart = bInclusiveStart ? Event.TriggerTime >= Start : Event.TriggerTime > Start;
			if (bAfterStart && Event.TriggerTime <= End)
			{
				OutNotifies.Add(Event);
			}
		}
		std::sort(OutNotifies.begin() + FirstAdded, OutNotifies.end(),
			[](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B)
			{
				return A.TriggerTime < B.TriggerTime;
			});
	};

	if (LoopCount == 0)
	{
		CollectSegment(PreviousTime, CurrentTime, bIncludeStart);
		return;
	}
	
	//시작부터 Lenght까지
	CollectSegment(PreviousTime, Length, bIncludeStart);

	//드물지만 LoopCount가 2 이상이라면 전체 순회를 포함해야 한다.
	//이 프레임에서 클립이 한바퀴 통째로 재생된거라서 그 안의 모든 노티파이가 한번씩 트리거 되어야 한다.
	for (int32 Index = 1; Index < LoopCount; ++Index)
	{
		CollectSegment(0.0f, Length, true);
	}

	//0부터 current까지. 0초부터 시작하니 includestart는 true로.
	CollectSegment(0.0f, CurrentTime, true);
}

bool UAnimSequence::ExtractRootMotion(float PreviousTime, float CurrentTime, int32 LoopCount, FMatrix& OutDelta) const
{
	OutDelta = FMatrix::Identity;
	if (!bEnableRootMotion || !DataModel || GetRootBoneIndex() < 0 ||
		!Skeleton->RefSkeleton.RefBonePose.IsValidIndex(0) || LoopCount < 0 || LoopCount > 256)
	{
		return false;
	}
	const float Length = DataModel->PlayLength;
	if (Length <= 0.0f || !std::isfinite(PreviousTime) || !std::isfinite(CurrentTime))
	{
		return false;
	}
	FAnimBoneTransform Lock;
	switch (RootMotionRootLock)
	{
	case ERootMotionRootLock::RefPose:
	{
		const FBoneTransform& Ref = Skeleton->RefSkeleton.RefBonePose[0];
		Lock = { Ref.Translation, Ref.Rotation, Ref.Scale };
		break;
	}
	case ERootMotionRootLock::AnimFirstFrame:
		if (!SampleRawRoot(0.0f, Lock))
		{
			return false;
		}
		break;
	case ERootMotionRootLock::Zero:
		break;
	}
	const FMatrix LockMatrix = MakeAnimMatrix(Lock.Position, Lock.Rotation, Lock.Scale);
	const auto Segment = [&](float Start, float End, FMatrix& Delta)
	{
		FAnimBoneTransform A, B;
		if (!SampleRawRoot(Start, A) || !SampleRawRoot(End, B))
		{
			return false;
		}
		const FMatrix RawA = MakeAnimMatrix(A.Position, A.Rotation, A.Scale);
		const FMatrix RawB = MakeAnimMatrix(B.Position, B.Rotation, B.Scale);
		Delta = LockMatrix.Inverse() * RawB * RawA.Inverse() * LockMatrix;
		return true;
	};
	if (LoopCount == 0)
	{
		if (!Segment(PreviousTime, CurrentTime, OutDelta))
		{
			return false;
		}
	}
	else
	{
		FMatrix First, Cycle, Last;
		if (!Segment(PreviousTime, Length, First) || !Segment(0.0f, Length, Cycle) ||
			!Segment(0.0f, CurrentTime, Last))
		{
			return false;
		}
		OutDelta = First;
		for (int32 Index = 1; Index < LoopCount; ++Index)
		{
			OutDelta = Cycle * OutDelta;
		}
		OutDelta = Last * OutDelta;
	}
	if (bUseNormalizedRootMotionScale)
	{
		FBoneTransform Normalized = FBoneTransform::FromMatrix(OutDelta);
		Normalized.Scale = FVector::OneVector;
		OutDelta = Normalized.ToMatrix();
	}
	return true;
}

bool UAnimSequence::EvaluateLocalPoseTransforms(float TimeSeconds, TArray<FAnimBoneTransform>& OutPose, bool bApplyRootLock) const
{
	if (!Skeleton || !DataModel || DataModel->NumberOfKeys <= 0 || DataModel->FrameRate.AsDecimal() <= 0.0f ||
		DataModel->BoneAnimationTracks.Num() != Skeleton->RefSkeleton.GetNum() || !std::isfinite(TimeSeconds))
	{
		return false;
	}
	const float Sample = std::clamp(TimeSeconds * DataModel->FrameRate.AsDecimal(),
		0.0f, static_cast<float>(DataModel->NumberOfKeys - 1));
	const int32 First = static_cast<int32>(Sample);
	const int32 Second = std::min(First + 1, DataModel->NumberOfKeys - 1);
	const float Alpha = Sample - First;
	const int32 NumBones = Skeleton->RefSkeleton.GetNum();
	OutPose.SetNum(NumBones);

	//모든 본에 대해 보간
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		//본 Track 가져오기
		const FBoneAnimationTrack& BoneTrack = DataModel->BoneAnimationTracks[BoneIndex];
		if (BoneTrack.Name != FName(Skeleton->RefSkeleton.RefBoneInfo[BoneIndex].Name))
		{
			return false;
		}
		const FRawAnimSequenceTrack& Track = BoneTrack.InternalTrack;
		if ((Track.PosKeys.Num() != 1 && Track.PosKeys.Num() != DataModel->NumberOfKeys) ||
			(Track.RotKeys.Num() != 1 && Track.RotKeys.Num() != DataModel->NumberOfKeys) ||
			(Track.ScaleKeys.Num() != 1 && Track.ScaleKeys.Num() != DataModel->NumberOfKeys))
		{
			return false;
		}
		//보간
		const FVector3f Position = FMath::Lerp(Track.PosKeys[Track.PosKeys.Num() == 1 ? 0 : First],
			Track.PosKeys[Track.PosKeys.Num() == 1 ? 0 : Second], Alpha);
		const FQuat4f Rotation = FMath::Slerp(Track.RotKeys[Track.RotKeys.Num() == 1 ? 0 : First],
			Track.RotKeys[Track.RotKeys.Num() == 1 ? 0 : Second], Alpha);
		const FVector3f Scale = FMath::Lerp(Track.ScaleKeys[Track.ScaleKeys.Num() == 1 ? 0 : First],
			Track.ScaleKeys[Track.ScaleKeys.Num() == 1 ? 0 : Second], Alpha);
		OutPose[BoneIndex] = { Position, Rotation, Scale };
	}

	const int32 Root = GetRootBoneIndex();
	if (bApplyRootLock && Root >= 0 && (bEnableRootMotion || bForceRootLock))
	{
		switch (RootMotionRootLock)
		{
		case ERootMotionRootLock::RefPose:
			if (Skeleton->RefSkeleton.RefBonePose.IsValidIndex(Root))
			{
				//루트를 Ref 포즈로 고정
				const FBoneTransform& Ref = Skeleton->RefSkeleton.RefBonePose[Root];
				OutPose[Root] = { Ref.Translation, Ref.Rotation, Ref.Scale };
			}
			break;
		case ERootMotionRootLock::AnimFirstFrame:
		{
			//애니메이션 첫 프레임으로 고정
			const FRawAnimSequenceTrack& Track = DataModel->BoneAnimationTracks[Root].InternalTrack;
			OutPose[Root] = { Track.PosKeys[0], Track.RotKeys[0], Track.ScaleKeys[0] };
			break;
		}
		case ERootMotionRootLock::Zero:
			//0,0,0과 같이 초기화로 고정
			OutPose[Root] = FAnimBoneTransform{};
			break;
		}
	}
	return true;
}

bool UAnimSequence::EvaluateLocalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const
{
	TArray<FAnimBoneTransform> Transforms;
	if (!EvaluateLocalPoseTransforms(TimeSeconds, Transforms))
	{
		return false;
	}
	OutPose.SetNum(Transforms.Num());
	for (int32 BoneIndex = 0; BoneIndex < Transforms.Num(); ++BoneIndex)
	{
		const FAnimBoneTransform& Bone = Transforms[BoneIndex];
		OutPose[BoneIndex] = MakeAnimMatrix(Bone.Position, Bone.Rotation, Bone.Scale);
	}
	return true;
}

bool UAnimSequence::EvaluateGlobalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const
{
	if (!EvaluateLocalPose(TimeSeconds, OutPose))
	{
		return false;
	}
	for (int32 BoneIndex = 0; BoneIndex < OutPose.Num(); ++BoneIndex)
	{
		const int32 Parent = Skeleton->RefSkeleton.RefBoneInfo[BoneIndex].ParentIndex;
		if (Parent >= 0)
		{
			OutPose[BoneIndex] = OutPose[BoneIndex] * OutPose[Parent];
		}
	}
	return true;
}
