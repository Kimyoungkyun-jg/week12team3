#pragma once

#include "Asset/AnimNotifyEvent.h"
#include "Asset/AnimSequenceBase.h"
#include "Asset/AnimTransform.h"
#include "Math/Matrix.h"
#include "AnimSequence.generated.h"

enum class ERootMotionRootLock : uint8 { RefPose, AnimFirstFrame, Zero };

UCLASS()
class UAnimSequence : public UAnimSequenceBase
{
	GENERATED_BODY()
public:
	FString Name;
	FString SourceFilePath;
	int32 SourceTakeIndex = 0;
	bool bEnableRootMotion = false;
	bool bForceRootLock = false;
	bool bUseNormalizedRootMotionScale = false;
	ERootMotionRootLock RootMotionRootLock = ERootMotionRootLock::RefPose;
	float RateScale = 1.0f;
	TArray<FAnimNotifyEvent> Notifies;

	// Notify 클래스 이름(예: "UAnimNotify_Log")으로 객체를 만들어 TriggerTime에 추가하고 그 객체를 돌려준다.
	// 클래스를 찾을 수 없거나 추상 클래스이거나 UAnimNotify가 아니면 로그를 남기고 nullptr을 돌려준다.
	// 속성은 돌려받은 객체에 설정한다. 저장·로드와 에디터 편집도 이 함수로 Notify를 만든다.
	UAnimNotify* AddNotify(const FString& EventName, const FString& NotifyClassName, float TriggerTime);

	// 구간형(Notify State)을 추가한다. 클래스가 없거나 추상이거나 UAnimNotifyState가 아니면 nullptr.
	// 시작은 [0, PlayLength], 길이는 GetMinNotifyStateDuration() 이상이고 끝이 PlayLength를 넘지 않게 맞춘다.
	UAnimNotifyState* AddNotifyState(const FString& EventName, const FString& StateClassName, float StartTime, float Duration);
	// 구간형의 최소 길이(초). 구간이 한 프레임보다 짧아져 시작과 끝이 같은 프레임에 들지 않게 하는 하한이다.
	static float GetMinNotifyStateDuration() { return 1.0f / 120.0f; }
	// 시작 시각과 길이를 시퀀스 범위로 맞춘다. 에디터에서 끌거나 값을 입력할 때도 이 함수로 맞춘다.
	void ClampNotifyStateRange(float& InOutStart, float& InOutDuration) const;

	// 저장 형식(클래스 이름 + 속성 JSON)과 Notify 객체 사이를 변환한다.
	// ImportNotifies는 기존 Notifies를 비우고 레코드로 다시 만든다. 알 수 없는 클래스의 레코드는 경고를 남기고 건너뛴다.
	// 시간은 DataModel이 있으면 [0, PlayLength]로 맞춘다. DataModel을 먼저 설정한 뒤 호출한다.
	void ExportNotifies(TArray<FAnimNotifyRecord>& OutRecords) const;
	void ImportNotifies(const TArray<FAnimNotifyRecord>& Records);

	// 이번 프레임에 플레이어가 진행한 구간에 든 Notify를 OutNotifies 뒤에 추가한다 (비우지 않는다).
	// 구간 규칙: 기본은 (PreviousTime, CurrentTime]. 루프를 넘으면 (PreviousTime, Length], 전체 주기 [0, Length] x (LoopCount - 1), [0, CurrentTime].
	// bIncludeStart가 true면 첫 구간의 시작도 포함한다 (재생을 막 시작한 첫 Update에서 0초 Notify를 놓치지 않기 위해).
	void GetNotifies(float PreviousTime, float CurrentTime, int32 LoopCount, bool bIncludeStart, TArray<FAnimNotifyEvent>& OutNotifies) const;

	// GetNotifies로 모은 Notify를 가중치와 이 시퀀스를 붙여 OutQueue 뒤에 추가한다. BlendSpace가 샘플마다 쓴다.
	void GetNotifyQueueItems(float PreviousTime, float CurrentTime, int32 LoopCount, bool bIncludeStart,
		float Weight, TArray<FAnimNotifyQueueItem>& OutQueue) const;

	int32 GetFrameIndex(float TimeSeconds) const;
	int32 GetRootBoneIndex() const;
	bool SampleRawRoot(float TimeSeconds, FAnimBoneTransform& OutRoot) const;
	bool ExtractRootMotion(float PreviousTime, float CurrentTime, int32 LoopCount, FMatrix& OutDelta) const;
	bool EvaluateLocalPoseTransforms(float TimeSeconds, TArray<FAnimBoneTransform>& OutPose, bool bApplyRootLock = true) const;
	bool EvaluateLocalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const;
	// 본 변환은 스켈레탈 메시 컴포넌트 공간 기준
	bool EvaluateGlobalPose(float TimeSeconds, TArray<FMatrix>& OutPose) const;
};
