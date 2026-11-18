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

UMaterialInterface* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
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

	// 본별 주 가중치 정점 범위로 히트박스 캡슐을 미리 생성한다
	BoneHitCapsules.Reset();
	const int32 BoneCount = MeshData.Bones.Num();
	const TArray<FSkeletalVertex>& BaseVertices = GetMeshData(0).SkinnedVertices;
	if (BoneCount <= 0 || BaseVertices.IsEmpty())
		return;

	const FVector MeshExtent = MeshData.AABB.Max - MeshData.AABB.Min;
	const float MeshScaleRef = std::max({ MeshExtent.X, MeshExtent.Y, MeshExtent.Z, 0.001f });
	const float MinExtentThreshold = MeshScaleRef * 0.025f;
	const float MinRadius = MeshScaleRef * 0.015f;
	const float MaxRadius = MeshScaleRef * 0.25f;

	TArray<FBox> RawBoneBounds;
	RawBoneBounds.SetNum(BoneCount);
	for (FBox& Box : RawBoneBounds)
		Box = EmptyBounds;
	TArray<int32> RawVertCounts;
	RawVertCounts.SetNum(BoneCount);
	for (int32& Count : RawVertCounts)
		Count = 0;

	TArray<int32> PrimaryBones;
	PrimaryBones.SetNum(BaseVertices.Num());
	for (int32 VertIndex = 0; VertIndex < BaseVertices.Num(); ++VertIndex)
	{
		const FSkeletalVertex& Vertex = BaseVertices[VertIndex];
		int32 BestBone = -1;
		float BestWeight = 0.0f;
		for (int32 Influence = 0; Influence < 4; ++Influence)
		{
			const int32 BoneIndex = Vertex.BoneIndices[Influence];
			const float Weight = Vertex.BoneWeights[Influence];
			if (BoneIndex >= 0 && BoneIndex < BoneCount && Weight > BestWeight)
			{
				BestWeight = Weight;
				BestBone = BoneIndex;
			}
		}
		PrimaryBones[VertIndex] = BestBone;
		if (BestBone >= 0)
		{
			const FVector LocalPos = MeshData.Bones[BestBone].InverseBindPose.TransformPosition(Vertex.Position);
			RawBoneBounds[BestBone].Expand(FBox{ LocalPos, LocalPos });
			++RawVertCounts[BestBone];
		}
	}

	// 손가락이나 발가락처럼 아주 작은 하위 본은 부모 본으로 병합한다
	TArray<int32> TargetBones;
	TargetBones.SetNum(BoneCount);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		TargetBones[BoneIndex] = BoneIndex;

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		if (RawVertCounts[BoneIndex] == 0)
			continue;
		const FVector Extent = (RawBoneBounds[BoneIndex].Max - RawBoneBounds[BoneIndex].Min) * 0.5f;
		const float MaxExtent = std::max({ Extent.X, Extent.Y, Extent.Z });
		const int32 ParentIndex = MeshData.Bones[BoneIndex].ParentIndex;
		if (ParentIndex >= 0 && (RawVertCounts[BoneIndex] < 16 || MaxExtent < MinExtentThreshold))
		{
			int32 Ancestor = ParentIndex;
			int32 Steps = 0;
			while (Ancestor >= 0 && Ancestor < BoneCount && Steps < BoneCount)
			{
				if (TargetBones[Ancestor] == Ancestor && RawVertCounts[Ancestor] >= 16)
				{
					const FVector ParentExtent = (RawBoneBounds[Ancestor].Max - RawBoneBounds[Ancestor].Min) * 0.5f;
					if (std::max({ ParentExtent.X, ParentExtent.Y, ParentExtent.Z }) >= MinExtentThreshold)
						break;
				}
				const int32 NextParent = MeshData.Bones[Ancestor].ParentIndex;
				if (NextParent < 0)
					break;
				Ancestor = NextParent;
				++Steps;
			}
			if (Ancestor >= 0 && Ancestor < BoneCount && RawVertCounts[Ancestor] > 0)
				TargetBones[BoneIndex] = TargetBones[Ancestor];
		}
	}

	TArray<FBox> FinalBoneBounds;
	FinalBoneBounds.SetNum(BoneCount);
	for (FBox& Box : FinalBoneBounds)
		Box = EmptyBounds;
	TArray<int32> FinalVertCounts;
	FinalVertCounts.SetNum(BoneCount);
	for (int32& Count : FinalVertCounts)
		Count = 0;

	for (int32 VertIndex = 0; VertIndex < BaseVertices.Num(); ++VertIndex)
	{
		const int32 RawBone = PrimaryBones[VertIndex];
		if (RawBone < 0 || RawBone >= BoneCount)
			continue;
		const int32 TargetBone = TargetBones[RawBone];
		const FVector LocalPos = MeshData.Bones[TargetBone].InverseBindPose.TransformPosition(BaseVertices[VertIndex].Position);
		FinalBoneBounds[TargetBone].Expand(FBox{ LocalPos, LocalPos });
		++FinalVertCounts[TargetBone];
	}

	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		if (FinalVertCounts[BoneIndex] <= 0)
			continue;
		const FBox& Bounds = FinalBoneBounds[BoneIndex];
		if (Bounds.Min.X > Bounds.Max.X)
			continue;
		const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
		const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		int32 LongAxis = 0;
		if (Extent.Y > Extent[LongAxis]) LongAxis = 1;
		if (Extent.Z > Extent[LongAxis]) LongAxis = 2;
		const float PerpSum = (Extent.X + Extent.Y + Extent.Z) - Extent[LongAxis];
		const float Radius = std::clamp(0.5f * PerpSum, MinRadius, MaxRadius);
		const float HalfSeg = std::max(0.0f, Extent[LongAxis] - Radius);

		FBoneHitCapsule Capsule;
		Capsule.BoneIndex = BoneIndex;
		Capsule.LocalA = Center;
		Capsule.LocalB = Center;
		Capsule.LocalA[LongAxis] -= HalfSeg;
		Capsule.LocalB[LongAxis] += HalfSeg;
		Capsule.Radius = Radius;
		BoneHitCapsules.Add(Capsule);
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
