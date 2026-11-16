#include "EnginePCH.h"
#include "Engine/ParticleSceneProxy.h"
#include "Asset/AssetManager.h"
#include "Render/MeshBatch.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	struct FSortedParticleIndex
	{
		int32 Index = 0;
		float DistanceSq = 0.0f;
	};

	FMatrix BuildRotatedBillboardMatrix(const FParticleRenderData& ParticleData, const FViewContext& View)
	{
		FMatrix World = View.BuildBillboardMatrix(ParticleData.Location, ParticleData.Width, ParticleData.Height);
		const float C = std::cos(ParticleData.Rotation);
		const float S = std::sin(ParticleData.Rotation);
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const float Right = World.M[1][Axis];
			const float Up = World.M[2][Axis];
			World.M[1][Axis] = C * Right + S * Up;
			World.M[2][Axis] = C * Up - S * Right;
		}
		return World;
	}

	EPSOType ResolveMeshParticlePSO(const UMaterial& EntryMat)
	{
		if (EntryMat.BlendState == EBlendState::Opaque)
			return EPSOType::Particle_MeshOpaque;
		if (EntryMat.BlendState == EBlendState::Additive)
			return EPSOType::Particle_MeshAdditive;
		return EPSOType::Particle_MeshTranslucent;
	}
}

void FDynamicSpriteEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterial* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
{
	if (Sprites.IsEmpty() || !QuadMesh)
		return;

	FVertexBuffer* QuadVB = QuadMesh->GetVertexBuffer(0);
	FIndexBuffer* QuadIB = QuadMesh->GetIndexBuffer(0);
	if (!QuadVB || !QuadIB)
		return;

	const int32 Count = static_cast<int32>(Sprites.Num());
	TArray<FSortedParticleIndex> Sorted;
	Sorted.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Delta = Sprites[Index].Location - View.CameraPosition;
		Sorted.Add({ Index, Delta.Dot(Delta) });
	}
	std::sort(Sorted.begin(), Sorted.end(), [](const FSortedParticleIndex& A, const FSortedParticleIndex& B) // 파티클 알갱이들을 거리 순으로 정렬 Back -> Front
	{
		return A.DistanceSq > B.DistanceSq;
	});

	for (int32 RunStart = 0; RunStart < Count;)
	{
		const FParticleRenderData& First = Sprites[Sorted[RunStart].Index];
		UMaterial* EntryMat = First.Material ? First.Material : DefaultMaterial;
		if (!EntryMat)
		{
			++RunStart;
			continue;
		}

		int32 RunEnd = RunStart + 1;
		while (RunEnd < Count) // 연속된 Particle 중에서 같은 Material을 사용하는 Particle을 하나의 그룹으로 묶음
		{
			const FParticleRenderData& Next = Sprites[Sorted[RunEnd].Index];
			UMaterial* NextMat = Next.Material ? Next.Material : DefaultMaterial;
			if (NextMat != EntryMat)
				break;
			++RunEnd;
		}

		const uint32 RunCount = static_cast<uint32>(RunEnd - RunStart);
		FParticleInstanceData* Dest = nullptr; // 이번에 렌더링할 파티클 그룹의 인스턴스 데이터가 저장될 메모리 시작 주소
		const uint32 StartInstance = Collector.AllocateParticleInstances(RunCount, Dest);
		if (!Dest)
			break;

		for (uint32 LocalIdx = 0; LocalIdx < RunCount; ++LocalIdx)
		{
			const FParticleRenderData& Sprite = Sprites[Sorted[RunStart + static_cast<int32>(LocalIdx)].Index];
			FParticleInstanceData& Instance = Dest[LocalIdx];
			Instance.World = BuildRotatedBillboardMatrix(Sprite, View);
			Instance.TintAndAlpha = FVector4(
				Sprite.Constants.Tint.X,
				Sprite.Constants.Tint.Y,
				Sprite.Constants.Tint.Z,
				Sprite.Constants.Alpha);
			Instance.SubUVParams = FVector4(
				Sprite.Constants.CurrentFrame,
				Sprite.Constants.AtlasRowSize,
				Sprite.Constants.AtlasColSize,
				Sprite.Constants.DecodeSRGB);
		}

		FMeshBatch Batch{};
		Batch.VertexFactory.Type = EVertexFactoryType::ParticleSprite;
		Batch.VertexFactory.VertexBuffer = QuadVB;
		Batch.Material = EntryMat;
		Batch.OverridePSOType = EPSOType::Particle_Instanced;
		Batch.MeshSortID = QuadMesh->SortID;
		Batch.CameraDistanceSq = Sorted[RunStart].DistanceSq;
		Batch.bIsParticle = true;
		Batch.bCastShadow = false;

		FMeshBatchElement Element{};
		Element.Proxy = Proxy;
		Element.IndexBuffer = QuadIB;
		Element.IndexCount = QuadIB->GetIndexCount();
		Element.InstanceCount = RunCount;
		Element.StartInstance = StartInstance;
		Element.bReceiveShadow = false;
		Element.bReceiveDecal = false;
		Batch.Elements.Add(Element);
		Collector.AddMesh(std::move(Batch));

		RunStart = RunEnd;
	}
}

void FDynamicMeshEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterial* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
{
	if (!Mesh || Particles.IsEmpty())
		return;

	FVertexBuffer* MeshVB = Mesh->GetVertexBuffer(0);
	FIndexBuffer* MeshIB = Mesh->GetIndexBuffer(0);
	if (!MeshVB || !MeshIB)
		return;

	const int32 Count = static_cast<int32>(Particles.Num());
	TArray<FSortedParticleIndex> Sorted;
	Sorted.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector WorldPos = bUseLocalSpace
			? LocalToWorld.TransformPosition(Particles[Index].Location)
			: Particles[Index].Location;
		const FVector Delta = WorldPos - View.CameraPosition;
		Sorted.Add({ Index, Delta.Dot(Delta) });
	}
	std::sort(Sorted.begin(), Sorted.end(), [](const FSortedParticleIndex& A, const FSortedParticleIndex& B)
	{
		return A.DistanceSq > B.DistanceSq;
	});

	const FStaticMeshData& MeshData = Mesh->GetMeshData(0);
	const bool bHasSections = !MeshData.Sections.IsEmpty();
	const int32 SectionCount = bHasSections ? static_cast<int32>(MeshData.Sections.Num()) : 1;

	for (int32 SecIdx = 0; SecIdx < SectionCount; ++SecIdx)
	{
		const uint32 SecStartIndex = bHasSections ? MeshData.Sections[SecIdx].StartIndex : 0;
		const uint32 SecIndexCount = bHasSections ? MeshData.Sections[SecIdx].IndexCount : MeshIB->GetIndexCount();
		if (SecIndexCount == 0)
			continue;

		const uint32 SlotIndex = bHasSections ? MeshData.Sections[SecIdx].MaterialSlotIndex : 0;
		UMaterial* EntryMat = MaterialOverride ? MaterialOverride : Mesh->GetMaterial(SlotIndex);
		if (!EntryMat)
			continue;

		const uint32 InstanceCount = static_cast<uint32>(Count);
		FParticleInstanceData* Dest = nullptr;
		const uint32 StartInstance = Collector.AllocateParticleInstances(InstanceCount, Dest);
		if (!Dest)
			continue;

		const FVector4& BaseCol = EntryMat->BaseColor;
		const bool bHasTex = !EntryMat->Textures.IsEmpty() && EntryMat->Textures[0] != nullptr;

		for (int32 SortedIdx = 0; SortedIdx < Count; ++SortedIdx)
		{
			const FParticleRenderData& ParticleData = Particles[Sorted[SortedIdx].Index];
			const float C = std::cos(ParticleData.Rotation);
			const float S = std::sin(ParticleData.Rotation);
			FMatrix World = FMatrix::Identity;
			World.M[0][0] = C * ParticleData.MeshScale.X;
			World.M[0][1] = S * ParticleData.MeshScale.X;
			World.M[1][0] = -S * ParticleData.MeshScale.Y;
			World.M[1][1] = C * ParticleData.MeshScale.Y;
			World.M[2][2] = ParticleData.MeshScale.Z;
			World.M[3][0] = ParticleData.Location.X;
			World.M[3][1] = ParticleData.Location.Y;
			World.M[3][2] = ParticleData.Location.Z;
			if (ParticleData.bUseLocalSpace)
				World = World * LocalToWorld;

			FParticleInstanceData& Instance = Dest[SortedIdx];
			Instance.World = World;
			Instance.TintAndAlpha = FVector4(
				ParticleData.Constants.Tint.X * BaseCol.X,
				ParticleData.Constants.Tint.Y * BaseCol.Y,
				ParticleData.Constants.Tint.Z * BaseCol.Z,
				ParticleData.Constants.Alpha * BaseCol.W);
			Instance.SubUVParams = FVector4(bHasTex ? 1.0f : 0.0f, 1.0f, 1.0f, 0.0f);
		}

		FMeshBatch Batch{};
		Batch.VertexFactory.Type = EVertexFactoryType::ParticleMesh;
		Batch.VertexFactory.VertexBuffer = MeshVB;
		Batch.Material = EntryMat;
		Batch.OverridePSOType = ResolveMeshParticlePSO(*EntryMat);
		Batch.MeshSortID = Mesh->SortID;
		Batch.CameraDistanceSq = Sorted[0].DistanceSq;
		Batch.bIsParticle = true;
		Batch.bCastShadow = false;

		FMeshBatchElement Element{};
		Element.Proxy = Proxy;
		Element.IndexBuffer = MeshIB;
		Element.FirstIndex = SecStartIndex;
		Element.IndexCount = SecIndexCount;
		Element.InstanceCount = InstanceCount;
		Element.StartInstance = StartInstance;
		Element.bReceiveShadow = false;
		Element.bReceiveDecal = false;
		Batch.Elements.Add(Element);
		Collector.AddMesh(std::move(Batch));
	}
}

void FDynamicGpuSpriteEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterial* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
{
	if (!QuadMesh || !GpuResources || MaxParticles == 0)
		return;

	UMaterial* EntryMat = Material ? Material : DefaultMaterial;
	FVertexBuffer* QuadVB = QuadMesh->GetVertexBuffer(0);
	FIndexBuffer* QuadIB = QuadMesh->GetIndexBuffer(0);
	if (!EntryMat || !QuadVB || !QuadIB)
		return;

	FVector Forward = View.CameraForward.Normalized();
	if (Forward.Length() <= 1e-6f)
		Forward = (WorldLocation - View.CameraPosition).Normalized();
	FVector Right = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
	if (Right.Length() <= 1e-6f)
		Right = FVector(0.0f, 1.0f, 0.0f);
	const FVector Up = FVector::Cross(Forward, Right).Normalized();
	const FVector Facing = Forward * -1.0f;

	FGpuParticleDrawParams DrawParams{};
	DrawParams.LocalToWorld = LocalToWorld;
	DrawParams.CameraFacing = FVector4(Facing.X, Facing.Y, Facing.Z, 0.0f);
	DrawParams.CameraRight = FVector4(Right.X, Right.Y, Right.Z, 0.0f);
	DrawParams.CameraUp = FVector4(Up.X, Up.Y, Up.Z, 0.0f);
	DrawParams.WorldScaleAndLocal = FVector4(WorldScale.Y, WorldScale.Z, bUseLocalSpace ? 1.0f : 0.0f, 0.0f);
	DrawParams.AtlasParams = AtlasParams;

	FGpuParticleBatch GpuBatch{};
	GpuBatch.GpuResources = GpuResources;
	GpuBatch.Mesh = QuadMesh;
	GpuBatch.Material = EntryMat;
	GpuBatch.MaxParticles = MaxParticles;
	GpuBatch.ParticleStride = ParticleStride;
	GpuBatch.bResetRequested = bResetRequested;
	GpuBatch.SpawnCount = SimParams.SpawnCount;
	GpuBatch.PendingSpawnBytes = std::move(PendingSpawnBytes);
	GpuBatch.SimParams = SimParams;
	GpuBatch.CurveLUT = CurveLUT;
	GpuBatch.DrawParams = DrawParams;
	Collector.AddGpuParticleBatch(std::move(GpuBatch));

	// 동일 프레임의 다른 뷰에서 스폰과 델타타임이 중복 실행되지 않도록 소진 처리
	bResetRequested = false;
	SimParams.DeltaTime = 0.0f;
	SimParams.SpawnCount = 0;

	const FVector Delta = WorldLocation - View.CameraPosition;
	FMeshBatch Batch{};
	Batch.VertexFactory.Type = EVertexFactoryType::ParticleGpuSprite;
	Batch.VertexFactory.VertexBuffer = QuadVB;
	Batch.VertexFactory.GpuResources = GpuResources.get();
	Batch.VertexFactory.DrawParamsSize = sizeof(FGpuParticleDrawParams);
	std::memcpy(Batch.VertexFactory.DrawParamsData.data(), &DrawParams, sizeof(FGpuParticleDrawParams));
	Batch.Material = EntryMat;
	Batch.OverridePSOType = EPSOType::Particle_GpuInstanced;
	Batch.MeshSortID = QuadMesh->SortID;
	Batch.CameraDistanceSq = Delta.Dot(Delta);
	Batch.bIsParticle = true;
	Batch.bCastShadow = false;

	FMeshBatchElement Element{};
	Element.Proxy = Proxy;
	Element.IndexBuffer = QuadIB;
	Element.IndexCount = QuadIB->GetIndexCount();
	Element.InstanceCount = MaxParticles;
	Element.bReceiveShadow = false;
	Element.bReceiveDecal = false;
	Batch.Elements.Add(Element);
	Collector.AddMesh(std::move(Batch));
}

FParticleSceneProxy::FParticleSceneProxy(FParticleDynamicData&& InitialData)
	: DynamicData(std::move(InitialData))
{
	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	SyncPrimitiveState();
}

void FParticleSceneProxy::SetDynamicData(FParticleDynamicData&& InData)
{
	DynamicData = std::move(InData);
	SyncPrimitiveState();
}

void FParticleSceneProxy::ApplyParticleTransform(const FPrimitiveTransformData& Data)
{
	ApplyTransformData(Data);
	DynamicData.LocalToWorld = Data.LocalToWorld;
	DynamicData.WorldLocation = Data.LocalToWorld.TransformPosition(FVector::ZeroVector);
	DynamicData.WorldScale = Data.WorldScale3D;

	const FVector BoxMin = Data.WorldBounds.Center - Data.WorldBounds.Extent;
	const FVector BoxMax = Data.WorldBounds.Center + Data.WorldBounds.Extent;
	if (DynamicData.bFixedBounds || DynamicData.bLocalBoundsOnly)
	{
		DynamicData.Bounds = FBox{ BoxMin, BoxMax };
	}
	else
	{
		DynamicData.Bounds.Min = FVector(
			std::min(DynamicData.Bounds.Min.X, BoxMin.X),
			std::min(DynamicData.Bounds.Min.Y, BoxMin.Y),
			std::min(DynamicData.Bounds.Min.Z, BoxMin.Z));
		DynamicData.Bounds.Max = FVector(
			std::max(DynamicData.Bounds.Max.X, BoxMax.X),
			std::max(DynamicData.Bounds.Max.Y, BoxMax.Y),
			std::max(DynamicData.Bounds.Max.Z, BoxMax.Z));
		Bounds.Center = (DynamicData.Bounds.Min + DynamicData.Bounds.Max) * 0.5f;
		Bounds.Extent = (DynamicData.Bounds.Max - DynamicData.Bounds.Min) * 0.5f;
	}
}

void FParticleSceneProxy::SyncPrimitiveState()
{
	ApplyBaseRenderState(DynamicData.BaseState);

	FPrimitiveTransformData TransformData;
	TransformData.LocalToWorld = DynamicData.LocalToWorld;
	TransformData.WorldToLocal = DynamicData.LocalToWorld.Inverse();
	TransformData.WorldScale3D = DynamicData.WorldScale;
	TransformData.WorldBounds.Center = (DynamicData.Bounds.Min + DynamicData.Bounds.Max) * 0.5f;
	TransformData.WorldBounds.Extent = (DynamicData.Bounds.Max - DynamicData.Bounds.Min) * 0.5f;
	ApplyTransformData(TransformData);
}

bool FParticleSceneProxy::HasParticles() const
{
	if (!Material)
		return false;
	for (const TUniquePtr<FDynamicEmitterDataBase>& Emitter : DynamicData.DynamicEmitters)
	{
		if (Emitter && Emitter->HasParticles())
			return true;
	}
	return false;
}

void FParticleSceneProxy::GetDynamicMeshElements(const FViewContext& View, FMeshElementCollector& Collector, uint32 LODIndex) const
{
	if (!bVisible || !HasParticles())
		return;

	for (const TUniquePtr<FDynamicEmitterDataBase>& Emitter : DynamicData.DynamicEmitters)
	{
		if (Emitter && Emitter->HasParticles())
			Emitter->GetDynamicMeshElementsForView(this, QuadMesh, Material, View, Collector);
	}
}
