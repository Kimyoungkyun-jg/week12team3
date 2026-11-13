#include "EnginePCH.h"
#include "StaticPoseRetargeter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "Math/Quat.h"

#include <algorithm>
#include <cmath>

namespace
{
	FMatrix RotationBetween(const FVector& From, const FVector& To)
	{
		const FVector A = From.GetSafeNormal();
		const FVector B = To.GetSafeNormal();
		if (A.SizeSquared() < 1e-8f || B.SizeSquared() < 1e-8f) return FMatrix::Identity;
		const float Dot = std::clamp(FVector::Dot(A, B), -1.0f, 1.0f);
		if (Dot > 0.9999f) return FMatrix::Identity;
		FVector Axis = FVector::Cross(A, B);
		if (Axis.SizeSquared() < 1e-8f)
		{
			const FVector Reference = std::abs(A.Z) < 0.9f ? FVector(0, 0, 1) : FVector(0, 1, 0);
			Axis = FVector::Cross(A, Reference);
		}
		return FQuat::MakeFromAxisAngle(Axis.GetSafeNormal(), std::acos(Dot)).ToFMatrix();
	}
}

static bool ApplyRigPose(const USkeletalMesh& SourceRig, const TArray<FMatrix>* SourceGlobalPose,
	USkeletalMeshComponent& TargetMesh,
	const TArray<int32>& TargetBoneToSourceBone, const TArray<int32>& TargetAimChild,
	int32 SourceYawQuarterTurns, FString& OutError) // 각 본이 향하는 방향을 Source의 기준 자세 또는 애니메이션 자세에 맞추는 코드
{
	USkeletalMesh* TargetAsset = TargetMesh.GetSkeletalMesh();
	if (!TargetAsset)
	{
		OutError = "Select a target mesh first.";
		return false;
	}
	const TArray<FBoneInfo>& Source = SourceRig.MeshData.Bones;
	const TArray<FBoneInfo>& Target = TargetAsset->MeshData.Bones;
	if (Source.IsEmpty() || Target.IsEmpty() || TargetBoneToSourceBone.Num() != Target.Num() ||
		TargetAimChild.Num() != Target.Num() || (SourceGlobalPose && SourceGlobalPose->Num() != Source.Num()))
	{
		OutError = "Both rigs need bones and a complete mapping table.";
		return false;
	}
	for (int32 Index = 0; Index < Target.Num(); ++Index)
		if (TargetBoneToSourceBone[Index] >= Source.Num() || TargetBoneToSourceBone[Index] < -1)
		{
			OutError = "A mapped source bone index is invalid.";
			return false;
		}

	const FMatrix Axis = FQuat::MakeFromAxisAngle(FVector(0, 0, 1),
		static_cast<float>(SourceYawQuarterTurns) * 1.57079632679f).ToFMatrix();
	TArray<FMatrix> Global;
	int32 AlignedChains = 0;
	Global.SetNum(Target.Num());
	for (int32 Index = 0; Index < Target.Num(); ++Index)
	{
		const int32 Parent = Target[Index].ParentIndex;
		if (Parent >= Index || Parent < -1)
		{
			OutError = "Target bones must be ordered parent before child.";
			return false;
		}
		Global[Index] = Parent < 0 ? Target[Index].GlobalBindPose : Target[Index].LocalBindPose * Global[Parent];
		const int32 SourceIndex = TargetBoneToSourceBone[Index];
		if (SourceIndex < 0) continue;

		// A matched parent-child pair supplies a direction. The target keeps its own bone lengths.
		for (int32 Child = Index + 1; Child < Target.Num(); ++Child)
		{
			if (Target[Child].ParentIndex != Index) continue;
			if (TargetAimChild[Index] >= 0 && TargetAimChild[Index] != Child) continue;
			const int32 SourceChild = TargetBoneToSourceBone[Child];

			if (SourceChild < 0 || SourceChild == SourceIndex) continue;
			bool bDescendant = false;
			for (int32 Ancestor = Source[SourceChild].ParentIndex; Ancestor >= 0 && Ancestor < Source.Num(); Ancestor = Source[Ancestor].ParentIndex)
				if (Ancestor == SourceIndex) { bDescendant = true; break; }

			if (!bDescendant) continue;

			const FVector CurrentOrigin = Global[Index].TransformPosition(FVector::ZeroVector);
			const FVector CurrentChild = (Target[Child].LocalBindPose * Global[Index]).TransformPosition(FVector::ZeroVector);
			const FMatrix& SourceParentPose = SourceGlobalPose ? (*SourceGlobalPose)[SourceIndex] : Source[SourceIndex].GlobalBindPose;
			const FMatrix& SourceChildPose = SourceGlobalPose ? (*SourceGlobalPose)[SourceChild] : Source[SourceChild].GlobalBindPose;
			const FVector SourceOrigin = SourceParentPose.TransformPosition(FVector::ZeroVector);
			const FVector SourceTip = SourceChildPose.TransformPosition(FVector::ZeroVector);
			const FVector DesiredDirection = Axis.TransformVector(SourceTip - SourceOrigin);
			FMatrix DesiredGlobal = Global[Index] * RotationBetween(CurrentChild - CurrentOrigin, DesiredDirection);
			DesiredGlobal.SetOrigin(CurrentOrigin);
			Global[Index] = DesiredGlobal;
			++AlignedChains;
			break;
		}
	}
	if (AlignedChains == 0)
	{
		OutError = "Map at least one parent and child pair in both rigs.";
		return false;
	}

	TargetMesh.SetGlobalBoneTransforms(Global);
	TargetMesh.UpdateSkinning();
	OutError.clear();
	return true;
}

bool ApplyStaticRigPose(const USkeletalMesh& SourceRig, USkeletalMeshComponent& TargetMesh,
	const TArray<int32>& TargetBoneToSourceBone, const TArray<int32>& TargetAimChild,
	int32 SourceYawQuarterTurns, FString& OutError)
{
	return ApplyRigPose(SourceRig, nullptr, TargetMesh, TargetBoneToSourceBone,
		TargetAimChild, SourceYawQuarterTurns, OutError);
}

bool ApplyAnimatedRigPose(const USkeletalMesh& SourceRig, const TArray<FMatrix>& SourceGlobalPose,
	USkeletalMeshComponent& TargetMesh, const TArray<int32>& TargetBoneToSourceBone,
	const TArray<int32>& TargetAimChild, int32 SourceYawQuarterTurns, FString& OutError)
{
	return ApplyRigPose(SourceRig, &SourceGlobalPose, TargetMesh, TargetBoneToSourceBone,
		TargetAimChild, SourceYawQuarterTurns, OutError);
}
