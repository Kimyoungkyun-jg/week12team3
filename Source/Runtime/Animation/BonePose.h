#pragma once

#include "Render/SkeletalMeshData.h"

// UE FCompactPose를 단순화했다. UE의 FCompactPoseBoneIndex 대신 RefSkeleton 본 인덱스를 그대로 쓴다.
// Bones[i]는 RefSkeleton[i]의 부모 기준 로컬 트랜스폼이다.
struct FCompactPose
{
	int32 GetNumBones() const { return Bones.Num(); }
	const TArray<FTransform>& GetBones() const { return Bones; }

	FTransform& operator[](int32 BoneIndex) { return Bones[BoneIndex]; }
	const FTransform& operator[](int32 BoneIndex) const { return Bones[BoneIndex]; }

	void SetNumBones(int32 NumBones) { Bones.SetNum(NumBones); }

	void ResetToRefPose(const TArray<FBoneInfo>& RefSkeleton)
	{
		Bones.SetNum(RefSkeleton.Num());
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.Num(); ++BoneIndex)
			Bones[BoneIndex] = RefSkeleton[BoneIndex].LocalBindPose;
	}

private:
	TArray<FTransform> Bones;
};

// UE FBlendedCurve 자리. 커브를 구현하기 전까지 비어 있으며 블렌딩 시그니처를 과제 코드와 맞춘다.
struct FBlendedCurve
{
};
