#include "EnginePCH.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Render/Material.h"
#include "Render/MeshBatch.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include <algorithm>
#include <cmath>

namespace
{
	const FCachedMeshLOD GEmptyMeshLOD{};
	const FCachedMeshSection GEmptyMeshSection{};
}

// ---------------- FPrimitiveSceneProxy ----------------

void FPrimitiveSceneProxy::ApplyTransformData(const FPrimitiveTransformData& Data)
{
	LocalToWorld = Data.LocalToWorld;
	WorldToLocal = Data.WorldToLocal;
	Bounds = Data.WorldBounds;
	WorldScale3D = Data.WorldScale3D;
	UpdateLODSphere();
}

void FPrimitiveSceneProxy::ApplyBaseRenderState(const FPrimitiveRenderStateData& Data)
{
	bVisible = Data.bVisible;
	bShownInGame = Data.bShownInGame;
	bShownInEditor = Data.bShownInEditor;
	bCastsShadow = Data.bCastsShadow;
	bReceiveShadow = Data.bReceiveShadow;
	bReceiveDecal = Data.bReceiveDecal;
}

bool FPrimitiveSceneProxy::IsShown(bool bGameView, bool bShowSkeletalMeshes) const
{
	if (bIsSkeletalMesh && !bShowSkeletalMeshes)
		return false;
	return bVisible && (bGameView ? bShownInGame : bShownInEditor);
}

const FCachedMeshLOD& FPrimitiveSceneProxy::GetLOD(uint32 LODIndex) const
{
	const FMeshRenderState* State = GetRenderState();
	if (!State || LODIndex >= State->LODCount)
		return GEmptyMeshLOD;
	return State->LODs[LODIndex];
}

const FCachedMeshSection& FPrimitiveSceneProxy::GetSection(uint32 SectionIndex) const
{
	const FMeshRenderState* State = GetRenderState();
	if (!State || SectionIndex >= static_cast<uint32>(State->Sections.Num()))
		return GEmptyMeshSection;
	return State->Sections[SectionIndex];
}

void FPrimitiveSceneProxy::UpdateLODSphere()
{
	LODSphere.Center = LocalToWorld.TransformPosition(LocalLODSphere.Center);
	const float MaxScale = std::max({ std::abs(WorldScale3D.X), std::abs(WorldScale3D.Y), std::abs(WorldScale3D.Z) });
	LODSphere.RadiusSquared = LocalLODSphere.RadiusSquared * MaxScale * MaxScale;
}

// ---------------- FStaticMeshSceneProxy ----------------

FStaticMeshSceneProxy::FStaticMeshSceneProxy(const FStaticMeshProxyData& InitialData, const FPrimitiveTransformData& InitialTransform)
{
	SetStaticMeshData(InitialData);
	ApplyTransformData(InitialTransform);
}

FStaticMeshSceneProxy::~FStaticMeshSceneProxy()
{
	if (StaticMesh)
		StaticMesh->UnregisterProxy(this);
}

void FStaticMeshSceneProxy::SetStaticMeshData(const FStaticMeshProxyData& Data)
{
	if (StaticMesh != Data.StaticMesh)
	{
		if (StaticMesh)
			StaticMesh->UnregisterProxy(this);
		StaticMesh = Data.StaticMesh;
		if (StaticMesh)
			StaticMesh->RegisterProxy(this);
	}

	ApplyBaseRenderState(Data.BaseState);
	RenderState = Data.RenderState;

	if (StaticMesh)
		LocalLODSphere = StaticMesh->GetLocalLODSphere();
	else
		LocalLODSphere = FLODSphere{};

	UpdateLODSphere();
}

void FStaticMeshSceneProxy::OnMeshRenderDataChanged()
{
	if (StaticMesh)
	{
		LocalLODSphere = StaticMesh->GetLocalLODSphere();
		UpdateLODSphere();
	}
	if (Scene)
	{
		Scene->MarkDirty(this);
		Scene->MarkRenderStateDirty(this);
	}
}

void FStaticMeshSceneProxy::OnMeshDestroyed()
{
	StaticMesh = nullptr;
	RenderState.reset();
	bVisible = false;
}

void FStaticMeshSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) const
{
	if (!PDI || !bVisible || !StaticMesh || PackedIndex < 0)
		return;

	const uint32 PrimIdx = static_cast<uint32>(PackedIndex);
	auto EmitStaticSection = [&](uint8 ClampedLOD, FVertexBuffer* VB, FIndexBuffer* IB,
		UMaterial* SectionMat, uint32 StartIndex, uint32 IndexCount)
	{
		if (!VB || !IB || !SectionMat)
			return;
		FStaticMeshBatch Batch{};
		Batch.PrimitiveSceneProxy = this;
		Batch.PrimitiveIndex = PrimIdx;
		Batch.VertexFactory.Type = EVertexFactoryType::Local;
		Batch.VertexFactory.VertexBuffer = VB;
		Batch.Material = SectionMat;
		Batch.MeshSortID = StaticMesh->SortID;
		Batch.LODIndex = ClampedLOD;
		Batch.bCastShadow = bCastsShadow;

		FMeshBatchElement Element{};
		Element.Proxy = this;
		Element.IndexBuffer = IB;
		Element.FirstIndex = StartIndex;
		Element.IndexCount = IndexCount;
		Element.PrimitiveId = PrimIdx;
		Element.bReceiveShadow = bReceiveShadow;
		Element.bReceiveDecal = bReceiveDecal;
		Batch.Elements.Add(Element);
		PDI->DrawMesh(Batch);
	};

	if (RenderState && RenderState->LODCount > 0) // RenderState 는 컴포넌트가 만든 FMeshRenderState
	{
		for (uint32 LOD = 0; LOD < RenderState->LODCount; ++LOD)
		{
			FVertexBuffer* VB = StaticMesh->GetVertexBuffer(LOD);
			FIndexBuffer* IB = StaticMesh->GetIndexBuffer(LOD);
			const FCachedMeshLOD& CachedLOD = RenderState->LODs[LOD];
			for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
			{
				const FCachedMeshSection& Section = RenderState->Sections[CachedLOD.FirstSection + i];
				EmitStaticSection(static_cast<uint8>(LOD), VB, IB, Section.Material, Section.StartIndex, Section.IndexCount);
			}
		}
		return;
	}

	const uint32 NumMeshLODs = StaticMesh->GetLODCount();
	for (uint32 LOD = 0; LOD < NumMeshLODs; ++LOD) // StaticMesh 에셋 원본의 MeshData.Sections와 기본 머티리얼을 직접 순회
	{
		FVertexBuffer* VB = StaticMesh->GetVertexBuffer(LOD);
		FIndexBuffer* IB = StaticMesh->GetIndexBuffer(LOD);
		const FStaticMeshData& MeshData = StaticMesh->GetMeshData(LOD);
		if (MeshData.Sections.IsEmpty())
		{
			EmitStaticSection(static_cast<uint8>(LOD), VB, IB, StaticMesh->GetMaterial(0), 0, 0);
			continue;
		}
		for (const FStaticMeshSection& Section : MeshData.Sections)
		{
			EmitStaticSection(static_cast<uint8>(LOD), VB, IB, StaticMesh->GetMaterial(Section.MaterialSlotIndex),
				Section.StartIndex, Section.IndexCount);
		}
	}
}

// ---------------- FSkeletalMeshSceneProxy ----------------

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(const FSkeletalMeshProxyData& InitialData, const FPrimitiveTransformData& InitialTransform)
{
	bIsSkeletalMesh = true;
	SetSkeletalMeshData(InitialData);
	ApplyTransformData(InitialTransform);
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
	if (SkeletalMesh)
		SkeletalMesh->UnregisterProxy(this);
}

void FSkeletalMeshSceneProxy::SetSkeletalMeshData(const FSkeletalMeshProxyData& Data)
{
	if (SkeletalMesh != Data.SkeletalMesh)
	{
		if (SkeletalMesh)
			SkeletalMesh->UnregisterProxy(this);
		SkeletalMesh = Data.SkeletalMesh;
		if (SkeletalMesh)
			SkeletalMesh->RegisterProxy(this);
	}

	ApplyBaseRenderState(Data.BaseState);
	RenderState = Data.RenderState;
	AppliedMode = Data.AppliedMode;
	CPUSkinnedVertexBuffer = Data.CPUSkinnedVertexBuffer;
	SkinningMatricesBuffer = Data.SkinningMatricesBuffer;
	bBoneWeightHeatmap = Data.bBoneWeightHeatmap;

	if (SkeletalMesh)
		LocalLODSphere = SkeletalMesh->GetLocalLODSphere();
	else
		LocalLODSphere = FLODSphere{};

	UpdateLODSphere();
}

void FSkeletalMeshSceneProxy::SetSkinningData(uint8 InAppliedMode, FVertexBuffer* InCPUVertexBuffer, FStructuredBuffer* InMatricesBuffer, bool bInHeatmap)
{
	AppliedMode = InAppliedMode;
	CPUSkinnedVertexBuffer = InCPUVertexBuffer;
	SkinningMatricesBuffer = InMatricesBuffer;
	bBoneWeightHeatmap = bInHeatmap;
}

void FSkeletalMeshSceneProxy::OnMeshRenderDataChanged()
{
	if (SkeletalMesh)
	{
		LocalLODSphere = SkeletalMesh->GetLocalLODSphere();
		UpdateLODSphere();
	}
	if (Scene)
	{
		Scene->MarkDirty(this);
		Scene->MarkRenderStateDirty(this);
	}
}

void FSkeletalMeshSceneProxy::OnMeshDestroyed()
{
	SkeletalMesh = nullptr;
	RenderState.reset();
	CPUSkinnedVertexBuffer = nullptr;
	SkinningMatricesBuffer = nullptr;
	bVisible = false;
}

void FSkeletalMeshSceneProxy::GetDynamicMeshElements(const FViewContext& ViewContext, FMeshElementCollector& Collector, uint32 LODIndex) const
{
	if (!bVisible || !SkeletalMesh)
		return;

	const ESkinningMode Mode = static_cast<ESkinningMode>(AppliedMode);
	const bool bUseGPUSkinning = (Mode != ESkinningMode::CPU) && (SkinningMatricesBuffer != nullptr);
	const uint8 ClampedLOD = static_cast<uint8>(bUseGPUSkinning && RenderState && RenderState->LODCount > 0
		? std::min(LODIndex, static_cast<uint32>(RenderState->LODCount) - 1u)
		: 0u);
	FVertexBuffer* VB = bUseGPUSkinning ? SkeletalMesh->GetVertexBuffer(ClampedLOD) : CPUSkinnedVertexBuffer;
	FIndexBuffer* IB = SkeletalMesh->GetIndexBuffer(ClampedLOD);
	if (!VB || !IB)
		return;

	UMaterial* HeatmapMaterial = bBoneWeightHeatmap ? UAssetManager::GetAssetByPath<UMaterial>("BoneWeightHeatmapMaterial") : nullptr;
	const FVector Delta = Bounds.Center - ViewContext.CameraPosition;
	const float DistanceSq = Delta.Dot(Delta);

	auto EmitSectionBatch = [&](UMaterial* SectionMat, uint32 StartIndex, uint32 IndexCount)
	{
		if (!SectionMat)
			return;
		FMeshBatch Batch{};
		Batch.VertexFactory.Type = bUseGPUSkinning ? EVertexFactoryType::GPUSkin : EVertexFactoryType::Local;
		Batch.VertexFactory.VertexBuffer = VB;
		Batch.VertexFactory.SkinningMatricesBuffer = bUseGPUSkinning ? SkinningMatricesBuffer : nullptr;
		Batch.VertexFactory.bComputeSkinned = (Mode == ESkinningMode::Compute) && bUseGPUSkinning;
		Batch.Material = SectionMat;
		Batch.MeshSortID = SkeletalMesh->SortID;
		Batch.LODIndex = ClampedLOD;
		Batch.bCastShadow = bCastsShadow;
		Batch.CameraDistanceSq = DistanceSq;

		FMeshBatchElement Element{};
		Element.Proxy = this;
		Element.IndexBuffer = IB;
		Element.FirstIndex = StartIndex;
		Element.IndexCount = IndexCount;
		Element.PrimitiveId = PackedIndex >= 0 ? static_cast<uint32>(PackedIndex) : InvalidPrimitiveId;
		Element.bReceiveShadow = bReceiveShadow;
		Element.bReceiveDecal = bReceiveDecal;
		Batch.Elements.Add(Element);
		Collector.AddMesh(std::move(Batch));
	};

	if (RenderState && RenderState->LODCount > 0)
	{
		const FCachedMeshLOD& CachedLOD = RenderState->LODs[ClampedLOD];
		for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
		{
			const FCachedMeshSection& Section = RenderState->Sections[CachedLOD.FirstSection + i];
			EmitSectionBatch(HeatmapMaterial ? HeatmapMaterial : Section.Material, Section.StartIndex, Section.IndexCount);
		}
		return;
	}

	const auto& Sections = SkeletalMesh->MeshData.Sections;
	if (Sections.IsEmpty())
	{
		EmitSectionBatch(HeatmapMaterial ? HeatmapMaterial : SkeletalMesh->GetMaterial(0), 0, 0);
		return;
	}

	for (const FSkeletalMeshSection& Section : Sections)
	{
		EmitSectionBatch(HeatmapMaterial ? HeatmapMaterial : SkeletalMesh->GetMaterial(Section.MaterialSlotIndex),
			Section.StartIndex, Section.IndexCount);
	}
}

// ---------------- FBillboardSceneProxy ----------------

FBillboardSceneProxy::FBillboardSceneProxy(const FBillboardProxyData& InitialData, const FPrimitiveTransformData& InitialTransform)
{
	SetBillboardData(InitialData);
	ApplyTransformData(InitialTransform);
}

void FBillboardSceneProxy::SetBillboardData(const FBillboardProxyData& Data)
{
	ApplyBaseRenderState(Data.BaseState);
	QuadMesh = Data.QuadMesh;
	Material = Data.Material;
	IconParams = Data.IconParams;
	IconSize = Data.IconSize;
	bEditorIcon = Data.bEditorIcon;
	bSubUVEmitter = Data.bSubUVEmitter;
	bActive = Data.bActive;
	bOpaqueMaterial = Data.bOpaqueMaterial;
	ColSize = std::max(1u, Data.ColSize);
	RowSize = std::max(1u, Data.RowSize);
	SubUVParticles = Data.SubUVParticles;
}

void FBillboardSceneProxy::GetDynamicMeshElements(const FViewContext& ViewContext, FMeshElementCollector& Collector, uint32 LODIndex) const
{
	if (!bVisible || !QuadMesh || !Material)
		return;

	FVertexBuffer* VB = QuadMesh->GetVertexBuffer(0);
	FIndexBuffer* IB = QuadMesh->GetIndexBuffer(0);
	if (!VB || !IB)
		return;

	if (!bSubUVEmitter)
	{
		if (bEditorIcon && ViewContext.bGameView)
			return;

		const float Width = bEditorIcon ? IconSize : WorldScale3D.Y;
		const float Height = bEditorIcon ? IconSize : WorldScale3D.Z;
		const FVector WorldLocation(LocalToWorld.M[3][0], LocalToWorld.M[3][1], LocalToWorld.M[3][2]);
		const FMatrix WorldMatrix = ViewContext.BuildBillboardMatrix(WorldLocation, Width, Height);
		const FVector Delta = WorldLocation - ViewContext.CameraPosition;

		FMeshBatch Batch{};
		Batch.VertexFactory.Type = EVertexFactoryType::Local;
		Batch.VertexFactory.VertexBuffer = VB;
		Batch.Material = Material;
		Batch.MaterialParamData = &IconParams;
		Batch.MaterialParamDataSize = sizeof(IconParams);
		Batch.MeshSortID = QuadMesh->SortID;
		Batch.CameraDistanceSq = Delta.Dot(Delta);
		Batch.bCastShadow = false;

		FMeshBatchElement Element{};
		Element.Proxy = this;
		Element.IndexBuffer = IB;
		Element.WorldMatrix = Collector.StoreWorldMatrix(WorldMatrix);
		Element.bReceiveShadow = false;
		Element.bReceiveDecal = false;
		Batch.Elements.Add(Element);
		Collector.AddMesh(std::move(Batch));
		return;
	}

	if (!bActive || SubUVParticles.IsEmpty())
		return;

	struct FParticleSortItem
	{
		int32 Index;
		float DistSq;
	};

	TArray<FParticleSortItem> SortList;
	SortList.Reserve(SubUVParticles.Num());
	for (int32 i = 0; i < SubUVParticles.Num(); ++i)
	{
		const FBillboardSubUVParticleData& P = SubUVParticles[i];
		if (!P.bAlive)
			continue;
		const FVector Delta = P.Location - ViewContext.CameraPosition;
		SortList.Add({ i, Delta.Dot(Delta) });
	}

	if (SortList.IsEmpty())
		return;

	if (!bOpaqueMaterial)
	{
		std::sort(SortList.begin(), SortList.end(),
			[](const FParticleSortItem& A, const FParticleSortItem& B) { return A.DistSq > B.DistSq; });
	}

	CachedSubUVConstants.Reset();
	CachedSubUVConstants.Reserve(SortList.Num());

	for (const FParticleSortItem& Item : SortList)
	{
		const FBillboardSubUVParticleData& P = SubUVParticles[Item.Index];
		const FMatrix WorldMatrix = ViewContext.BuildBillboardMatrix(P.Location, P.Scale, P.Scale);

		FBillboardSubUVConstants& Param = CachedSubUVConstants.AddDefaulted_GetRef();
		Param.CurrentFrame = static_cast<float>(P.SubUVFrame);
		Param.RowSize = static_cast<float>(RowSize);
		Param.ColSize = static_cast<float>(ColSize);
		Param.Alpha = P.Alpha;

		FMeshBatch Batch{};
		Batch.VertexFactory.Type = EVertexFactoryType::Local;
		Batch.VertexFactory.VertexBuffer = VB;
		Batch.Material = Material;
		Batch.MaterialParamData = &Param;
		Batch.MaterialParamDataSize = sizeof(Param);
		Batch.MeshSortID = QuadMesh->SortID;
		Batch.CameraDistanceSq = Item.DistSq;
		Batch.bCastShadow = false;

		FMeshBatchElement Element{};
		Element.Proxy = this;
		Element.IndexBuffer = IB;
		Element.WorldMatrix = Collector.StoreWorldMatrix(WorldMatrix);
		Element.bReceiveShadow = false;
		Element.bReceiveDecal = false;
		Batch.Elements.Add(Element);
		Collector.AddMesh(std::move(Batch));
	}
}
