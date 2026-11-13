#pragma once

#include "Asset/AnimationAsset.h"
#include "Asset/AnimNotifyEvent.h"
#include "Asset/AnimTransform.h"
#include "BlendSpace.generated.h"

UCLASS()
class UBlendSpace : public UAnimationAsset
{
	GENERATED_BODY()
public:
	virtual float GetEffectiveLength(float X, float Y) const { return 0.0f; }
	virtual bool Evaluate(float X, float Y, float NormalizedTime,
		TArray<FAnimBoneTransform>& OutPose) const { return false; }
	virtual bool ExtractRootMotion(float X, float Y, float PreviousTime, float CurrentTime,
		int32 LoopCount, FMatrix& OutDelta) const { return false; }
	// 정규화 시간 구간 (PreviousTime, CurrentTime]에 든 Notify를 OutQueue 뒤에 추가한다 (비우지 않는다).
	// 입력 (X, Y)에서 섞이는 샘플마다 그 샘플의 길이로 구간을 초로 바꿔 모으고, 항목의 Weight는 샘플의 블렌드 가중치다.
	// 가중치가 0인 샘플은 건너뛴다. 구간 규칙과 bIncludeStart의 뜻은 UAnimSequence::GetNotifies와 같다.
	virtual void GetNotifies(float X, float Y, float PreviousTime, float CurrentTime, int32 LoopCount,
		bool bIncludeStart, TArray<FAnimNotifyQueueItem>& OutQueue) const {}
};
