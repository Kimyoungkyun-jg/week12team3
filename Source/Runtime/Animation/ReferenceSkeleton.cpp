#include "EnginePCH.h"
#include "Animation/ReferenceSkeleton.h"
#include "Animation/Skeleton.h"
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

FMatrix FSkeletalMeshSocket::GetSocketLocalMatrix() const
{
	const FTransform LocalTransform(RelativeRotation, RelativeLocation, RelativeScale);
	return LocalTransform.GetLocalMatrix();
}

int32 USkeleton::FindSocketIndex(const FString& SocketName) const
{
	if (SocketName.empty())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Sockets.Num(); ++Index)
	{
		if (Sockets[Index].SocketName == SocketName)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

const FSkeletalMeshSocket* USkeleton::FindSocket(const FString& SocketName) const
{
	const int32 Index = FindSocketIndex(SocketName);
	return Index != INDEX_NONE ? &Sockets[Index] : nullptr;
}

FSkeletalMeshSocket* USkeleton::FindMutableSocket(const FString& SocketName)
{
	const int32 Index = FindSocketIndex(SocketName);
	return Index != INDEX_NONE ? &Sockets[Index] : nullptr;
}

bool USkeleton::HasSocket(const FString& SocketName) const
{
	return FindSocketIndex(SocketName) != INDEX_NONE;
}

bool USkeleton::AddSocket(const FSkeletalMeshSocket& NewSocket)
{
	if (NewSocket.SocketName.empty() || NewSocket.BoneName.empty() || HasSocket(NewSocket.SocketName))
	{
		return false;
	}
	if (RefSkeleton.FindBoneIndex(NewSocket.BoneName) < 0)
	{
		return false;
	}
	Sockets.Add(NewSocket);
	return true;
}

bool USkeleton::RemoveSocket(const FString& SocketName)
{
	const int32 Index = FindSocketIndex(SocketName);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Sockets.RemoveAt(Index, 1);
	return true;
}
