#include "EnginePCH.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Render/Material.h"
#include "Render/RenderPacket.h"
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
	bEditorIcon = Data.bEditorIcon;

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

void FStaticMeshSceneProxy::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext, uint32 LODIndex) const
{
	if (!bVisible || !StaticMesh)
		return;

	if (RenderState && RenderState->LODCount > 0)
	{
		const uint32 ClampedLOD = std::min(LODIndex, static_cast<uint32>(RenderState->LODCount) - 1u);
		const FCachedMeshLOD& CachedLOD = RenderState->LODs[ClampedLOD];
		for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
		{
			const FCachedMeshSection& Section = RenderState->Sections[CachedLOD.FirstSection + i];
			FRenderPacket Packet;
			Packet.Proxy = this;
			Packet.Mesh = StaticMesh;
			Packet.Material = Section.Material;
			Packet.StartIndex = Section.StartIndex;
			Packet.IndexCount = Section.IndexCount;
			Packet.LODIndex = static_cast<uint8>(ClampedLOD);
			RenderQueue.Add(Packet);
		}
		return;
	}

	const uint32 ClampedLOD = std::min(LODIndex, StaticMesh->GetLODCount() - 1u);
	const FStaticMeshData& MeshData = StaticMesh->GetMeshData(ClampedLOD);
	if (MeshData.Sections.IsEmpty())
	{
		FRenderPacket Packet;
		Packet.Proxy = this;
		Packet.Mesh = StaticMesh;
		Packet.Material = StaticMesh->GetMaterial(0);
		Packet.LODIndex = static_cast<uint8>(ClampedLOD);
		RenderQueue.Add(Packet);
		return;
	}

	for (const FStaticMeshSection& Section : MeshData.Sections)
	{
		FRenderPacket Packet;
		Packet.Proxy = this;
		Packet.Mesh = StaticMesh;
		Packet.Material = StaticMesh->GetMaterial(Section.MaterialSlotIndex);
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = static_cast<uint8>(ClampedLOD);
		RenderQueue.Add(Packet);
	}
}

FVertexBuffer* FStaticMeshSceneProxy::GetVertexBuffer(uint8 LOD) const
{
	return StaticMesh ? StaticMesh->GetVertexBuffer(LOD) : nullptr;
}

FIndexBuffer* FStaticMeshSceneProxy::GetIndexBuffer(uint8 LOD) const
{
	return StaticMesh ? StaticMesh->GetIndexBuffer(LOD) : nullptr;
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

void FSkeletalMeshSceneProxy::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext, uint32 LODIndex) const
{
	if (!bVisible || !SkeletalMesh)
		return;

	const ESkinningMode Mode = static_cast<ESkinningMode>(AppliedMode);
	const bool bUseGPUSkinning = (Mode != ESkinningMode::CPU) && (SkinningMatricesBuffer != nullptr);
	const uint32 ClampedLOD = bUseGPUSkinning && RenderState && RenderState->LODCount > 0
		? std::min(LODIndex, static_cast<uint32>(RenderState->LODCount) - 1u)
		: 0u;
	FVertexBuffer* CPUVB = !bUseGPUSkinning ? CPUSkinnedVertexBuffer : nullptr;
	UMaterial* HeatmapMaterial = bBoneWeightHeatmap ? UAssetManager::GetAssetByPath<UMaterial>("BoneWeightHeatmapMaterial") : nullptr;

	if (RenderState && RenderState->LODCount > 0)
	{
		const FCachedMeshLOD& CachedLOD = RenderState->LODs[ClampedLOD];
		for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
		{
			const FCachedMeshSection& Section = RenderState->Sections[CachedLOD.FirstSection + i];
			FRenderPacket Packet;
			Packet.Proxy = this;
			Packet.SkeletalMesh = SkeletalMesh;
			Packet.SkeletalVertexBuffer = CPUVB;
			Packet.SkinningMatricesBuffer = bUseGPUSkinning ? SkinningMatricesBuffer : nullptr;
			Packet.bComputeSkinned = (Mode == ESkinningMode::Compute) && bUseGPUSkinning;
			Packet.Material = HeatmapMaterial ? HeatmapMaterial : Section.Material;
			Packet.StartIndex = Section.StartIndex;
			Packet.IndexCount = Section.IndexCount;
			Packet.LODIndex = static_cast<uint8>(ClampedLOD);
			RenderQueue.Add(Packet);
		}
		return;
	}

	const auto& Sections = SkeletalMesh->MeshData.Sections;
	if (Sections.IsEmpty())
	{
		FRenderPacket Packet;
		Packet.Proxy = this;
		Packet.SkeletalMesh = SkeletalMesh;
		Packet.SkeletalVertexBuffer = CPUVB;
		Packet.SkinningMatricesBuffer = bUseGPUSkinning ? SkinningMatricesBuffer : nullptr;
		Packet.bComputeSkinned = (Mode == ESkinningMode::Compute) && bUseGPUSkinning;
		Packet.Material = HeatmapMaterial ? HeatmapMaterial : SkeletalMesh->GetMaterial(0);
		Packet.LODIndex = static_cast<uint8>(ClampedLOD);
		RenderQueue.Add(Packet);
		return;
	}

	for (const FSkeletalMeshSection& Section : Sections)
	{
		FRenderPacket Packet;
		Packet.Proxy = this;
		Packet.SkeletalMesh = SkeletalMesh;
		Packet.SkeletalVertexBuffer = CPUVB;
		Packet.SkinningMatricesBuffer = bUseGPUSkinning ? SkinningMatricesBuffer : nullptr;
		Packet.bComputeSkinned = (Mode == ESkinningMode::Compute) && bUseGPUSkinning;
		Packet.Material = HeatmapMaterial ? HeatmapMaterial : SkeletalMesh->GetMaterial(Section.MaterialSlotIndex);
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = static_cast<uint8>(ClampedLOD);
		RenderQueue.Add(Packet);
	}
}

FVertexBuffer* FSkeletalMeshSceneProxy::GetVertexBuffer(uint8 LOD) const
{
	if (!SkeletalMesh)
		return nullptr;
	if (static_cast<ESkinningMode>(AppliedMode) == ESkinningMode::CPU && LOD == 0 && CPUSkinnedVertexBuffer)
		return CPUSkinnedVertexBuffer;
	return SkeletalMesh->GetVertexBuffer(LOD);
}

FIndexBuffer* FSkeletalMeshSceneProxy::GetIndexBuffer(uint8 LOD) const
{
	return SkeletalMesh ? SkeletalMesh->GetIndexBuffer(LOD) : nullptr;
}

bool FSkeletalMeshSceneProxy::IsComputeSkinned() const
{
	return static_cast<ESkinningMode>(AppliedMode) == ESkinningMode::Compute;
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

void FBillboardSceneProxy::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext, uint32 LODIndex) const
{
	if (!bVisible || !QuadMesh || !Material)
		return;

	if (!bSubUVEmitter)
	{
		if (bEditorIcon && ViewContext.bGameView)
			return;

		const float Width = bEditorIcon ? IconSize : WorldScale3D.Y;
		const float Height = bEditorIcon ? IconSize : WorldScale3D.Z;
		const FVector WorldLocation(LocalToWorld.M[3][0], LocalToWorld.M[3][1], LocalToWorld.M[3][2]);
		const FMatrix WorldMatrix = ViewContext.BuildBillboardMatrix(WorldLocation, Width, Height);

		FRenderPacket Packet;
		Packet.Proxy = this;
		Packet.Model = RenderQueue.StoreWorldMatrix(WorldMatrix);
		Packet.Mesh = QuadMesh;
		Packet.Material = Material;
		Packet.MaterialParamData = &IconParams;
		Packet.MaterialParamDataSize = sizeof(IconParams);
		RenderQueue.Add(Packet);
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

		FRenderPacket Packet;
		Packet.Proxy = this;
		Packet.Model = RenderQueue.StoreWorldMatrix(WorldMatrix);
		Packet.Mesh = QuadMesh;
		Packet.Material = Material;
		Packet.MaterialParamData = &Param;
		Packet.MaterialParamDataSize = sizeof(Param);
		Packet.CameraToParticleDistance = Item.DistSq;
		RenderQueue.Add(Packet);
	}
}

FVertexBuffer* FBillboardSceneProxy::GetVertexBuffer(uint8 LOD) const
{
	return QuadMesh ? QuadMesh->GetVertexBuffer(LOD) : nullptr;
}

FIndexBuffer* FBillboardSceneProxy::GetIndexBuffer(uint8 LOD) const
{
	return QuadMesh ? QuadMesh->GetIndexBuffer(LOD) : nullptr;
}
