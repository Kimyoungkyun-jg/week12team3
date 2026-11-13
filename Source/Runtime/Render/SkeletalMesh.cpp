#include "EnginePCH.h"
#include "Render/SkeletalMesh.h"
#include "Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    FBox MakeEmptyBounds()
    {
        const float Limit = std::numeric_limits<float>::max();
        return { FVector(Limit, Limit, Limit), FVector(-Limit, -Limit, -Limit) };
    }
}

USkeletalMesh::~USkeletalMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
    {
        Proxy->OnMeshDestroyed();
    }
    VertexBuffer = nullptr;
    IndexBuffer = nullptr;
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
    if (SlotIndex < Materials.size() && Materials[SlotIndex])
    {
        return Materials[SlotIndex];
    }
    return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

void USkeletalMesh::RebuildRenderData()
{
    ComputeSkinningSources.Reset();
    ++RenderDataRevision;
    RefSkeleton.BuildFromBones(MeshData.Bones);
	RefBasesInvMatrix.Reset();
	for (const auto& Bone : MeshData.Bones) RefBasesInvMatrix.Add(Bone.InverseBindPose);
    {
        // 행렬 -> TRS -> 행렬 왕복 오차. 크면 전단이 있거나 분해 규약이 어긋난 것이다.
        float MaxError = 0.0f;
        for (int32 Index = 0; Index < RefSkeleton.GetNum(); ++Index)
        {
            const FMatrix Rebuilt = RefSkeleton.RefBonePose[Index].ToMatrix();
            for (int32 Row = 0; Row < 4; ++Row)
                for (int32 Col = 0; Col < 4; ++Col)
                    MaxError = std::max(MaxError, std::abs(Rebuilt.M[Row][Col] - MeshData.Bones[Index].LocalBindPose.M[Row][Col]));
        }
        FString Error;
        if (RefSkeleton.GetNum() > 0 && !RefSkeleton.Validate(Error))
            HTR_LOG(Warning, "[Skeleton] {}: {}", GetPath(), Error);
        else if (MaxError > 1.0e-3f)
            HTR_LOG(Warning, "[Skeleton] {}: bind pose TRS round-trip error {:.6f}", GetPath(), MaxError);
#ifdef ENGINE_DEBUG
        else
            HTR_LOG(Info, "[Skeleton] {}: {} bones, TRS round-trip error {:.6f}", GetPath(), RefSkeleton.GetNum(), MaxError);
#endif
    }

    RenderBounds = MeshData.AABB;
    RebuildSkinningBounds();
    LocalLODSphere.Center = (RenderBounds.Min + RenderBounds.Max) * 0.5f;

    double RadiusSquared = 0.0;
    for (const FSkeletalVertex& Vertex : MeshData.SkinnedVertices)
    {
        const double X = static_cast<double>(Vertex.Position.X) - LocalLODSphere.Center.X;
        const double Y = static_cast<double>(Vertex.Position.Y) - LocalLODSphere.Center.Y;
        const double Z = static_cast<double>(Vertex.Position.Z) - LocalLODSphere.Center.Z;
        RadiusSquared = std::max(RadiusSquared, X * X + Y * Y + Z * Z);
    }

    LocalLODSphere.RadiusSquared = std::nextafter(
        static_cast<float>(RadiusSquared * (1.0 + 2e-5)), std::numeric_limits<float>::infinity());

    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
    {
        Proxy->OnMeshRenderDataChanged();
    }
}

FStructuredBuffer* USkeletalMesh::GetOrCreateComputeSkinningSource(uint32 LOD, ID3D11Device* Device)
{
    if (LOD >= GetLODCount()) return nullptr;
    ComputeSkinningSources.SetNum(GetLODCount());
    auto& Source = ComputeSkinningSources[LOD];
    if (Source) return Source.get();

    const auto& Vertices = GetMeshData(LOD).SkinnedVertices;
    if (Vertices.IsEmpty()) return nullptr;
    static_assert(sizeof(FSkeletalVertex) == 96 && offsetof(FSkeletalVertex, BoneIndices) == 64
        && offsetof(FSkeletalVertex, BoneWeights) == 80);

    const uint32 Count = static_cast<uint32>(Vertices.Num());
    D3D11_BUFFER_DESC Desc{};
    Desc.ByteWidth = Count * sizeof(FSkeletalVertex);
    Desc.Usage = D3D11_USAGE_IMMUTABLE;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    Desc.StructureByteStride = sizeof(FSkeletalVertex);
    auto Buffer = std::make_unique<FStructuredBuffer>(Device, Desc, Count, Vertices.GetData());
    if (!Buffer->GetSRV()) return nullptr;
    Source = std::move(Buffer);
    return Source.get();
}

void USkeletalMesh::RebuildSkinningBounds()
{
    const FBox EmptyBounds = MakeEmptyBounds();
    BoneInfluenceBounds.SetNum(MeshData.Bones.Num());
    for (FBox& Bounds : BoneInfluenceBounds)
        Bounds = EmptyBounds;
    UnweightedVertexBounds = EmptyBounds;
    MinSkinWeightSum = MaxSkinWeightSum = 1.0f;

    for (uint32 LOD = 0; LOD < GetLODCount(); ++LOD)
    {
        for (const FSkeletalVertex& Vertex : GetMeshData(LOD).SkinnedVertices)
        {
            const FBox Point{ Vertex.Position, Vertex.Position };
            float WeightSum = 0.0f;
            for (int32 Influence = 0; Influence < 4; ++Influence)
            {
                const int32 BoneIndex = Vertex.BoneIndices[Influence];
                const float Weight = Vertex.BoneWeights[Influence];
                if (BoneIndex < 0 || BoneIndex >= BoneInfluenceBounds.Num() || Weight <= 0.0f)
                    continue;
                BoneInfluenceBounds[BoneIndex].Expand(Point);
                WeightSum += Weight;
            }
            if (WeightSum > 0.0f)
            {
                MinSkinWeightSum = std::min(MinSkinWeightSum, WeightSum);
                MaxSkinWeightSum = std::max(MaxSkinWeightSum, WeightSum);
            }
            else
            {
                UnweightedVertexBounds.Expand(Point);
            }
        }
    }
}

const FBox* USkeletalMesh::GetBoneInfluenceBounds(int32 BoneIndex) const
{
    if (BoneIndex < 0 || BoneIndex >= BoneInfluenceBounds.Num())
        return nullptr;
    const FBox& Bounds = BoneInfluenceBounds[BoneIndex];
    return Bounds.Min.X <= Bounds.Max.X ? &Bounds : nullptr;
}

FBox USkeletalMesh::CalcSkinnedBounds(const TArray<FMatrix>& SkinningMatrices) const
{
    if (BoneInfluenceBounds.Num() != SkinningMatrices.Num())
        return MeshData.AABB;

    FBox Bounds = MakeEmptyBounds();
    for (int32 BoneIndex = 0; BoneIndex < BoneInfluenceBounds.Num(); ++BoneIndex)
    {
        if (const FBox* BoneBounds = GetBoneInfluenceBounds(BoneIndex))
            Bounds.Expand(BoneBounds->GetWorldAABB(SkinningMatrices[BoneIndex]));
    }

    if (Bounds.Min.X <= Bounds.Max.X)
    {
        // SkinVertex uses the original weights. Cover sums that differ from one.
        const FBox NormalizedBounds = Bounds;
        Bounds.Expand({ NormalizedBounds.Min * MinSkinWeightSum, NormalizedBounds.Max * MinSkinWeightSum });
        Bounds.Expand({ NormalizedBounds.Min * MaxSkinWeightSum, NormalizedBounds.Max * MaxSkinWeightSum });
    }
    if (UnweightedVertexBounds.Min.X <= UnweightedVertexBounds.Max.X)
        Bounds.Expand(UnweightedVertexBounds);
    return Bounds.Min.X <= Bounds.Max.X ? Bounds : MeshData.AABB;
}

void USkeletalMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void USkeletalMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
    {
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
    }
}
