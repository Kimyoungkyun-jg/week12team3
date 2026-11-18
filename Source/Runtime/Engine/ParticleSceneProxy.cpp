#include "EnginePCH.h"
#include "Engine/ParticleSceneProxy.h"
#include "Asset/AssetManager.h"
#include "Render/MaterialRenderProxy.h"
#include "Render/MeshBatch.h"
#include "Render/SkeletalMesh.h"
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

	EPSOType ResolveMeshParticlePSO(const UMaterialInterface& EntryMat)
	{
		if (EntryMat.GetBlendState() == EBlendState::Opaque)
			return EPSOType::Particle_MeshOpaque;
		if (EntryMat.GetBlendState() == EBlendState::Additive)
			return EPSOType::Particle_MeshAdditive;
		return EPSOType::Particle_MeshTranslucent;
	}

	EPSOType ResolveGpuMeshParticlePSO(const UMaterialInterface& EntryMat)
	{
		if (EntryMat.GetBlendState() == EBlendState::Opaque)
			return EPSOType::Particle_GpuMeshOpaque;
		if (EntryMat.GetBlendState() == EBlendState::Additive)
			return EPSOType::Particle_GpuMeshAdditive;
		return EPSOType::Particle_GpuMeshTranslucent;
	}

	EPSOType ResolveBeamTrailPSO(const UMaterialInterface& EntryMat)
	{
		if (EntryMat.GetBlendState() == EBlendState::Additive)
			return EPSOType::Particle_BeamTrailAdditive;
		return EPSOType::Particle_BeamTrailTranslucent;
	}

	bool HasRenderableStrips(const TArray<FBeamTrailStrip>& Strips)
	{
		for (const FBeamTrailStrip& Strip : Strips)
		{
			if (Strip.Points.Num() >= 2)
				return true;
		}
		return false;
	}

	FVector SafeNormalize(const FVector& Vec, const FVector& Fallback)
	{
		const float LenSq = Vec.Dot(Vec);
		if (LenSq > 1e-10f)
			return Vec / std::sqrt(LenSq);
		return Fallback;
	}

	void EmitBeamTrailStrips(const FParticleSceneProxy* Proxy, UMaterialInterface* EmitterMaterial,
		UMaterialInterface* DefaultMaterial, int32 Sheets, int32 FacingMode,
		const TArray<FBeamTrailStrip>& Strips, const FViewContext& View, FMeshElementCollector& Collector)
	{
		if (!HasRenderableStrips(Strips))
			return;

		UMaterialInterface* EntryMat = EmitterMaterial;
		if (!EntryMat)
			EntryMat = UAssetManager::GetAssetByPath<UMaterial>("BeamTrailMaterial");
		if (!EntryMat)
			EntryMat = DefaultMaterial;
		if (!EntryMat)
			return;

		const int32 ClampedSheets = std::max(1, Sheets);
		constexpr float PiValue = 3.14159265358979323846f;

		for (const FBeamTrailStrip& Strip : Strips)
		{
			const int32 PointCount = static_cast<int32>(Strip.Points.Num());
			if (PointCount < 2)
				continue;

			const uint32 VertCount = static_cast<uint32>(PointCount * 2);
			const uint32 IndexCount = static_cast<uint32>((PointCount - 1) * 6);
			const FVector MidPos = Strip.Points[PointCount / 2].Position;
			const FVector CamDelta = MidPos - View.CameraPosition;
			const float CameraDistSq = CamDelta.Dot(CamDelta);

			for (int32 SheetIdx = 0; SheetIdx < ClampedSheets; ++SheetIdx)
			{
				FGlobalDynamicVertexBuffer::FAllocation VertAlloc = Collector.AllocateDynamicVertices(VertCount);
				FGlobalDynamicIndexBuffer::FAllocation IdxAlloc = Collector.AllocateDynamicIndices(IndexCount);
				if (!VertAlloc.Data || !IdxAlloc.Data)
					continue;

				FVertex* Vertices = reinterpret_cast<FVertex*>(VertAlloc.Data);
				uint32* Indices = IdxAlloc.Data;
				const float SheetAngle = (ClampedSheets > 1)
					? (PiValue * static_cast<float>(SheetIdx) / static_cast<float>(ClampedSheets))
					: 0.0f;
				const float CosA = std::cos(SheetAngle);
				const float SinA = std::sin(SheetAngle);

				for (int32 i = 0; i < PointCount; ++i)
				{
					const FBeamTrailPoint& Pt = Strip.Points[i];
					const FVector PrevPos = Strip.Points[std::max(0, i - 1)].Position;
					const FVector NextPos = Strip.Points[std::min(PointCount - 1, i + 1)].Position;
					const FVector Tangent = SafeNormalize(NextPos - PrevPos, FVector(1.0f, 0.0f, 0.0f));

					FVector FacingNormal = FVector(0.0f, 0.0f, 1.0f);
					if (FacingMode == 0)
						FacingNormal = SafeNormalize(View.CameraPosition - Pt.Position, FVector(0.0f, 0.0f, 1.0f));
					else if (FacingMode == 1)
						FacingNormal = SafeNormalize(Pt.UpHint, FVector(0.0f, 0.0f, 1.0f));

					FVector Side = FVector::Cross(Tangent, FacingNormal);
					if (Side.Dot(Side) <= 1e-10f)
					{
						const FVector AltAxis = (std::abs(Tangent.Z) < 0.9f) ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
						Side = FVector::Cross(Tangent, AltAxis);
					}
					Side = SafeNormalize(Side, FVector(0.0f, 1.0f, 0.0f));

					if (ClampedSheets > 1)
					{
						const FVector Ortho = FVector::Cross(Tangent, Side);
						Side = SafeNormalize(Side * CosA + Ortho * SinA, Side);
					}

					const FVector FaceNormal = Pt.bUseExplicitEdges
						? SafeNormalize(FVector::Cross(SafeNormalize(Pt.EdgeHalfOffset, Side), Tangent), FacingNormal)
						: SafeNormalize(FVector::Cross(Side, Tangent), FacingNormal);
					const float HalfWidth = std::max(0.001f, Pt.Width) * 0.5f;
					const FVector HalfOffset = (Pt.bUseExplicitEdges && SheetIdx == 0)
						? Pt.EdgeHalfOffset
						: (Side * HalfWidth);

					FVertex& V0 = Vertices[i * 2];
					V0.Position = Pt.Position - HalfOffset;
					V0.Normal = FaceNormal;
					V0.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, 1.0f);
					V0.UV = FVector2(Pt.UCoord, 0.0f);
					V0.Color = Pt.Color;

					FVertex& V1 = Vertices[i * 2 + 1];
					V1.Position = Pt.Position + HalfOffset;
					V1.Normal = FaceNormal;
					V1.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, 1.0f);
					V1.UV = FVector2(Pt.UCoord, 1.0f);
					V1.Color = Pt.Color;
				}

				for (int32 s = 0; s < PointCount - 1; ++s)
				{
					const uint32 BaseV = static_cast<uint32>(s * 2);
					const uint32 BaseI = static_cast<uint32>(s * 6);
					Indices[BaseI + 0] = BaseV + 0;
					Indices[BaseI + 1] = BaseV + 1;
					Indices[BaseI + 2] = BaseV + 2;
					Indices[BaseI + 3] = BaseV + 1;
					Indices[BaseI + 4] = BaseV + 3;
					Indices[BaseI + 5] = BaseV + 2;
				}

				FMeshBatch Batch{};
				Batch.VertexFactory.Type = EVertexFactoryType::ParticleBeamTrail;
				Batch.VertexFactory.bUsesDynamicVertexBuffer = true;
				Batch.VertexFactory.bUsesDynamicIndexBuffer = true;
				Batch.Material = EntryMat;
				Batch.OverridePSOType = ResolveBeamTrailPSO(*EntryMat);
				Batch.CameraDistanceSq = CameraDistSq;
				Batch.bIsParticle = true;
				Batch.bCastShadow = false;

				FMeshBatchElement Element{};
				Element.Proxy = Proxy;
				Element.FirstIndex = IdxAlloc.FirstIndex;
				Element.IndexCount = IndexCount;
				Element.BaseVertexLocation = VertAlloc.BaseVertex;
				Element.InstanceCount = 0;
				Element.bReceiveShadow = false;
				Element.bReceiveDecal = false;
				Batch.Elements.Add(Element);
				Collector.AddMesh(std::move(Batch));
			}
		}
	}
}

void FDynamicSpriteEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
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
		UMaterialInterface* EntryMat = First.Material ? First.Material : DefaultMaterial;
		if (!EntryMat)
		{
			++RunStart;
			continue;
		}

		int32 RunEnd = RunStart + 1;
		while (RunEnd < Count) // 연속된 Particle 중에서 같은 Material을 사용하는 Particle을 하나의 그룹으로 묶음
		{
			const FParticleRenderData& Next = Sprites[Sorted[RunEnd].Index];
			UMaterialInterface* NextMat = Next.Material ? Next.Material : DefaultMaterial;
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
	UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
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
		UMaterialInterface* EntryMat = MaterialOverride ? MaterialOverride : Mesh->GetMaterial(SlotIndex);
		if (!EntryMat)
			continue;

		const uint32 InstanceCount = static_cast<uint32>(Count);
		FParticleInstanceData* Dest = nullptr;
		const uint32 StartInstance = Collector.AllocateParticleInstances(InstanceCount, Dest);
		if (!Dest)
			continue;

		FMaterialRenderProxy MaterialProxy{};
		EntryMat->GetRenderProxy(MaterialProxy);
		const FVector4& BaseCol = MaterialProxy.BaseColor;
		const bool bHasTex = MaterialProxy.Textures.IsValidIndex(0) && MaterialProxy.Textures[0] != nullptr;

		for (int32 SortedIdx = 0; SortedIdx < Count; ++SortedIdx)
		{
			const FParticleRenderData& ParticleData = Particles[Sorted[SortedIdx].Index];
			const float C = std::cos(ParticleData.Rotation);
			const float S = std::sin(ParticleData.Rotation);
			FMatrix LocalMat = FMatrix::Identity;
			LocalMat.M[0][0] = C * ParticleData.MeshScale.X;
			LocalMat.M[0][1] = S * ParticleData.MeshScale.X;
			LocalMat.M[1][0] = -S * ParticleData.MeshScale.Y;
			LocalMat.M[1][1] = C * ParticleData.MeshScale.Y;
			LocalMat.M[2][2] = ParticleData.MeshScale.Z;
			FMatrix World = LocalMat * ParticleData.MeshRotation.ToFMatrix();
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

void FDynamicAfterimageEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
{
	UMaterialInterface* EntryMat = Material && Material->GetPSOType() == EPSOType::StaticMesh_Translucent
		? Material : UAssetManager::GetAssetByPath<UMaterial>("Materials/AfterimageMaterial.material");
	if (!EntryMat) return;

	for (const FAfterimageRenderParticle& Particle : Particles)
	{
		const TSharedPtr<FAfterimageSnapshot>& Snapshot = Particle.Snapshot;
		if (!Snapshot || !Snapshot->Mesh || Snapshot->SkinningMatrices.IsEmpty() || Particle.Alpha <= 0.0f) continue;
		if (!Snapshot->MatrixBuffer)
		{
			Snapshot->MatrixBuffer = RenderCommand::CreateDynamicStructuredBuffer(
				static_cast<uint32>(Snapshot->SkinningMatrices.Num()), sizeof(FMatrix));
			if (Snapshot->MatrixBuffer)
				RenderCommand::UpdateBufferData(Snapshot->MatrixBuffer.get(), Snapshot->SkinningMatrices.GetData(),
					static_cast<uint32>(Snapshot->SkinningMatrices.Num() * sizeof(FMatrix)));
		}
		if (!Snapshot->MatrixBuffer || !Snapshot->MatrixBuffer->GetSRV()) continue;

		FVertexBuffer* VB = Snapshot->Mesh->GetVertexBuffer(0);
		FIndexBuffer* IB = Snapshot->Mesh->GetIndexBuffer(0);
		if (!VB || !IB) continue;
		const FVector Delta = (Snapshot->WorldBounds.Min + Snapshot->WorldBounds.Max) * 0.5f - View.CameraPosition;
		const float DistanceSq = Delta.Dot(Delta);
		const auto& Sections = Snapshot->Mesh->MeshData.Sections;
		const int32 SectionCount = Sections.IsEmpty() ? 1 : static_cast<int32>(Sections.Num());
		for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
		{
			FMeshBatch Batch{};
			Batch.VertexFactory.Type = EVertexFactoryType::GPUSkin;
			Batch.VertexFactory.VertexBuffer = VB;
			Batch.VertexFactory.SkinningMatricesBuffer = Snapshot->MatrixBuffer.get();
			Batch.Material = EntryMat;
			Batch.OverridePSOType = EPSOType::StaticMesh_Translucent;
			Batch.MeshSortID = Snapshot->Mesh->SortID;
			Batch.CameraDistanceSq = DistanceSq;
			Batch.ParticleAlpha = Particle.Alpha;
			Batch.bCastShadow = false;

			FMeshBatchElement Element{};
			Element.Proxy = Proxy;
			Element.IndexBuffer = IB;
			Element.WorldMatrix = &Snapshot->World;
			Element.FirstIndex = Sections.IsEmpty() ? 0 : Sections[SectionIndex].StartIndex;
			Element.IndexCount = Sections.IsEmpty() ? IB->GetIndexCount() : Sections[SectionIndex].IndexCount;
			Element.bReceiveShadow = false;
			Element.bReceiveDecal = false;
			Batch.Elements.Add(Element);
			Collector.AddMesh(std::move(Batch));
		}
	}
}

void FDynamicGpuSpriteEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
{
	UStaticMesh* RenderMesh = MeshAsset ? MeshAsset : QuadMesh;
	if (!RenderMesh || !GpuResources || MaxParticles == 0)
		return;

	UMaterialInterface* FallbackMat = Material ? Material : DefaultMaterial;
	FVertexBuffer* RenderVB = RenderMesh->GetVertexBuffer(0);
	FIndexBuffer* RenderIB = RenderMesh->GetIndexBuffer(0);
	if (!RenderVB || !RenderIB)
		return;
	if (!MeshAsset && !FallbackMat)
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
	GpuBatch.Mesh = RenderMesh;
	GpuBatch.Material = FallbackMat;
	GpuBatch.MaxParticles = MaxParticles;
	GpuBatch.ParticleStride = ParticleStride;
	GpuBatch.bResetRequested = bResetRequested;
	GpuBatch.SpawnCount = SimParams.SpawnCount;
	GpuBatch.PendingSpawnBytes = std::move(PendingSpawnBytes);
	GpuBatch.SimParams = SimParams;
	GpuBatch.DrawParams = DrawParams;
	Collector.AddGpuParticleBatch(std::move(GpuBatch));

	// 동일 프레임의 다른 뷰에서 스폰과 델타타임이 중복 실행되지 않도록 소진 처리
	bResetRequested = false;
	SimParams.DeltaTime = 0.0f;
	SimParams.SpawnCount = 0;

	const FVector Delta = WorldLocation - View.CameraPosition;
	const float CameraDistSq = Delta.Dot(Delta);

	if (MeshAsset)
	{
		const FStaticMeshData& MeshData = MeshAsset->GetMeshData(0);
		const bool bHasSections = !MeshData.Sections.IsEmpty();
		const int32 SectionCount = bHasSections ? static_cast<int32>(MeshData.Sections.Num()) : 1;

		for (int32 SecIdx = 0; SecIdx < SectionCount; ++SecIdx)
		{
			const uint32 SecStartIndex = bHasSections ? MeshData.Sections[SecIdx].StartIndex : 0;
			const uint32 SecIndexCount = bHasSections ? MeshData.Sections[SecIdx].IndexCount : RenderIB->GetIndexCount();
			if (SecIndexCount == 0)
				continue;

			const uint32 SlotIndex = bHasSections ? MeshData.Sections[SecIdx].MaterialSlotIndex : 0;
			UMaterialInterface* EntryMat = Material ? Material : MeshAsset->GetMaterial(SlotIndex);
			if (!EntryMat)
				EntryMat = DefaultMaterial;
			if (!EntryMat)
				continue;

			FMeshBatch Batch{};
			Batch.VertexFactory.Type = EVertexFactoryType::ParticleGpuMesh;
			Batch.VertexFactory.VertexBuffer = RenderVB;
			Batch.VertexFactory.GpuResources = GpuResources.get();
			Batch.VertexFactory.DrawParamsSize = sizeof(FGpuParticleDrawParams);
			std::memcpy(Batch.VertexFactory.DrawParamsData.data(), &DrawParams, sizeof(FGpuParticleDrawParams));
			Batch.Material = EntryMat;
			Batch.OverridePSOType = ResolveGpuMeshParticlePSO(*EntryMat);
			Batch.MeshSortID = MeshAsset->SortID;
			Batch.CameraDistanceSq = CameraDistSq;
			Batch.bIsParticle = true;
			Batch.bCastShadow = false;

			FMeshBatchElement Element{};
			Element.Proxy = Proxy;
			Element.IndexBuffer = RenderIB;
			Element.FirstIndex = SecStartIndex;
			Element.IndexCount = SecIndexCount;
			Element.InstanceCount = MaxParticles;
			Element.bReceiveShadow = false;
			Element.bReceiveDecal = false;
			Batch.Elements.Add(Element);
			Collector.AddMesh(std::move(Batch));
		}
		return;
	}

	FMeshBatch Batch{};
	Batch.VertexFactory.Type = EVertexFactoryType::ParticleGpuSprite;
	Batch.VertexFactory.VertexBuffer = RenderVB;
	Batch.VertexFactory.GpuResources = GpuResources.get();
	Batch.VertexFactory.DrawParamsSize = sizeof(FGpuParticleDrawParams);
	std::memcpy(Batch.VertexFactory.DrawParamsData.data(), &DrawParams, sizeof(FGpuParticleDrawParams));
	Batch.Material = FallbackMat;
	Batch.OverridePSOType = EPSOType::Particle_GpuInstanced;
	Batch.MeshSortID = QuadMesh->SortID;
	Batch.CameraDistanceSq = CameraDistSq;
	Batch.bIsParticle = true;
	Batch.bCastShadow = false;

	FMeshBatchElement Element{};
	Element.Proxy = Proxy;
	Element.IndexBuffer = RenderIB;
	Element.IndexCount = RenderIB->GetIndexCount();
	Element.InstanceCount = MaxParticles;
	Element.bReceiveShadow = false;
	Element.bReceiveDecal = false;
	Batch.Elements.Add(Element);
	Collector.AddMesh(std::move(Batch));
}

bool FDynamicBeamEmitterData::HasParticles() const
{
	return HasRenderableStrips(Beams);
}

void FDynamicBeamEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
{
	EmitBeamTrailStrips(Proxy, Material, DefaultMaterial, Sheets, 0, Beams, View, Collector);
}

bool FDynamicRibbonEmitterData::HasParticles() const
{
	return HasRenderableStrips(Trails);
}

void FDynamicRibbonEmitterData::GetDynamicMeshElementsForView(const FParticleSceneProxy* Proxy, UStaticMesh* QuadMesh,
	UMaterialInterface* DefaultMaterial, const FViewContext& View, FMeshElementCollector& Collector) const
{
	EmitBeamTrailStrips(Proxy, Material, DefaultMaterial, SheetsPerTrail, FacingMode, Trails, View, Collector);
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
