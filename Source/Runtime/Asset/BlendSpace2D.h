#pragma once

#include "Asset/BlendSpace.h"
#include <array>
#include "BlendSpace2D.generated.h"

class UAnimSequence;

UCLASS()
class UBlendSpace2D : public UBlendSpace
{
	GENERATED_BODY()
public:
	struct FSample
	{
		float X = 0.0f;
		float Y = 0.0f;
		UAnimSequence* Sequence = nullptr;
	};
	struct FTriangle { int32 A, B, C; };

	bool AddSample(float X, float Y, UAnimSequence* Sequence);
	bool ReplaceSamples(const TArray<FSample>& InSamples);
	const TArray<FSample>& GetSamples() const { return Samples; }
	const TArray<FTriangle>& GetTriangles() const { return Triangles; }
	float GetEffectiveLength(float X, float Y) const override;
	bool Evaluate(float X, float Y, float NormalizedTime,
		TArray<FAnimBoneTransform>& OutPose) const override;
	bool ExtractRootMotion(float X, float Y, float PreviousTime, float CurrentTime,
		int32 LoopCount, FMatrix& OutDelta) const override;
	void GetNotifies(float X, float Y, float PreviousTime, float CurrentTime, int32 LoopCount,
		bool bIncludeStart, TArray<FAnimNotifyQueueItem>& OutQueue) const override;

private:
	struct FWeightedSample { const FSample* Sample = nullptr; float Weight = 0.0f; };
	bool GetBlendSamples(float X, float Y, std::array<FWeightedSample, 3>& Out, int32& OutCount) const;
	void RebuildTriangles();
	TArray<FSample> Samples;
	TArray<FTriangle> Triangles;
};
