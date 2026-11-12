#include "EnginePCH.h"
#include "SkeletalMesh.h"
#include "Render/RenderCommand.h"
#include "Asset/AssetManager.h"

namespace
{
	TArray<FBox> BuildBoneBounds(const FSkeletalMeshLODRenderData& LOD, int32 NumBones)
	{
		TArray<FBox> Bounds;
		Bounds.SetNum(NumBones);
		for (FBox& Box : Bounds)
			Box = FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };

		for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
		{
			const FVector& Position = LOD.BindVertices[V].Position;
			const FSkinWeight& Weight = LOD.SkinWeights[V];
			for (int32 K = 0; K < MaxBoneInfluences; ++K)
			{
				if (Weight.Weight[K] <= 0.0f) continue;
				FBox& Box = Bounds[Weight.BoneIndex[K]];
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					Box.Min[Axis] = std::min(Box.Min[Axis], Position[Axis]);
					Box.Max[Axis] = std::max(Box.Max[Axis], Position[Axis]);
				}
			}
		}
		return Bounds;
	}
}

bool USkeletalMesh::Build(FSkeletalMeshData&& ImportData, FString& OutError)
{
	// CreateStaticMesh와 같은 기본값: 슬롯/섹션이 없으면 하나씩 채운다
	if (ImportData.MaterialSlots.IsEmpty())
	{
		FSkeletalMaterialSlot DefaultSlot;
		DefaultSlot.Name = "Default";
		ImportData.MaterialSlots.Add(DefaultSlot);
	}
	if (ImportData.Sections.IsEmpty())
	{
		FSkeletalMeshSection DefaultSection;
		DefaultSection.StartIndex = 0;
		DefaultSection.IndexCount = static_cast<uint32>(ImportData.Indices.Num());
		DefaultSection.MaterialSlotIndex = 0;
		ImportData.Sections.Add(DefaultSection);
	}

	if (!ImportData.Validate(OutError))
	{
		return false;
	}

	// 공유 IB. VB는 인스턴스마다 FSkeletalMeshObjectCPUSkin이 동적으로 만든다
	TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(ImportData.Indices.GetData(), static_cast<uint32>(ImportData.Indices.Num()));
	if (!IB)
	{
		OutError = "SkeletalMesh: index buffer creation failed";
		return false;
	}

	TArray<UMaterial*> NewMaterials;
	if (!UAssetManager::BuildMaterialsFromSlots(ImportData.MaterialSlots, NewMaterials))
	{
		OutError = "SkeletalMesh: material creation failed";
		return false;
	}

	FSkeletalMeshLODRenderData LOD;
	LOD.BindVertices = std::move(ImportData.BindVertices);
	LOD.SkinWeights = std::move(ImportData.SkinWeight);
	LOD.Indices = std::move(ImportData.Indices);
	LOD.Sections = std::move(ImportData.Sections);
	LOD.IndexBuffer = std::move(IB);

	// 임포터가 채운 값에 의존하지 않고 바인드 정점에서 다시 계산
	LOD.BindAABB.Min = LOD.BindAABB.Max = LOD.BindVertices[0].Position;
	for (const FVertex& Vertex : LOD.BindVertices)
	{
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			LOD.BindAABB.Min[Axis] = std::min(LOD.BindAABB.Min[Axis], Vertex.Position[Axis]);
			LOD.BindAABB.Max[Axis] = std::max(LOD.BindAABB.Max[Axis], Vertex.Position[Axis]);
		}
	}

	TArray<FGpuSkinVertex> GpuSkinVertices;
	GpuSkinVertices.SetNum(LOD.BindVertices.Num());
	for (int32 V = 0; V < GpuSkinVertices.Num(); ++V)
	{
		GpuSkinVertices[V].Vertex = LOD.BindVertices[V];
		for (int32 K = 0; K < MaxBoneInfluences; ++K)
		{
			GpuSkinVertices[V].BoneIndex[K] = LOD.SkinWeights[V].BoneIndex[K];
			GpuSkinVertices[V].Weight[K] = LOD.SkinWeights[V].Weight[K];
		}
	}
	LOD.GpuSkinVertexBuffer = RenderCommand::CreateStaticVertexBuffer(GpuSkinVertices.GetData(),
		sizeof(FGpuSkinVertex) * static_cast<uint32>(GpuSkinVertices.Num()), sizeof(FGpuSkinVertex));
	LOD.BoneBounds = BuildBoneBounds(LOD, ImportData.Bones.Num());

	RefSkeleton = std::move(ImportData.Bones);
	MaterialSlots = std::move(ImportData.MaterialSlots);
	Materials = std::move(NewMaterials);
	RenderData.LODRenderData.Empty();
	RenderData.LODRenderData.Add(std::move(LOD));
	RenderStates.Reset();
	return true;
}

int32 USkeletalMesh::FindBoneIndex(const FString& Name) const
{
	for (int32 I = 0; I < RefSkeleton.Num(); ++I)
	{
		if (RefSkeleton[I].Name == Name)
			return I;
	}
	return -1;
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t USkeletalMesh::FMaterialSlotsHash::operator()(const TArray<UMaterial*>& Slots) const
{
	size_t Hash = 0;
	for (const UMaterial* Material : Slots)
		Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	return Hash;
}

TSharedPtr<const FMeshRenderState> USkeletalMesh::GetRenderState(const TArray<UMaterial*>& InMaterials)
{
	auto& WeakState = RenderStates[InMaterials];
	if (auto State = WeakState.lock()) return State;

	// LOD는 하나뿐이라 임계값 없이 LOD 0 범위만 채운다
	auto State = MakeShared<FMeshRenderState>();
	State->LODCount = 1;
	if (!RenderData.LODRenderData.IsEmpty())
	{
		auto& Range = State->LODs[0];
		Range.FirstSection = 0;
		for (const FSkeletalMeshSection& Section : RenderData.LODRenderData[0].Sections)
		{
			UMaterial* Material = Section.MaterialSlotIndex < InMaterials.size()
				? InMaterials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
			if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
		}
		Range.NumSections = State->Sections.Num();
	}
	WeakState = State;
	return State;
}
