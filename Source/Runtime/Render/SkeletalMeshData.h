#pragma once

#include "Render/Vertex.h"
#include "Math/Box.h"
#include "Math/Transform.h"
#include <Containers/Array.h>


struct FSkeletalMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	uint32 MaterialSlotIndex = 0;
	// GPU Skinning에서 Section별 Bone palette를 사용하면 BoneMap과 MaxBoneInfluences를 추가하고,
	// Weight의 BoneIndex 변환 및 Binary 저장 형식을 함께 변경해야 한다.
};

struct FSkeletalMaterialSlot
{
	FString Name;
	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FString DiffuseTexturePath;
	FString NormalTexturePath{};
};

struct FBoneInfo
{
	FString Name;
	int32 ParentIndex = -1; // 부모가 항상 앞 인덱스
	FTransform LocalBindPose; // 부모기준 위치
	FMatrix InverseBindPose; //컴포넌트 공간 바인드 역행렬
};

struct FSkinWeight
{
	// 현재 BoneIndex는 전체 Bones 배열의 번호다. Section별 GPU palette 도입 시 해당 Section의 번호로 재매핑한다.
	uint8 BoneIndex[MaxBoneInfluences] = {};
	float Weight[MaxBoneInfluences] = {};
};

struct FSkeletalMeshData
{
	TArray<FVertex> BindVertices;
	TArray<FSkinWeight> SkinWeight; // BindVertices의 크기와 동일(어떤 정점이 어떤 Bone의 영향을 받는지, 얼마나 영향을 받는지)
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TArray<FSkeletalMaterialSlot> MaterialSlots;
	TArray<FBoneInfo> Bones;
	FBox BindAABB;
	FString SourcePath;

	bool Validate(FString& OutError) const;
};

// B 전용 테스트 데이터: Z축으로 Segments칸, 칸마다 본 하나. 관절 ±20%는 두 본에 선형 가중치
FSkeletalMeshData MakeTestBoneStick(int32 Segments = 3, float SegmentLength = 100.0f);

