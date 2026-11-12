#pragma once

#include "AnimDataModel.h"
#include "AnimationAsset.h"
#include "Animation/AnimTypes.h"
#include "AnimNotifyQueue.h"
#include <memory>
#include "AnimSequenceBase.generated.h"

UCLASS()
class UAnimSequenceBase : public UAnimationAsset
{
	GENERATED_BODY()

public:
	virtual ~UAnimSequenceBase() override;

	const UAnimDataModel* GetDataModel() const { return DataModel; }
	// 클립 길이(초). 데이터가 없으면 0 (Unreal UAnimSequenceBase::GetPlayLength)
	float GetPlayLength() const { return DataModel ? DataModel->GetPlayLength() : 0.0f; }
	bool IsNotifyAvailable() const;
	void GetAnimNotifies(float StartTime, float DeltaTime, FAnimNotifyContext& NotifyContext) const;
	void GetAnimNotifiesFromDeltaPositions(const float& PreviousPosition, const float& CurrentPosition, FAnimNotifyContext& NotifyContext) const;

	// ---------------- Notify 편집 (타임라인 UI용) ----------------
	// - Notify는 항상 TriggerTime 오름차순으로 정렬된 상태를 유지한다.
	// - 배열 Index는 추가/삭제/정렬마다 바뀌므로 UI는 Guid로만 Notify를 가리킨다.
	// - 클래스 기반 Notify 객체(UAnimNotify)는 이 시퀀스가 소유하며, 삭제 시 함께 delete된다.
	// - 주의: 애니메이션 틱 도중(Notify 핸들러 안 등)에 호출하면 안 된다.
	//   큐에 담긴 FAnimNotifyEventReference가 배열 원소를 포인터로 가리키기 때문이다.

	const TArray<FAnimNotifyEvent>& GetNotifies() const { return AnimNotifies; }

	// 없으면 nullptr. 반환된 Event의 Notify 객체 파라미터는 수정해도 되지만, 시간은 SetNotifyTriggerTime으로 바꾼다.
	const FAnimNotifyEvent* FindNotify(const FGuid& NotifyGuid) const;

	// Duration: 0이면 단발, >0이면 [Time, Time + Duration] 구간 동안 활성.
	// 시퀀스 끝을 넘지 않도록 잘린다. 단발이 아닌 이름/UAnimNotify 기반은 구간 동안 매 프레임 전달된다.

	// 이름 기반 Notify 추가. 새 Notify의 Guid를 반환한다.
	FGuid AddNotify(float Time, const FName& NotifyName, float Duration = 0.f);

	// 클래스 기반 Notify 추가. 이 시퀀스를 Outer로 객체를 만들어 반환하므로 호출한 쪽이 파라미터를 채운다.
	// T가 UAnimNotifyState 파생이면 NotifyStateClass에, UAnimNotify 파생이면 Notify에 담는다.
	template <typename T>
	T* AddNotify(float Time, float Duration = 0.f, FGuid* OutGuid = nullptr)
	{
		static_assert(std::is_base_of_v<UAnimNotify, T> || std::is_base_of_v<UAnimNotifyState, T>,
			"T must derive from UAnimNotify or UAnimNotifyState");
		T* NotifyObject = NewObject<T>(this);

		FAnimNotifyEvent NewEvent;
		if constexpr (std::is_base_of_v<UAnimNotifyState, T>)
		{
			NewEvent.NotifyStateClass = NotifyObject;
		}
		else
		{
			NewEvent.Notify = NotifyObject;
		}
		const FGuid NewGuid = AddNotifyEvent(Time, Duration, NewEvent);
		if (OutGuid)
		{
			*OutGuid = NewGuid;
		}
		return NotifyObject;
	}

	// 클래스 기반 Notify를 UClass로 추가 (Lua, 노드 에디터처럼 타입을 런타임에 고르는 곳용).
	// UAnimNotify 또는 UAnimNotifyState 파생이 아니면 nullptr. 파라미터는 리플렉션으로 채운다.
	UObject* AddNotifyOfClass(UClass* NotifyClass, float Time, float Duration = 0.f, FGuid* OutGuid = nullptr);

	// 시간 변경 (타임라인에서 드래그). 범위를 제한하고 다시 정렬한다.
	bool SetNotifyTriggerTime(const FGuid& NotifyGuid, float Time);
	// 길이 변경 (NotifyState 끝 핸들 드래그). 단발 Notify(Notify 객체/이름 기반)는 0으로 유지된다.
	bool SetNotifyDuration(const FGuid& NotifyGuid, float Duration);
	// 이 가중치 이상으로 블렌드될 때만 발동한다 (BlendSpace에서 우세한 샘플만 울리게 할 때)
	bool SetNotifyTriggerWeightThreshold(const FGuid& NotifyGuid, float Threshold);
	// 있는 트랙 범위(0 ~ GetNumNotifyTracks()-1)로 잘린다. 새 트랙은 AddNotifyTrack/InsertNotifyTrack으로 만든다.
	bool SetNotifyTrackIndex(const FGuid& NotifyGuid, int32 TrackIndex);
	// 이름 기반 Notify의 이름 (클래스 Notify는 HandleNamedNotify로 가지 않으므로 표시용일 뿐이다)
	bool SetNotifyName(const FGuid& NotifyGuid, const FName& NotifyName);

	// ---------------- Notify 트랙 (타임라인의 줄) ----------------
	// 항상 1개 이상이며, 모든 Notify의 TrackIndex보다 많다.
	int32 GetNumNotifyTracks() const { return FMath::Max(NotifyTracks.Num(), 1); }
	FString GetNotifyTrackName(int32 TrackIndex) const;   // 이름이 비어 있으면 번호
	const TArray<FAnimNotifyTrack>& GetNotifyTracks() const { return NotifyTracks; }

	int32 AddNotifyTrack(const FString& Name = FString());                     // 맨 아래에 추가, 새 Index 반환
	// Index 자리에 끼워 넣는다. 그 아래 트랙의 Notify는 TrackIndex가 1씩 밀린다.
	int32 InsertNotifyTrack(int32 TrackIndex, const FString& Name = FString());
	// 트랙과 그 위의 Notify를 지운다. 마지막 1개는 지울 수 없다.
	bool RemoveNotifyTrack(int32 TrackIndex);
	bool RenameNotifyTrack(int32 TrackIndex, const FString& Name);

	// 실행 취소용 스냅샷: 트랙/Notify 전체(프로퍼티, Guid 포함)를 .anim.bin v3 Notify 섹션과 같은 형식으로 저장·복원한다.
	// 복원하면 Notify 객체가 새로 만들어지므로, 진행 중인 NotifyState는 호출하는 쪽이 먼저 닫아야 한다.
	bool CaptureNotifySnapshot(TArray<uint8>& OutBytes);
	bool RestoreNotifySnapshot(const TArray<uint8>& Bytes);

	bool RemoveNotify(const FGuid& NotifyGuid);
	int32 RemoveNotifies(const TArray<FGuid>& NotifyGuids);   // 다중 선택 삭제, 삭제한 개수 반환
	int32 RemoveNotifiesByName(const FName& NotifyName);      // 같은 이름 전부 삭제 (UE RemoveNotifies(Names))
	void RemoveAllNotifies();                                 // UE RemoveNotifies()

	void SortNotifies();

	// Notify 객체의 소멸자가 부른다. 목록에 남은 포인터를 nullptr로 바꿔 두 번 delete되지 않게 한다.
	// 항목 자체는 지우지 않으므로 순회 중(RemoveNotifyAt, 소멸자)에 불려도 배열이 바뀌지 않는다.
	void OnNotifyObjectDestroyed(const UObject* NotifyObject);

private:
	FGuid AddNotifyEvent(float Time, float Duration, FAnimNotifyEvent& NewEvent);
	float ClampNotifyTime(float Time) const;
	float ClampNotifyDuration(float TriggerTime, float Duration) const;   // TriggerTime + Duration <= PlayLength
	int32 FindNotifyIndex(const FGuid& NotifyGuid) const;
	void RemoveNotifyAt(int32 Index);   // 소유한 Notify 객체를 delete하고 순서를 유지한 채 제거
	void EnsureNotifyTracks();          // 트랙 수를 1개 이상, 가장 큰 TrackIndex + 1 이상으로 맞춘다

protected:
	// .anim.bin v3 Notify 섹션 (트랙 목록 + Notify).
	// 저장: NotifyTracks/AnimNotifies를 쓴다 (Out 인자는 쓰지 않음).
	// 불러오기: Out 인자에 담기만 한다. DataModel이 아직 없어 시간 범위를 확인할 수 없으므로
	//          Initialize 뒤에 ReplaceNotifies로 넣는다. 실패하면 OutLoaded의 객체는 호출한 쪽이 DeleteNotifyObjects로 지운다.
	void SerializeNotifyEvents(FArchive& Ar, TArray<FAnimNotifyTrack>& OutTracks, TArray<FAnimNotifyEvent>& OutLoaded);

	// 기존 트랙/Notify(객체 포함)를 지우고 바꾼다. 시간/길이를 다시 자르고 정렬한다.
	void ReplaceNotifies(TArray<FAnimNotifyTrack>&& Tracks, TArray<FAnimNotifyEvent>&& Events);
	static void DeleteNotifyObjects(TArray<FAnimNotifyEvent>& Events);

	void InitializeDataModel(
		TArray<FBoneAnimationTrack> Tracks, FFrameRate FrameRate, float PlayLength)
	{
		if (!DataModel)
		{
			// unique_ptr은 raw 포인터를 '='로 받을 수 없다. 소유권을 넘길 때는 reset을 쓴다.
			DataModel = NewObject<UAnimDataModel>(this);
		}

		DataModel->Initialize(std::move(Tracks), FrameRate, PlayLength);
	}

private:
	UAnimDataModel* DataModel = nullptr;
	TArray<FAnimNotifyEvent> AnimNotifies;
	TArray<FAnimNotifyTrack> NotifyTracks;
};
