#include "EnginePCH.h"
#include "Animation/AnimationRuntime.h"
#include "AnimSequenceBase.h"
#include "AnimNotify.h"
#include "AnimNotifyState.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/BinaryArchiveFormatter.h"
#include "UObject/Class.h"
#include "UObject/Property.h"
#include "UObject/UObjectHash.h"
#include <algorithm>

UAnimSequenceBase::~UAnimSequenceBase()
{
    // 이 시퀀스를 Outer로 만든 Notify 객체들을 함께 해제한다
    for (FAnimNotifyEvent& Event : AnimNotifies)
    {
        delete Event.Notify;
        delete Event.NotifyStateClass;
    }
}

void UAnimSequenceBase::OnNotifyObjectDestroyed(const UObject* NotifyObject)
{
    for (FAnimNotifyEvent& Event : AnimNotifies)
    {
        if (Event.Notify == NotifyObject) Event.Notify = nullptr;
        if (Event.NotifyStateClass == NotifyObject) Event.NotifyStateClass = nullptr;
    }
}

const FAnimNotifyEvent* UAnimSequenceBase::FindNotify(const FGuid& NotifyGuid) const
{
    const int32 Index = FindNotifyIndex(NotifyGuid);
    return Index != -1 ? &AnimNotifies[Index] : nullptr;
}

FGuid UAnimSequenceBase::AddNotify(float Time, const FName& NotifyName, float Duration)
{
    FAnimNotifyEvent NewEvent;
    NewEvent.NotifyName = NotifyName;
    return AddNotifyEvent(Time, Duration, NewEvent);
}

UObject* UAnimSequenceBase::AddNotifyOfClass(UClass* NotifyClass, float Time, float Duration, FGuid* OutGuid)
{
    if (!NotifyClass)
    {
        return nullptr;
    }

    const bool bIsState = NotifyClass->IsChildOf(UAnimNotifyState::StaticClass());
    if (!bIsState && !NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
    {
        return nullptr;
    }

    UObject* NotifyObject = NewObject<UObject>(this, NotifyClass);
    if (!NotifyObject)
    {
        return nullptr;
    }

    FAnimNotifyEvent NewEvent;
    if (bIsState)
    {
        NewEvent.NotifyStateClass = static_cast<UAnimNotifyState*>(NotifyObject);
    }
    else
    {
        NewEvent.Notify = static_cast<UAnimNotify*>(NotifyObject);
        Duration = 0.f;   // 단발 Notify에 Duration을 주면 구간 동안 매 프레임 불리므로 막는다
    }

    const FGuid NewGuid = AddNotifyEvent(Time, Duration, NewEvent);
    if (OutGuid)
    {
        *OutGuid = NewGuid;
    }
    return NotifyObject;
}

FGuid UAnimSequenceBase::AddNotifyEvent(float Time, float Duration, FAnimNotifyEvent& NewEvent)
{
    NewEvent.TriggerTime = ClampNotifyTime(Time);
    NewEvent.Duration = ClampNotifyDuration(NewEvent.TriggerTime, Duration);
    NewEvent.Guid = FGuid::NewGuid();

    NewEvent.TrackIndex = FMath::Max(NewEvent.TrackIndex, 0);

    const FGuid NewGuid = NewEvent.Guid;
    AnimNotifies.Add(NewEvent);
    SortNotifies();
    EnsureNotifyTracks();
    return NewGuid;
}

bool UAnimSequenceBase::SetNotifyTriggerTime(const FGuid& NotifyGuid, float Time)
{
    const int32 Index = FindNotifyIndex(NotifyGuid);
    if (Index == -1)
    {
        return false;
    }

    FAnimNotifyEvent& Event = AnimNotifies[Index];
    Event.TriggerTime = ClampNotifyTime(Time);
    // 뒤로 밀면 끝이 시퀀스 밖으로 나갈 수 있으므로 Duration도 다시 자른다
    Event.Duration = ClampNotifyDuration(Event.TriggerTime, Event.Duration);
    SortNotifies();
    return true;
}

bool UAnimSequenceBase::SetNotifyDuration(const FGuid& NotifyGuid, float Duration)
{
    const int32 Index = FindNotifyIndex(NotifyGuid);
    if (Index == -1)
    {
        return false;
    }

    FAnimNotifyEvent& Event = AnimNotifies[Index];
    // 단발 Notify에 길이를 주면 구간 동안 매 프레임 발동하므로 NotifyState만 길이를 가진다
    Event.Duration = Event.NotifyStateClass ? ClampNotifyDuration(Event.TriggerTime, Duration) : 0.f;
    return true;
}

bool UAnimSequenceBase::SetNotifyTriggerWeightThreshold(const FGuid& NotifyGuid, float Threshold)
{
    const int32 Index = FindNotifyIndex(NotifyGuid);
    if (Index == -1)
    {
        return false;
    }

    AnimNotifies[Index].TriggerWeightThreshold = FMath::Clamp(Threshold, 0.f, 1.f);
    return true;
}

bool UAnimSequenceBase::SetNotifyTrackIndex(const FGuid& NotifyGuid, int32 TrackIndex)
{
    const int32 Index = FindNotifyIndex(NotifyGuid);
    if (Index == -1)
    {
        return false;
    }

    AnimNotifies[Index].TrackIndex = FMath::Clamp(TrackIndex, 0, GetNumNotifyTracks() - 1);
    return true;
}

// ---------------- Notify 트랙 ----------------

void UAnimSequenceBase::EnsureNotifyTracks()
{
    int32 Required = 1;
    for (const FAnimNotifyEvent& Event : AnimNotifies)
        Required = FMath::Max(Required, Event.TrackIndex + 1);
    while (NotifyTracks.Num() < Required)
        NotifyTracks.Add(FAnimNotifyTrack{});
}

FString UAnimSequenceBase::GetNotifyTrackName(int32 TrackIndex) const
{
    if (TrackIndex >= 0 && TrackIndex < NotifyTracks.Num() && !NotifyTracks[TrackIndex].Name.empty())
        return NotifyTracks[TrackIndex].Name;
    return std::to_string(TrackIndex + 1);
}

int32 UAnimSequenceBase::AddNotifyTrack(const FString& Name)
{
    return InsertNotifyTrack(GetNumNotifyTracks(), Name);
}

int32 UAnimSequenceBase::InsertNotifyTrack(int32 TrackIndex, const FString& Name)
{
    EnsureNotifyTracks();   // 비어 있던(암묵적 1개) 목록을 실제 원소로 만든다
    TrackIndex = FMath::Clamp(TrackIndex, 0, NotifyTracks.Num());

    for (FAnimNotifyEvent& Event : AnimNotifies)
        if (Event.TrackIndex >= TrackIndex)
            ++Event.TrackIndex;

    NotifyTracks.Insert(FAnimNotifyTrack{ Name }, static_cast<uint32>(TrackIndex));
    return TrackIndex;
}

bool UAnimSequenceBase::RemoveNotifyTrack(int32 TrackIndex)
{
    EnsureNotifyTracks();
    if (NotifyTracks.Num() <= 1 || TrackIndex < 0 || TrackIndex >= NotifyTracks.Num())
    {
        return false;
    }

    // 이 트랙의 Notify를 지우고, 아래 트랙들을 한 칸씩 올린다
    for (int32 Index = AnimNotifies.Num() - 1; Index >= 0; --Index)
    {
        if (AnimNotifies[Index].TrackIndex == TrackIndex)
            RemoveNotifyAt(Index);
    }
    for (FAnimNotifyEvent& Event : AnimNotifies)
        if (Event.TrackIndex > TrackIndex)
            --Event.TrackIndex;

    NotifyTracks.RemoveAt(static_cast<uint32>(TrackIndex));
    return true;
}

bool UAnimSequenceBase::RenameNotifyTrack(int32 TrackIndex, const FString& Name)
{
    EnsureNotifyTracks();
    if (TrackIndex < 0 || TrackIndex >= NotifyTracks.Num())
    {
        return false;
    }

    NotifyTracks[TrackIndex].Name = Name;
    return true;
}

bool UAnimSequenceBase::SetNotifyName(const FGuid& NotifyGuid, const FName& NotifyName)
{
    const int32 Index = FindNotifyIndex(NotifyGuid);
    if (Index == -1)
    {
        return false;
    }

    AnimNotifies[Index].NotifyName = NotifyName;
    return true;
}

// ---------------- .anim.bin v3 Notify 섹션 ----------------
//
// int32 TrackCount
// 트랙마다: String Name
// int32 NotifyCount
// Notify마다:
//   float TriggerTime, Duration, TriggerWeightThreshold
//   int32 TrackIndex
//   uint32 Guid.A, B, C, D
//   String NotifyName
//   uint8 Kind (0 = 이름, 1 = UAnimNotify, 2 = UAnimNotifyState)
//   Kind != 0이면:
//     String ClassName
//     int32 PropertyCount
//     프로퍼티마다: String Name, String TypeName, int32 BlobSize, BlobSize 바이트
//
// 프로퍼티는 이름 + 타입 + 바이트 묶음으로 저장한다. 클래스에 프로퍼티가 추가/삭제/재정렬돼도
// 이름으로 찾아 읽고, 없거나 타입이 바뀐 것은 건너뛴다 (BlobSize만큼 넘긴다).

namespace
{
    enum class ENotifyKind : uint8 { Named = 0, Notify = 1, NotifyState = 2 };

    bool SerializeString(FArchive& Ar, FString& Value)
    {
        int32 Size = static_cast<int32>(Value.size());
        Ar << Size;
        if (Ar.IsLoading())
        {
            if (Ar.IsError() || Size < 0 || Size > Ar.TotalSize() - Ar.Tell())
            {
                Ar.SetError();
                return false;
            }
            Value.resize(Size);
        }
        if (Size > 0) Ar.Serialize(Value.data(), Size);
        return !Ar.IsError();
    }

    // 프로퍼티 하나의 값을 바이트 묶음으로 (구조화 아카이브의 바이너리 포맷 = 각 FProperty::SerializeItem)
    bool WritePropertyBlob(const FProperty& Property, UObject* Object, TArray<uint8>& OutBlob)
    {
        FMemoryWriter Writer(OutBlob);
        Writer.SetIsPersistent(true);
        {
            FBinaryArchiveFormatter Formatter(Writer);
            FStructuredArchive Archive(Formatter);
            Property.SerializeItem(Archive.Open(), Property.ContainerPtrToValuePtr<void>(Object));
        }
        return !Writer.IsError();
    }

    bool ReadPropertyBlob(const FProperty& Property, UObject* Object, const TArray<uint8>& Blob)
    {
        // 타입 이름이 같을 때만 불리므로 값 자리에 바로 읽는다 (FString처럼 생성된 객체가 필요한 타입이 있다)
        FMemoryReader Reader(Blob, true);
        {
            FBinaryArchiveFormatter Formatter(Reader);
            FStructuredArchive Archive(Formatter);
            Property.SerializeItem(Archive.Open(), Property.ContainerPtrToValuePtr<void>(Object));
        }
        return !Reader.IsError() && Reader.Tell() == Reader.TotalSize();
    }

    bool IsSavableProperty(const FProperty& Property)
    {
        // Transient는 파일에 남기지 않는다. 객체 참조는 파일에 저장할 방법이 아직 없다.
        return !Property.HasAnyFlags(CPF_Transient) && !CastField<FObjectProperty>(&Property);
    }
}

void UAnimSequenceBase::SerializeNotifyEvents(FArchive& Ar, TArray<FAnimNotifyTrack>& OutTracks, TArray<FAnimNotifyEvent>& OutLoaded)
{
    const bool bLoading = Ar.IsLoading();

    // 트랙 목록 (빈 트랙도 남도록 Notify와 따로 저장한다)
    int32 TrackCount = bLoading ? 0 : NotifyTracks.Num();
    Ar << TrackCount;
    if (Ar.IsError() || TrackCount < 0
        || (bLoading && TrackCount > (Ar.TotalSize() - Ar.Tell()) / static_cast<int64>(sizeof(int32))))
    {
        Ar.SetError();
        return;
    }
    for (int32 TrackIndex = 0; TrackIndex < TrackCount; ++TrackIndex)
    {
        FString TrackName = bLoading ? FString{} : NotifyTracks[TrackIndex].Name;
        if (!SerializeString(Ar, TrackName)) return;
        if (bLoading) OutTracks.Add(FAnimNotifyTrack{ TrackName });
    }

    int32 NotifyCount = bLoading ? 0 : AnimNotifies.Num();
    Ar << NotifyCount;

    // Notify 하나는 최소 고정 필드(float 3 + int32 + Guid 16 + 문자열 길이 + Kind)를 가진다
    constexpr int64 MinBytesPerNotify = 3 * sizeof(float) + sizeof(int32) + 4 * sizeof(uint32) + sizeof(int32) + sizeof(uint8);
    if (Ar.IsError() || NotifyCount < 0
        || (bLoading && NotifyCount > (Ar.TotalSize() - Ar.Tell()) / MinBytesPerNotify))
    {
        Ar.SetError();
        return;
    }

    for (int32 Index = 0; Index < NotifyCount && !Ar.IsError(); ++Index)
    {
        FAnimNotifyEvent Event = bLoading ? FAnimNotifyEvent{} : AnimNotifies[Index];

        FString NotifyName = bLoading ? FString{} : Event.NotifyName.ToString();
        if (!bLoading && Event.NotifyName.IsNone()) NotifyName.clear();

        UObject* NotifyObject = bLoading ? nullptr
            : (Event.NotifyStateClass ? static_cast<UObject*>(Event.NotifyStateClass) : static_cast<UObject*>(Event.Notify));
        uint8 Kind = static_cast<uint8>(Event.NotifyStateClass ? ENotifyKind::NotifyState
            : Event.Notify ? ENotifyKind::Notify : ENotifyKind::Named);

        Ar << Event.TriggerTime << Event.Duration << Event.TriggerWeightThreshold << Event.TrackIndex;
        Ar << Event.Guid.A << Event.Guid.B << Event.Guid.C << Event.Guid.D;
        if (!SerializeString(Ar, NotifyName)) return;
        Ar << Kind;
        if (Ar.IsError() || Kind > static_cast<uint8>(ENotifyKind::NotifyState))
        {
            Ar.SetError();
            return;
        }

        const ENotifyKind NotifyKind = static_cast<ENotifyKind>(Kind);
        if (NotifyKind != ENotifyKind::Named)
        {
            FString ClassName = bLoading ? FString{} : NotifyObject->GetClass()->Name;
            if (!SerializeString(Ar, ClassName)) return;

            if (bLoading)
            {
                UClass* Class = FindClass(ClassName);
                const UClass* Base = NotifyKind == ENotifyKind::NotifyState
                    ? UAnimNotifyState::StaticClass() : UAnimNotify::StaticClass();
                if (Class && Class->IsChildOf(Base))
                {
                    NotifyObject = NewObject<UObject>(this, Class);
                }
                else
                {
                    // 클래스가 지워졌거나 이름이 바뀌었다. 프로퍼티는 읽고 버린다.
                    HTR_LOG(Warning, "AnimSequence notify class not found, dropped: {}", ClassName);
                }
            }

            TArray<const FProperty*> Properties;
            if (!bLoading)
            {
                for (const UStruct* Struct = NotifyObject->GetClass(); Struct; Struct = Struct->GetSuperStruct())
                    for (const FProperty* Property : Struct->GetProperties())
                        if (IsSavableProperty(*Property))
                            Properties.Add(Property);
            }

            int32 PropertyCount = Properties.Num();
            Ar << PropertyCount;
            if (Ar.IsError() || PropertyCount < 0)
            {
                Ar.SetError();
                delete NotifyObject;
                return;
            }

            for (int32 PropIndex = 0; PropIndex < PropertyCount && !Ar.IsError(); ++PropIndex)
            {
                FString PropName, TypeName;
                TArray<uint8> Blob;
                if (!bLoading)
                {
                    const FProperty* Property = Properties[PropIndex];
                    PropName = Property->Name;
                    TypeName = Property->GetClass()->GetName();
                    if (!WritePropertyBlob(*Property, NotifyObject, Blob))
                    {
                        Ar.SetError();
                        return;
                    }
                }

                int32 BlobSize = Blob.Num();
                SerializeString(Ar, PropName);
                SerializeString(Ar, TypeName);
                Ar << BlobSize;
                if (Ar.IsError() || BlobSize < 0 || (bLoading && BlobSize > Ar.TotalSize() - Ar.Tell()))
                {
                    Ar.SetError();
                    break;
                }
                if (bLoading) Blob.SetNum(BlobSize);
                if (BlobSize > 0) Ar.Serialize(Blob.GetData(), BlobSize);

                if (bLoading && NotifyObject && !Ar.IsError())
                {
                    const FProperty* Property = NotifyObject->GetClass()->FindPropertyByName(PropName);
                    if (!Property || TypeName != Property->GetClass()->GetName() || !ReadPropertyBlob(*Property, NotifyObject, Blob))
                        HTR_LOG(Warning, "AnimSequence notify property skipped: {}.{}", ClassName, PropName);
                }
            }

            if (Ar.IsError())
            {
                if (bLoading) delete NotifyObject;
                return;
            }
        }

        if (!bLoading)
            continue;

        if (NotifyKind != ENotifyKind::Named && !NotifyObject)
            continue;   // 클래스를 못 찾아 버린 Notify

        Event.NotifyName = NotifyName.empty() ? FName() : FName(NotifyName);
        Event.Notify = NotifyKind == ENotifyKind::Notify ? static_cast<UAnimNotify*>(NotifyObject) : nullptr;
        Event.NotifyStateClass = NotifyKind == ENotifyKind::NotifyState ? static_cast<UAnimNotifyState*>(NotifyObject) : nullptr;
        if (!Event.Guid.IsValid()) Event.Guid = FGuid::NewGuid();
        OutLoaded.Add(Event);
    }
}

bool UAnimSequenceBase::CaptureNotifySnapshot(TArray<uint8>& OutBytes)
{
    OutBytes.Reset();
    FMemoryWriter Writer(OutBytes);
    Writer.SetIsPersistent(true);
    TArray<FAnimNotifyTrack> UnusedTracks;
    TArray<FAnimNotifyEvent> UnusedEvents;
    SerializeNotifyEvents(Writer, UnusedTracks, UnusedEvents);
    return !Writer.IsError();
}

bool UAnimSequenceBase::RestoreNotifySnapshot(const TArray<uint8>& Bytes)
{
    FMemoryReader Reader(Bytes, true);
    TArray<FAnimNotifyTrack> Tracks;
    TArray<FAnimNotifyEvent> Events;
    SerializeNotifyEvents(Reader, Tracks, Events);
    if (Reader.IsError() || Reader.Tell() != Reader.TotalSize())
    {
        DeleteNotifyObjects(Events);
        return false;
    }

    ReplaceNotifies(std::move(Tracks), std::move(Events));
    return true;
}

void UAnimSequenceBase::ReplaceNotifies(TArray<FAnimNotifyTrack>&& Tracks, TArray<FAnimNotifyEvent>&& Events)
{
    RemoveAllNotifies();
    NotifyTracks = std::move(Tracks);
    AnimNotifies = std::move(Events);
    for (FAnimNotifyEvent& Event : AnimNotifies)
    {
        Event.TriggerTime = ClampNotifyTime(Event.TriggerTime);
        Event.Duration = Event.NotifyStateClass ? ClampNotifyDuration(Event.TriggerTime, Event.Duration) : 0.f;
        Event.TriggerWeightThreshold = FMath::Clamp(Event.TriggerWeightThreshold, 0.f, 1.f);
        Event.TrackIndex = FMath::Max(Event.TrackIndex, 0);
    }
    SortNotifies();
    EnsureNotifyTracks();   // 파일의 TrackIndex가 트랙 수보다 크면 트랙을 늘린다
}

void UAnimSequenceBase::DeleteNotifyObjects(TArray<FAnimNotifyEvent>& Events)
{
    for (FAnimNotifyEvent& Event : Events)
    {
        delete Event.Notify;
        delete Event.NotifyStateClass;
    }
    Events.Empty();
}

bool UAnimSequenceBase::RemoveNotify(const FGuid& NotifyGuid)
{
    const int32 Index = FindNotifyIndex(NotifyGuid);
    if (Index == -1)
    {
        return false;
    }

    RemoveNotifyAt(Index);
    return true;
}

int32 UAnimSequenceBase::RemoveNotifies(const TArray<FGuid>& NotifyGuids)
{
    int32 NumRemoved = 0;
    // 뒤에서부터 지워야 아직 검사하지 않은 원소의 Index가 밀리지 않는다 (UE RemoveNotifies와 같은 방식)
    for (int32 Index = AnimNotifies.Num() - 1; Index >= 0; --Index)
    {
        if (NotifyGuids.Find(AnimNotifies[Index].Guid) != -1)
        {
            RemoveNotifyAt(Index);
            ++NumRemoved;
        }
    }
    return NumRemoved;
}

int32 UAnimSequenceBase::RemoveNotifiesByName(const FName& NotifyName)
{
    int32 NumRemoved = 0;
    for (int32 Index = AnimNotifies.Num() - 1; Index >= 0; --Index)
    {
        if (AnimNotifies[Index].NotifyName == NotifyName)
        {
            RemoveNotifyAt(Index);
            ++NumRemoved;
        }
    }
    return NumRemoved;
}

void UAnimSequenceBase::RemoveAllNotifies()
{
    for (int32 Index = AnimNotifies.Num() - 1; Index >= 0; --Index)
    {
        RemoveNotifyAt(Index);
    }
}

void UAnimSequenceBase::SortNotifies()
{
    // stable_sort: 같은 시간의 Notify는 추가한 순서를 유지한다
    std::stable_sort(AnimNotifies.begin(), AnimNotifies.end(),
        [](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.TriggerTime < B.TriggerTime; });
}

float UAnimSequenceBase::ClampNotifyTime(float Time) const
{
    const float PlayLength = DataModel ? DataModel->GetPlayLength() : 0.f;
    return FMath::Clamp(Time, 0.f, FMath::Max(PlayLength, 0.f));
}

float UAnimSequenceBase::ClampNotifyDuration(float TriggerTime, float Duration) const
{
    const float PlayLength = DataModel ? DataModel->GetPlayLength() : 0.f;
    return FMath::Clamp(Duration, 0.f, FMath::Max(PlayLength - TriggerTime, 0.f));
}

int32 UAnimSequenceBase::FindNotifyIndex(const FGuid& NotifyGuid) const
{
    if (!NotifyGuid.IsValid())
    {
        return -1;
    }

    for (int32 Index = 0; Index < AnimNotifies.Num(); ++Index)
    {
        if (AnimNotifies[Index].Guid == NotifyGuid)
        {
            return Index;
        }
    }
    return -1;
}

void UAnimSequenceBase::RemoveNotifyAt(int32 Index)
{
    FAnimNotifyEvent& Event = AnimNotifies[Index];
    delete Event.Notify;
    delete Event.NotifyStateClass;

    // RemoveAtSwap은 정렬을 깨므로 순서를 유지하는 RemoveAt을 쓴다
    AnimNotifies.RemoveAt(static_cast<uint32>(Index));
}

bool UAnimSequenceBase::IsNotifyAvailable() const
{
    return (AnimNotifies.Num() != 0) && (DataModel->GetPlayLength() > 0.f);
}

void UAnimSequenceBase::GetAnimNotifies(float StartTime, float DeltaTime, FAnimNotifyContext& Context) const
{
    // Early out if we have no notifies
    if (!IsNotifyAvailable())
    {
        return;
    }

    bool const bPlayingBackwards = (DeltaTime < 0.f);
    float PreviousPosition = StartTime;
    float CurrentPosition = StartTime;
    float DesiredDeltaMove = DeltaTime;
    const float PlayLength = DataModel->GetPlayLength();

    uint32_t MaxLoopCount = 2;

    //최대 1000번까지 루프 가능
    if (PlayLength > 0.0f && std::abs(DeltaTime) > PlayLength)
    {
        MaxLoopCount = FMath::Clamp(int32(DesiredDeltaMove / PlayLength), 2, 1000);
    }

    for (uint32_t i = 0; i < MaxLoopCount; i++)
    {
        // 루프를 끈 채로 시간 진행 → 끝에 닿으면 거기서 멈춤
        const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, PlayLength);

        GetAnimNotifiesFromDeltaPositions(PreviousPosition, CurrentPosition, Context);

        // 끝에 닿았고 루프 재생이면 → 남은 이동량으로 0초(역재생이면 끝)부터 다시
        if (AdvanceType == ETypeAdvanceAnim::ETAA_Finished && Context.bIsLooping)
        {
            const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
            DesiredDeltaMove -= ActualDeltaMove;
            PreviousPosition = bPlayingBackwards ? PlayLength : 0.f;
            CurrentPosition = PreviousPosition;
        }
        else break;
    }
}

void UAnimSequenceBase::GetAnimNotifiesFromDeltaPositions(const float& PreviousPosition, const float& CurrentPosition, FAnimNotifyContext& NotifyContext) const
{
	// Early out if we have no notifies
	if (AnimNotifies.Num() == 0)
	{
		return;
	}

	bool const bPlayingBackwards = (CurrentPosition < PreviousPosition);

	// If playing backwards, flip Min and Max.
	if (bPlayingBackwards)
	{
		for (int32 NotifyIndex = 0; NotifyIndex < AnimNotifies.Num(); NotifyIndex++)
		{
			const FAnimNotifyEvent& AnimNotifyEvent = AnimNotifies[NotifyIndex];
			const float NotifyStartTime = AnimNotifyEvent.TriggerTime;
			const float NotifyEndTime = AnimNotifyEvent.GetEndTriggerTime();

			if ((NotifyStartTime < PreviousPosition) && (NotifyEndTime >= CurrentPosition))
			{			
				NotifyContext.ActiveNotifies.Emplace(&AnimNotifyEvent, this);
				const bool bHasFinished = CurrentPosition <= FMath::Max(NotifyStartTime, 0.f);
				if (bHasFinished)
				{
					//TODO : Context를 붙이는 방법을 사용하여 추가 정보를 붙인다, 추후에 알아보기
					//NotifyContext.ActiveNotifies.Top().AddContextData<UE::Anim::FAnimNotifyEndDataContext>(true);
					NotifyContext.bHasFinished = true;
				}
			}
		}
	}
	else
	{
		for (int32 NotifyIndex = 0; NotifyIndex < AnimNotifies.Num(); NotifyIndex++)
		{
			const FAnimNotifyEvent& AnimNotifyEvent = AnimNotifies[NotifyIndex];
			const float NotifyStartTime = AnimNotifyEvent.TriggerTime;
			const float NotifyEndTime = AnimNotifyEvent.GetEndTriggerTime();

			// Note that if you arrive with zero delta time (CurrentPosition == PreviousPosition), only Notify States will be extracted
			if ((NotifyStartTime <= CurrentPosition) && (NotifyEndTime > PreviousPosition))
			{
				NotifyContext.ActiveNotifies.Emplace(&AnimNotifyEvent, this);

				const bool bHasFinished = CurrentPosition >= NotifyEndTime;
				if (bHasFinished)
				{
					//NotifyContext.ActiveNotifies.Top().AddContextData<UE::Anim::FAnimNotifyEndDataContext>(true);
					NotifyContext.bHasFinished = true;
				}
			}
		}
	}
}
