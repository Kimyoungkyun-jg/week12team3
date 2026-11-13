#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Animation/BoneTransform.h"

struct FBoneInfo;

struct FMeshBoneInfo
{
	FString Name;
	int32 ParentIndex = -1;
};

// 부모가 먼저 오는 메시 기준 본 테이블 (TEAM5 구조, UE FReferenceSkeleton과 같은 이름).
// RefBonePose는 부모 기준 로컬 자세다. 애니메이션 트랙은 본 이름으로 이 테이블에 연결하고,
// 포즈 블렌딩은 로컬 FBoneTransform 배열로 한 뒤 FillComponentSpaceTransforms로 행렬을 만든다.
struct FReferenceSkeleton
{
	TArray<FMeshBoneInfo> RefBoneInfo;
	TArray<FBoneTransform> RefBonePose;

	int32 GetNum() const { return RefBoneInfo.Num(); }
	int32 FindBoneIndex(const FString& Name) const;
	bool Validate(FString& OutError) const;
	bool GetModelSpaceMatrices(TArray<FMatrix>& Out, FString& Error) const;

	// 메시 임포트 결과(FBoneInfo의 LocalBindPose 행렬)에서 만든다.
	void BuildFromBones(const TArray<FBoneInfo>& Bones);

	// 로컬 포즈를 부모부터 누적해 컴포넌트 공간 행렬을 만든다. LocalPose 개수가 본 개수와 다르면 false
	bool FillComponentSpaceTransforms(const TArray<FBoneTransform>& LocalPose, TArray<FMatrix>& OutComponentSpace) const;
};
