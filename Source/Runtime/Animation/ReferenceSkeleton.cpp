#include "EnginePCH.h"
#include "Animation/ReferenceSkeleton.h"
#include "Render/SkeletalMeshData.h"
#include <cmath>
#include <unordered_set>

int32 FReferenceSkeleton::FindBoneIndex(const FString& Name) const
{
	for (int32 Index = 0; Index < RefBoneInfo.Num(); ++Index)
	{
		if (RefBoneInfo[Index].Name == Name)
		{
			return Index;
		}
	}
	return -1;
}

bool FReferenceSkeleton::Validate(FString& OutError) const
{
	if (RefBoneInfo.IsEmpty() || RefBoneInfo.Num() != RefBonePose.Num())
	{
		OutError = "Invalid reference skeleton size";
		return false;
	}

	std::unordered_set<FString> Names;
	for (int32 Index = 0; Index < RefBoneInfo.Num(); ++Index)
	{
		const FMeshBoneInfo& Bone = RefBoneInfo[Index];
		// 부모가 항상 앞에 있어야 한 번의 순회로 컴포넌트 공간을 누적할 수 있다.
		if (Bone.Name.empty() || !Names.insert(Bone.Name).second || Bone.ParentIndex >= Index)
		{
			OutError = "Invalid or duplicate bone: " + Bone.Name;
			return false;
		}

		const FBoneTransform& Pose = RefBonePose[Index];
		const float Values[] = { Pose.Translation.X, Pose.Translation.Y, Pose.Translation.Z,
			Pose.Rotation.X, Pose.Rotation.Y, Pose.Rotation.Z, Pose.Rotation.W,
			Pose.Scale.X, Pose.Scale.Y, Pose.Scale.Z };
		for (float Value : Values)
		{
			if (!std::isfinite(Value))
			{
				OutError = "Non-finite bone transform: " + Bone.Name;
				return false;
			}
		}
	}
	return true;
}

bool FReferenceSkeleton::GetModelSpaceMatrices(TArray<FMatrix>& Out, FString& Error) const
{
	return Validate(Error) && FillComponentSpaceTransforms(RefBonePose, Out);
}

void FReferenceSkeleton::BuildFromBones(const TArray<FBoneInfo>& Bones)
{
	RefBoneInfo.SetNum(Bones.Num());
	RefBonePose.SetNum(Bones.Num());
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		RefBoneInfo[Index].Name = Bones[Index].Name;
		RefBoneInfo[Index].ParentIndex = Bones[Index].ParentIndex;
		RefBonePose[Index] = FBoneTransform::FromMatrix(Bones[Index].LocalBindPose);
	}
}

bool FReferenceSkeleton::FillComponentSpaceTransforms(const TArray<FBoneTransform>& LocalPose, TArray<FMatrix>& OutComponentSpace) const
{
	if (LocalPose.Num() != RefBoneInfo.Num())
	{
		return false;
	}

	OutComponentSpace.SetNum(LocalPose.Num());
	for (int32 Index = 0; Index < LocalPose.Num(); ++Index)
	{
		OutComponentSpace[Index] = LocalPose[Index].ToMatrix();
		const int32 Parent = RefBoneInfo[Index].ParentIndex;
		if (Parent >= 0)
		{
			OutComponentSpace[Index] = OutComponentSpace[Index] * OutComponentSpace[Parent];
		}
	}
	return true;
}
