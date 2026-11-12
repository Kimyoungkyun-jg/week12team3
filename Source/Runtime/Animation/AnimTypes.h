#pragma once
#include "Core/NameTypes.h"
#include "Core/Misc/Guid.h"
#include "AnimTypes.generated.h"

// 포인터로만 쓰므로 include 대신 전방 선언 (AnimNotify.h를 include하면 순환 include가 생긴다)
class UAnimNotify;
class UAnimNotifyState;

enum class ETypeAdvanceAnim : uint8
{
	ETAA_Default,
	ETAA_Finished,
	ETAA_Looped
};

// 타임라인의 Notify 한 줄 (UE FAnimNotifyTrack). 재생에는 영향이 없고 편집 화면 정리용이다.
struct FAnimNotifyTrack
{
    FString Name;   // 비어 있으면 화면에는 번호("1", "2", ...)로 보인다
};

USTRUCT()
struct FAnimNotifyEvent
{
    GENERATED_BODY()
    float TriggerTime = 0.f;
    float Duration = 0.f;                 // 0이면 단발, >0이면 State

    UAnimNotify* Notify = nullptr;        // 있으면 클래스 기반
    UAnimNotifyState* NotifyStateClass = nullptr;
    FName        NotifyName;              // Notify가 없으면 이름 기반

    float TriggerWeightThreshold = 0.f;   // 가중치 필터

    // 타임라인에서 몇 번째 줄에 그릴지 (UE FAnimNotifyEvent::TrackIndex). 재생에는 영향이 없다.
    int32 TrackIndex = 0;

    // 타임라인 UI가 Notify를 가리키는 식별자 (UE의 FAnimNotifyEvent::Guid).
    // 배열 Index는 추가/삭제/정렬마다 바뀌므로 Index 대신 이 값을 쓴다.
    FGuid Guid;

    // 단발 Notify는 End == Start가 되어야 (Prev, Curr] 구간 검사에 걸린다
    float GetEndTriggerTime() const { return TriggerTime + Duration; }
};
