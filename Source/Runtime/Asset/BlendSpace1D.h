#pragma once

#include "Asset/BlendSpace.h"
#include "BlendSpace1D.generated.h"

class UAnimSequence;

UCLASS()
class UBlendSpace1D : public UBlendSpace
{
	GENERATED_BODY()
public:
	struct FSample
	{
		float X = 0.0f;
		UAnimSequence* Sequence = nullptr;
	};

	bool AddSample(float X, UAnimSequence* Sequence);
	bool ReplaceSamples(const TArray<FSample>& InSamples);
	const TArray<FSample>& GetSamples() const { return Samples; }
	float GetEffectiveLength(float X, float Y) const override;
	bool Evaluate(float X, float Y, float NormalizedTime,
		TArray<FAnimBoneTransform>& OutPose) const override;
	bool ExtractRootMotion(float X, float Y, float PreviousTime, float CurrentTime,
		int32 LoopCount, FMatrix& OutDelta) const override;
	void GetNotifies(float X, float Y, float PreviousTime, float CurrentTime, int32 LoopCount,
		bool bIncludeStart, TArray<FAnimNotifyQueueItem>& OutQueue) const override;

private:
	bool GetBlendSamples(float X, const FSample*& OutA, const FSample*& OutB, float& OutAlpha) const;
	TArray<FSample> Samples;
};
