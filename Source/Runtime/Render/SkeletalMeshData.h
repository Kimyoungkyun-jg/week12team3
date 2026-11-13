#pragma once

#include "Vertex.h"
#include "Render/ImportedMaterialProperties.h"
#include "Math/BVH.h"
#include "Math/Box.h"

struct FBoneInfo
{
	FString Name;
	int32 ParentIndex = -1;
	FMatrix LocalBindPose = FMatrix::Identity;
	FMatrix GlobalBindPose = FMatrix::Identity;
	FMatrix InverseBindPose = FMatrix::Identity;
};

struct FSkeletalMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	uint32 MaterialSlotIndex = 0;
};

struct FSkeletalMaterialSlot : FImportedMaterialProperties
{
	FString Name;
	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FString DiffuseTexturePath;
	FString NormalTexturePath{};
};

struct FSkeletalMeshData
{
	TArray<FSkeletalVertex> SkinnedVertices;
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TArray<FSkeletalMaterialSlot> MaterialSlots;
	TArray<FBoneInfo> Bones;
	FBox AABB;

	bool Validate(FString& OutError) const;

	void BuildTangentSpace();
};
