#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"
#include "Render/MeshRenderData.h"
#include "Animation/ReferenceSkeleton.h"
#include <unordered_map>
#include <vector>
#include <array>
#include "SkeletalMesh.generated.h"

class FPrimitiveSceneProxy;
class USkeleton;

struct FSkeletalMeshLODResource
{
	FSkeletalMeshData Data;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
};

UCLASS()
class USkeletalMesh : public URenderAsset
{
    GENERATED_BODY()

public:
    USkeletalMesh() : SortID(NextSortID++) {}
    virtual ~USkeletalMesh() override;

    FSkeletalMeshData MeshData;
	TArray<FSkeletalMeshLODResource> AdditionalLODs;
	std::array<float, 3> ScreenThresholds{ .30f, .10f, .03f };
	USkeleton* Skeleton = nullptr;
	TArray<FMatrix> RefBasesInvMatrix;
	uint32 GetLODCount() const { return 1u + static_cast<uint32>(AdditionalLODs.Num()); }
	const FSkeletalMeshData& GetMeshData(uint32 LOD) const { return LOD == 0 ? MeshData : AdditionalLODs[LOD - 1].Data; }
	FVertexBuffer* GetVertexBuffer(uint32 LOD) const { return LOD == 0 ? VertexBuffer.get() : AdditionalLODs[LOD - 1].VertexBuffer.get(); }
	FIndexBuffer* GetIndexBuffer(uint32 LOD) const { return LOD == 0 ? IndexBuffer.get() : AdditionalLODs[LOD - 1].IndexBuffer.get(); }

    // MeshData.Bones에서 만든 TRS 본 테이블. 애니메이션 포즈 계산은 이것을 쓴다. RebuildRenderData가 갱신한다.
    FReferenceSkeleton RefSkeleton;
    const FReferenceSkeleton& GetRefSkeleton() const { return RefSkeleton; }

    // Renderer가 바인드할 런타임 머티리얼 목록
	TArray<UMaterialInterface*> Materials;

    TUniquePtr<FVertexBuffer> VertexBuffer;
    TUniquePtr<FIndexBuffer> IndexBuffer;

    const FSkeletalMeshData& GetMeshData() const { return MeshData; }
	UMaterialInterface* GetMaterial(uint32 SlotIndex) const;

    void RebuildRenderData();
    const FLODSphere& GetLocalLODSphere() const { return LocalLODSphere; }
    const FBox& GetRenderBounds() const { return RenderBounds; }
    FBox CalcSkinnedBounds(const TArray<FMatrix>& SkinningMatrices) const;
    const FBox* GetBoneInfluenceBounds(int32 BoneIndex) const;
    FStructuredBuffer* GetOrCreateComputeSkinningSource(uint32 LOD, ID3D11Device* Device);
    uint64 GetRenderDataRevision() const { return RenderDataRevision; }
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

    const uint16 SortID;

private:
    static inline uint16 NextSortID = 0;

    FLODSphere LocalLODSphere;
    FBox RenderBounds;
    TArray<TUniquePtr<FStructuredBuffer>> ComputeSkinningSources;
    uint64 RenderDataRevision = 0;
    void RebuildSkinningBounds();
    TArray<FBox> BoneInfluenceBounds;
    FBox UnweightedVertexBounds;
    float MinSkinWeightSum = 1.0f;
    float MaxSkinWeightSum = 1.0f;
    TArray<FPrimitiveSceneProxy*> RenderProxies;
};
