#include "EnginePCH.h"
#include "Engine/ParticleSceneProxy.h"
#include "Asset/AssetManager.h"
#include "Render/RenderPacket.h"
#include <algorithm>
#include <cmath>

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

void FParticleSceneProxy::SubmitToRenderQueue(FRenderQueue& Queue, const FViewContext& View, uint32 LODIndex) const
{
	if (!HasParticles()) return;
	for (const FParticleRenderData& ParticleData : DynamicData.Particles)
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
		FRenderPacket Packet;
		Packet.Mesh = QuadMesh;
		Packet.Material = ParticleData.Material ? ParticleData.Material : Material;
		Packet.Model = Queue.StoreWorldMatrix(World);
		const FVector Delta = ParticleData.Location - View.CameraPosition;
		Packet.CameraToParticleDistance = Delta.Dot(Delta);
		Packet.MaterialParamData = &ParticleData.Constants;
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
		Queue.Add(Packet);
	}
}

void FParticleSceneProxy::SubmitInstancedParticles(TArray<FParticleInstanceData>& OutInstances,
	TArray<FParticleInstancedBatch>& OutBatches, const FViewContext& View) const
{
	if (!HasParticles() || DynamicData.Particles.IsEmpty()) return;

	struct FSortedParticle
	{
		int32 Index = 0;
		float DistanceSq = 0.0f;
	};

	const int32 Count = static_cast<int32>(DynamicData.Particles.Num());
	TArray<FSortedParticle> Sorted;
	Sorted.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Delta = DynamicData.Particles[Index].Location - View.CameraPosition;
		Sorted.Add({ Index, Delta.Dot(Delta) });
	}
	std::sort(Sorted.begin(), Sorted.end(), [](const FSortedParticle& A, const FSortedParticle& B)
	{
		return A.DistanceSq > B.DistanceSq;
	});

	const uint32 StartInstance = static_cast<uint32>(OutInstances.Num());
	OutInstances.Reserve(StartInstance + static_cast<uint32>(Count));

	UStaticMesh* BatchMesh = nullptr;
	UMaterial* BatchMaterial = nullptr;
	uint32 BatchStart = StartInstance;
	uint32 BatchCount = 0;

	auto FlushSpriteBatch = [&]()
	{
		if (BatchCount == 0) return;
		FParticleInstancedBatch Batch{};
		Batch.Mesh = BatchMesh;
		Batch.Material = BatchMaterial;
		Batch.StartInstance = BatchStart;
		Batch.InstanceCount = BatchCount;
		Batch.bIsMesh = false;
		Batch.BlendState = BatchMaterial->BlendState;
		OutBatches.Add(Batch);
		BatchStart = static_cast<uint32>(OutInstances.Num());
		BatchCount = 0;
	};

	for (int32 SortedIdx = 0; SortedIdx < Count;)
	{
		const FParticleRenderData& FirstParticle = DynamicData.Particles[Sorted[SortedIdx].Index];
		if (FirstParticle.bIsMesh)
		{
			FlushSpriteBatch();
			int32 RunEnd = SortedIdx + 1;
			while (RunEnd < Count)
			{
				const FParticleRenderData& NextParticle = DynamicData.Particles[Sorted[RunEnd].Index];
				if (!NextParticle.bIsMesh || NextParticle.Mesh != FirstParticle.Mesh || NextParticle.Material != FirstParticle.Material)
					break;
				++RunEnd;
			}

			UStaticMesh* EntryMesh = FirstParticle.Mesh;
			if (EntryMesh)
			{
				const FStaticMeshData& MeshData = EntryMesh->GetMeshData(0);
				const bool bHasSections = !MeshData.Sections.IsEmpty();
				const int32 SectionCount = bHasSections ? static_cast<int32>(MeshData.Sections.Num()) : 1;

				for (int32 SecIdx = 0; SecIdx < SectionCount; ++SecIdx)
				{
					const uint32 SecStartIndex = bHasSections ? MeshData.Sections[SecIdx].StartIndex : 0;
					const uint32 SecIndexCount = bHasSections ? MeshData.Sections[SecIdx].IndexCount : 0;
					if (bHasSections && SecIndexCount == 0) continue;

					const uint32 SlotIndex = bHasSections ? MeshData.Sections[SecIdx].MaterialSlotIndex : 0;
					UMaterial* EntryMat = FirstParticle.Material
						? FirstParticle.Material
						: EntryMesh->GetMaterial(SlotIndex);
					if (!EntryMat) continue;

					const uint32 SecBatchStart = static_cast<uint32>(OutInstances.Num());
					const FVector4& BaseCol = EntryMat->BaseColor;
					const bool bHasTex = !EntryMat->Textures.IsEmpty() && EntryMat->Textures[0] != nullptr;

					for (int32 RunIdx = SortedIdx; RunIdx < RunEnd; ++RunIdx)
					{
						const FParticleRenderData& ParticleData = DynamicData.Particles[Sorted[RunIdx].Index];
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
							World = World * DynamicData.LocalToWorld;

						FParticleInstanceData Instance{};
						Instance.World = World;
						Instance.TintAndAlpha = FVector4(
							ParticleData.Constants.Tint.X * BaseCol.X,
							ParticleData.Constants.Tint.Y * BaseCol.Y,
							ParticleData.Constants.Tint.Z * BaseCol.Z,
							ParticleData.Constants.Alpha * BaseCol.W);
						Instance.SubUVParams = FVector4(bHasTex ? 1.0f : 0.0f, 1.0f, 1.0f, 0.0f);
						OutInstances.Add(Instance);
					}

					FParticleInstancedBatch Batch{};
					Batch.Mesh = EntryMesh;
					Batch.Material = EntryMat;
					Batch.StartInstance = SecBatchStart;
					Batch.InstanceCount = static_cast<uint32>(RunEnd - SortedIdx);
					Batch.StartIndex = SecStartIndex;
					Batch.IndexCount = SecIndexCount;
					Batch.bIsMesh = true;
					Batch.BlendState = EntryMat->BlendState;
					OutBatches.Add(Batch);
				}
			}

			BatchStart = static_cast<uint32>(OutInstances.Num());
			SortedIdx = RunEnd;
			continue;
		}

		UStaticMesh* EntryMesh = QuadMesh;
		UMaterial* EntryMat = FirstParticle.Material ? FirstParticle.Material : Material;
		if (!EntryMesh || !EntryMat)
		{
			++SortedIdx;
			continue;
		}

		if (BatchCount > 0 && (EntryMesh != BatchMesh || EntryMat != BatchMaterial))
			FlushSpriteBatch();

		BatchMesh = EntryMesh;
		BatchMaterial = EntryMat;

		FMatrix World = View.BuildBillboardMatrix(FirstParticle.Location, FirstParticle.Width, FirstParticle.Height);
		const float C = std::cos(FirstParticle.Rotation);
		const float S = std::sin(FirstParticle.Rotation);
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const float Right = World.M[1][Axis];
			const float Up = World.M[2][Axis];
			World.M[1][Axis] = C * Right + S * Up;
			World.M[2][Axis] = C * Up - S * Right;
		}

		FParticleInstanceData Instance{};
		Instance.World = World;
		Instance.TintAndAlpha = FVector4(
			FirstParticle.Constants.Tint.X,
			FirstParticle.Constants.Tint.Y,
			FirstParticle.Constants.Tint.Z,
			FirstParticle.Constants.Alpha);
		Instance.SubUVParams = FVector4(
			FirstParticle.Constants.CurrentFrame,
			FirstParticle.Constants.AtlasRowSize,
			FirstParticle.Constants.AtlasColSize,
			FirstParticle.Constants.DecodeSRGB);
		OutInstances.Add(Instance);
		++BatchCount;
		++SortedIdx;
	}

	FlushSpriteBatch();
}

void FParticleSceneProxy::SubmitGpuEmitters(TArray<FGpuParticleBatch>& OutBatches, const FViewContext& View) const
{
	if (DynamicData.GpuEmitters.IsEmpty() || !QuadMesh || !Material) return;

	FVector Forward = View.CameraForward.Normalized();
	if (Forward.Length() <= 1e-6f)
		Forward = (DynamicData.WorldLocation - View.CameraPosition).Normalized();
	FVector Right = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
	if (Right.Length() <= 1e-6f)
		Right = FVector(0.0f, 1.0f, 0.0f);
	const FVector Up = FVector::Cross(Forward, Right).Normalized();
	const FVector Facing = Forward * -1.0f;

	for (const FParticleDynamicGpuEmitterData& Emitter : DynamicData.GpuEmitters)
	{
		FGpuParticleBatch& Batch = OutBatches.AddDefaulted_GetRef();
		Batch.GpuResources = Emitter.GpuResources;
		Batch.Mesh = QuadMesh;
		Batch.Material = Emitter.Material ? Emitter.Material : Material;
		Batch.MaxParticles = Emitter.MaxParticles;
		Batch.ParticleStride = Emitter.ParticleStride;
		Batch.bResetRequested = Emitter.bResetRequested;
		Batch.SpawnCount = Emitter.SimParams.SpawnCount;
		Batch.PendingSpawnBytes = std::move(Emitter.PendingSpawnBytes);
		Batch.SimParams = Emitter.SimParams;
		Batch.CurveLUT = Emitter.CurveLUT;

		// 동일 프레임의 다른 뷰에서 스폰과 델타타임이 중복 실행되지 않도록 소진 처리
		Emitter.bResetRequested = false;
		Emitter.SimParams.DeltaTime = 0.0f;
		Emitter.SimParams.SpawnCount = 0;

		Batch.DrawParams.LocalToWorld = DynamicData.LocalToWorld;
		Batch.DrawParams.CameraFacing = FVector4(Facing.X, Facing.Y, Facing.Z, 0.0f);
		Batch.DrawParams.CameraRight = FVector4(Right.X, Right.Y, Right.Z, 0.0f);
		Batch.DrawParams.CameraUp = FVector4(Up.X, Up.Y, Up.Z, 0.0f);
		Batch.DrawParams.WorldScaleAndLocal = FVector4(
			DynamicData.WorldScale.Y, DynamicData.WorldScale.Z, Emitter.bUseLocalSpace ? 1.0f : 0.0f, 0.0f);
		Batch.DrawParams.AtlasParams = Emitter.AtlasParams;
	}
}
