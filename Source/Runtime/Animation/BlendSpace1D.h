#pragma once

#include "AnimSequence.h"
#include "AnimationAsset.h"
#include "BlendSpace1D.generated.h"

struct FBlendSample
{
	UAnimSequence* Animation = nullptr;
	FVector SampleValue = FVector::ZeroVector;
};

UCLASS()
class UBlendSpace1D : public UAnimationAsset
{
	GENERATED_BODY()

public:
	bool AddSample(UAnimSequence* Animation, const FVector& SampleValue);
	bool GetSamples(float X, FBlendSample& OutA, FBlendSample& OutB, float& OutAlpha) const;

	const TArray<FBlendSample>& GetSampleData() const { return BlendSampleData; }
	bool RemoveSample(int32 Index);

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

private:
	TArray<FBlendSample> BlendSampleData;
};