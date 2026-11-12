#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "MovieScene/MovieSceneFrameTime.h"
#include "UObject/Object.h"
#include <utility>
#include "AnimDataModel.generated.h"

struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;
};

struct FBoneAnimationTrack
{
	FString Name;
	FRawAnimSequenceTrack InternalTrack;
};

struct FAnimationCurveData
{
};

class IAnimationDataModel
{
public:
	virtual ~IAnimationDataModel() = default;
	virtual const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const = 0;
};

UCLASS()
class UAnimDataModel : public UObject, public IAnimationDataModel
{
	GENERATED_BODY()

public:
	void Initialize(TArray<FBoneAnimationTrack> InTracks, FFrameRate InFrameRate, float InPlayLength)
	{
		BoneAnimationTracks = std::move(InTracks);
		FrameRate = InFrameRate;
		PlayLength = InPlayLength;
	}

	const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const override { return BoneAnimationTracks; }
	FFrameRate GetFrameRate() const { return FrameRate; }
	float GetPlayLength() const { return PlayLength; }
	int32 GetNumberOfKeys() const
	{
		return BoneAnimationTracks.IsEmpty() ? 0 : BoneAnimationTracks[0].InternalTrack.PosKeys.Num();
	}

	int32 GetNumberOfFrames() const
	{
		const int32 NumberOfKeys = GetNumberOfKeys();
		return NumberOfKeys > 0 ? NumberOfKeys - 1 : 0;
	}

private:
	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	FFrameRate FrameRate{0, 1};
	float PlayLength = 0.0f; // 초 단위
	FAnimationCurveData CurveData;
};
