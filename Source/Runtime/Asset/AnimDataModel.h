#pragma once

#include "Containers/Array.h"
#include "Core/NameTypes.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "UObject/Object.h"
#include "AnimDataModel.generated.h"

using FVector3f = FVector;
using FQuat4f = FQuat;

struct FRawAnimSequenceTrack
{
	TArray<FVector3f> PosKeys;
	TArray<FQuat4f> RotKeys;
	TArray<FVector3f> ScaleKeys;
};

struct FBoneAnimationTrack
{
	FName Name;
	FRawAnimSequenceTrack InternalTrack;
};

struct FAnimFrameRate
{
	int32 Numerator = 30;
	int32 Denominator = 1;
	float AsDecimal() const { return Denominator > 0 ? static_cast<float>(Numerator) / Denominator : 0.0f; }
};

struct FAnimationCurveData
{
};

UCLASS()
class UAnimDataModel : public UObject
{
	GENERATED_BODY()
public:
	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	float PlayLength = 0.0f;
	FAnimFrameRate FrameRate;
	int32 NumberOfFrames = 0;
	int32 NumberOfKeys = 0;
	FAnimationCurveData CurveData;

	const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const { return BoneAnimationTracks; }
};
