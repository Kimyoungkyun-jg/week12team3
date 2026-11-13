#pragma once

#include "Core/EngineString.h"

class UAnimNotify;
class UAnimNotifyState;
class UAnimSequence;

// 시퀀스의 특정 시각(초, 0~PlayLength)에 발생하는 이벤트
struct FAnimNotifyEvent
{
	// 에디터 표시와 로그용 이름. 어떤 동작을 하는지는 Notify 객체가 정한다.
	FString Name;
	float TriggerTime = 0.0f;
	// 블렌딩 중 전파된 가중치가 이 값보다 작으면 호출하지 않는다. 양쪽 상태의 Notify가 모두 호출되지 않게 Notify마다 조절한다.
	float TriggerWeightThreshold = 0.00001f;
	// 호출할 Notify 객체. UAnimSequence가 Outer이고, 소멸은 엔진 종료 때 UObject와 함께 일괄 처리된다.
	UAnimNotify* Notify = nullptr;
	// 구간형(Notify State)이면 이 객체를 쓰고 Notify는 nullptr이다. TriggerTime이 시작, TriggerTime + Duration이 끝이다.
	// 상태 객체는 시퀀스 에셋에 들어 있어 여러 컴포넌트가 공유하므로 호출 상태(시작했는지 등)를 들고 있지 않는다.
	UAnimNotifyState* NotifyState = nullptr;
	// NotifyState의 길이(초). Notify는 0이다.
	float Duration = 0.0f;
	// UAnimSequence::GetNotifies가 모은 복사본에만 채워진다. 이번에 모은 구간 안에서 구간형이 끝났는가.
	bool bStateEnds = false;

	bool IsState() const { return NotifyState != nullptr; }
};

// .fbxbin에 저장되는 Notify 한 건. UObject 없이 클래스 이름과 속성(JSON 문자열)만 들고 있다.
// 시퀀스를 로드할 때 클래스 이름으로 Notify 객체를 만들고 속성을 채운다.
struct FAnimNotifyRecord
{
	FString Name;
	float TriggerTime = 0.0f;
	float TriggerWeightThreshold = 0.00001f;
	// 구간형(Notify State)의 길이(초). Notify는 0. 파일 버전 7부터 저장된다.
	float Duration = 0.0f;
	FString ClassName;
	FString Properties;
};

// 노드 트리를 거쳐 UAnimInstance까지 올라온 Notify 한 건
struct FAnimNotifyQueueItem
{
	FAnimNotifyEvent Event;
	// 루트에서 이 Notify를 낸 노드까지의 블렌드 가중치를 곱한 값 (전이 중이 아니면 1)
	float Weight = 1.0f;
	// Notify가 든 시퀀스
	const UAnimSequence* Source = nullptr;
	// 이 항목을 올린 노드. 같은 노드가 그래프에서 여러 경로로 연결될 때 한 번만 올리는 데 쓴다 (예: 슬롯의 입력과 레이어 블렌드의 기본 포즈가 같은 노드).
	const void* SourceNode = nullptr;
};
