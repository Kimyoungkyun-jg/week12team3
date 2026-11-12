#pragma once

#include "UObject/Object.h"
#include "Math/Transform.h"
#include "AnimationAsset.generated.h"

// Unreal FRootMotionMovementParams (AnimationAsset.h). 블렌드 관련 기능은 제외
// 구간별로 뽑은 root motion 조각을 하나로 누적한다
struct FRootMotionMovementParams
{
public:
	bool bHasRootMotion = false;

private:
	FTransform RootMotionTransform; // 총 합친 이동량

public:
	void Set(const FTransform& InTransform)
	{
		bHasRootMotion = true;
		RootMotionTransform = InTransform;
		RootMotionTransform.Scale = FVector(1.f, 1.f, 1.f);   // Unreal: SetScale3D(RootMotionScale)
	}

	void Accumulate(const FTransform& InTransform)
	{
		if (!bHasRootMotion)
		{
			Set(InTransform);
		}
		else
		{
			// Unreal: RootMotionTransform = InTransform * RootMotionTransform  (기존 다음에 InTransform)
			RootMotionTransform = Multiply(InTransform, RootMotionTransform);
			RootMotionTransform.Scale = FVector(1.f, 1.f, 1.f);
		}
	}

	void Accumulate(const FRootMotionMovementParams& MovementParams)
	{
		if (MovementParams.bHasRootMotion)
		{
			Accumulate(MovementParams.RootMotionTransform);
		}
	}

	void Clear() { bHasRootMotion = false; }

	FTransform GetRootMotionTransform() const { return RootMotionTransform; }

private:
	// Unreal FTransform::operator*(A, B): A를 먼저 적용하고 그 다음 B
	static FTransform Multiply(const FTransform& A, const FTransform& B)
	{
		FTransform Result;
		Result.Rotation = B.Rotation * A.Rotation;
		Result.Scale = FVector(A.Scale.X * B.Scale.X, A.Scale.Y * B.Scale.Y, A.Scale.Z * B.Scale.Z);
		const FVector ScaledLoc(A.Location.X * B.Scale.X, A.Location.Y * B.Scale.Y, A.Location.Z * B.Scale.Z);
		Result.Location = B.Rotation.RotateVector(ScaledLoc) + B.Location;
		return Result;
	}
};

UCLASS()
class UAnimationAsset : public UObject
{
    GENERATED_BODY()

public:
    void SetPreviewMeshPath(FString InPath) { PreviewMeshPath = std::move(InPath); }
    const FString& GetPreviewMeshPath() const { return PreviewMeshPath; }

    void SetAssetPath(const FString& InPath) { AssetPath = InPath; }
    const FString& GetAssetPath() const { return AssetPath; }
private:
    FString PreviewMeshPath;
    FString AssetPath;
};