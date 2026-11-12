#pragma once

#include "Asset/RenderAsset.h"
#include "Math/Box.h"
#include "Vertex.h"
#include "SkeletalMeshData.h"
#include "MeshRenderData.h"
#include "Buffer.h"
#include "Material.h"
#include <unordered_map>
#include <vector>
#include "SkeletalMesh.generated.h"

struct FSkeletalMeshLODRenderData
{
	TArray<FVertex> BindVertices;
	TArray<FSkinWeight> SkinWeights;
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TUniquePtr<FIndexBuffer> IndexBuffer;
	FBox BindAABB;

	// GPU 스키닝 입력. 포즈와 무관하므로 에셋이 갖고 인스턴스끼리 공유한다
	TUniquePtr<FVertexBuffer> GpuSkinVertexBuffer;
	// 본마다 가중치가 있는 정점의 바인드 바운드. 정점이 없는 본은 Min > Max
	TArray<FBox> BoneBounds;
};

struct FSkeletalMeshRenderData
{
	TArray<FSkeletalMeshLODRenderData> LODRenderData;
};

UCLASS()
class USkeletalMesh : public URenderAsset
{
	GENERATED_BODY()
public:
	bool Build(FSkeletalMeshData&& ImportData, FString& OutError);

	const TArray<FBoneInfo>& GetRefSkeleton() const { return RefSkeleton; }
	int32 GetNumBones() const { return RefSkeleton.Num(); }
	int32 FindBoneIndex(const FString& Name) const;
	const FSkeletalMeshRenderData& GetRenderData() const { return RenderData; }
	const TArray<FSkeletalMaterialSlot>& GetMaterialSlots() const { return MaterialSlots; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

	TSharedPtr<const FMeshRenderState> GetRenderState(const TArray<UMaterial*>& Materials);

private:
	struct FMaterialSlotsHash
	{
		size_t operator()(const TArray<UMaterial*>& Slots) const;
	};

	TArray<FBoneInfo> RefSkeleton;
	TArray<FSkeletalMaterialSlot> MaterialSlots;
	TArray<UMaterial*> Materials;
	FSkeletalMeshRenderData RenderData;
	// UStaticMesh와 같은 캐시: 같은 머티리얼 조합의 컴포넌트끼리 섹션 목록 공유
	TMap<TArray<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;
};