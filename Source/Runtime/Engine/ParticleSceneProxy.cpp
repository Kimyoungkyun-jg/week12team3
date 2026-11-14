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
	for (const FParticleSpriteRenderData& Sprite : DynamicData.Sprites)
	{
		FMatrix World = View.BuildBillboardMatrix(Sprite.Location, Sprite.Width, Sprite.Height);
		const float C = std::cos(Sprite.Rotation);
		const float S = std::sin(Sprite.Rotation);
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			const float Right = World.M[1][Axis];
			const float Up = World.M[2][Axis];
			World.M[1][Axis] = C * Right + S * Up;
			World.M[2][Axis] = C * Up - S * Right;
		}
		FRenderPacket Packet;
		Packet.Mesh = QuadMesh;
		Packet.Material = Sprite.Material ? Sprite.Material : Material;
		Packet.Model = Queue.StoreWorldMatrix(World);
		const FVector Delta = Sprite.Location - View.CameraPosition;
		Packet.CameraToParticleDistance = Delta.Dot(Delta);
		Packet.MaterialParamData = &Sprite.Constants;
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
		Queue.Add(Packet);
	}
}

void FParticleSceneProxy::SubmitInstancedParticles(TArray<FParticleInstanceData>& OutInstances,
	TArray<FParticleInstancedBatch>& OutBatches, const FViewContext& View) const
{
	if (!HasParticles() || DynamicData.Sprites.IsEmpty()) return;

	struct FSortedSprite
	{
		int32 Index = 0;
		float DistanceSq = 0.0f;
	};

	const int32 Count = static_cast<int32>(DynamicData.Sprites.Num());
	TArray<FSortedSprite> Sorted;
	Sorted.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Delta = DynamicData.Sprites[Index].Location - View.CameraPosition;
		Sorted.Add({ Index, Delta.Dot(Delta) });
	}
	std::sort(Sorted.begin(), Sorted.end(), [](const FSortedSprite& A, const FSortedSprite& B)
	{
		return A.DistanceSq > B.DistanceSq;
	});

	const uint32 StartInstance = static_cast<uint32>(OutInstances.Num());
	OutInstances.Reserve(StartInstance + static_cast<uint32>(Count));

	UMaterial* BatchMaterial = nullptr;
	uint32 BatchStart = StartInstance;
	uint32 BatchCount = 0;

	for (const FSortedSprite& Entry : Sorted)
	{
		const FParticleSpriteRenderData& Sprite = DynamicData.Sprites[Entry.Index];
		UMaterial* SpriteMat = Sprite.Material ? Sprite.Material : Material;
		if (BatchCount > 0 && SpriteMat != BatchMaterial)
		{
			FParticleInstancedBatch Batch{};
			Batch.Mesh = QuadMesh;
			Batch.Material = BatchMaterial;
			Batch.StartInstance = BatchStart;
			Batch.InstanceCount = BatchCount;
			OutBatches.Add(Batch);
			BatchStart = static_cast<uint32>(OutInstances.Num());
			BatchCount = 0;
		}
		BatchMaterial = SpriteMat;

		FMatrix World = View.BuildBillboardMatrix(Sprite.Location, Sprite.Width, Sprite.Height);
		const float C = std::cos(Sprite.Rotation);
		const float S = std::sin(Sprite.Rotation);
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
			Sprite.Constants.Tint.X,
			Sprite.Constants.Tint.Y,
			Sprite.Constants.Tint.Z,
			Sprite.Constants.Alpha);
		Instance.SubUVParams = FVector4(
			Sprite.Constants.CurrentFrame,
			Sprite.Constants.AtlasRowSize,
			Sprite.Constants.AtlasColSize,
			Sprite.Constants.DecodeSRGB);
		OutInstances.Add(Instance);
		++BatchCount;
	}

	if (BatchCount > 0)
	{
		FParticleInstancedBatch Batch{};
		Batch.Mesh = QuadMesh;
		Batch.Material = BatchMaterial;
		Batch.StartInstance = BatchStart;
		Batch.InstanceCount = BatchCount;
		OutBatches.Add(Batch);
	}
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
