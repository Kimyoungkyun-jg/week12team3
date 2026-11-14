#include "EnginePCH.h"
#include "Engine/ParticleSceneProxy.h"
#include "Components/ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderPacket.h"
#include "Render/SceneRenderData.h"
#include "UObject/Casts.h"
#include <algorithm>
#include <cmath>

FParticleSceneProxy::FParticleSceneProxy(UParticleSystemComponent* InComponent)
	: Component(InComponent)
{
	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	UpdateFromComponent();
}

void FParticleSceneProxy::UpdateFromComponent()
{
	Sprites.Reset();
	bHasGpuEmitters = false;
	if (!Component) return;
	bShownInGame = Component->IsShown(true);
	bShownInEditor = Component->IsShown(false);
	const FMatrix LocalToWorld = Component->GetWorldMatrix();
	const FVector Scale = Component->GetWorldScale3D();
	bool bHasBounds = false;
	for (const FParticleEmitterInstance& Emitter : Component->GetEmitterInstances())
	{
		const UParticleLODLevel* LOD = Emitter.CurrentLODLevel;
		if (!LOD || !LOD->bEnabled || !LOD->RequiredModule || !LOD->TypeDataModule) continue;
		const EParticleEmitterType EmitterType = LOD->TypeDataModule->GetEmitterType();
		if (EmitterType == EParticleEmitterType::GpuSprite)
		{
			bHasGpuEmitters = true;
			const FVector Center = Component->GetWorldLocation();
			const FVector Extent(50.0f, 50.0f, 50.0f);
			const FVector Min = Center - Extent;
			const FVector Max = Center + Extent;
			if (!bHasBounds) { Bounds = FBox{ Min, Max }; bHasBounds = true; }
			else
			{
				Bounds.Min = FVector(std::min(Bounds.Min.X, Min.X), std::min(Bounds.Min.Y, Min.Y), std::min(Bounds.Min.Z, Min.Z));
				Bounds.Max = FVector(std::max(Bounds.Max.X, Max.X), std::max(Bounds.Max.Y, Max.Y), std::max(Bounds.Max.Z, Max.Z));
			}
			continue;
		}
		if (EmitterType != EParticleEmitterType::Sprite) continue;
		const bool bLocal = LOD->RequiredModule->bUseLocalSpace;
		const UParticleModuleSubUV* SubUVModule = nullptr;
		for (const UParticleModule* Module : LOD->Modules)
		{
			if (Module && Module->bEnabled)
			{
				if (const auto* Candidate = Cast<UParticleModuleSubUV>(Module))
					SubUVModule = Candidate;
			}
		}
		const float AtlasRowSize = SubUVModule ? static_cast<float>(std::max(1, SubUVModule->SubImagesVertical)) : 1.0f;
		const float AtlasColSize = SubUVModule ? static_cast<float>(std::max(1, SubUVModule->SubImagesHorizontal)) : 1.0f;
		const uint32 SubUVOffset = SubUVModule ? Emitter.GetModuleDataOffset(SubUVModule) : 0;
		UMaterial* EmitterMaterial = LOD->RequiredModule->Material ? LOD->RequiredModule->Material : Material;

		if (!Emitter.ParticleData || !Emitter.ParticleIndices) continue;
		for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
		{
			DECLARE_PARTICLE_CONST(Particle, Emitter.ParticleData + static_cast<size_t>(Emitter.ParticleIndices[Index]) * static_cast<size_t>(Emitter.ParticleStride));
			FParticleSpriteRenderData& Sprite = Sprites.AddDefaulted_GetRef();
			Sprite.Location = bLocal ? LocalToWorld.TransformPosition(Particle.Location) : Particle.Location;
			Sprite.Width = std::abs(Particle.Size.X * (bLocal ? Scale.Y : 1.0f));
			Sprite.Height = std::abs(Particle.Size.Y * (bLocal ? Scale.Z : 1.0f));
			Sprite.Rotation = Particle.Rotation;
			Sprite.Material = EmitterMaterial;
			float CurrentFrame = 0.0f;
			if (SubUVModule && SubUVOffset > 0)
			{
				const FSubUVPayload& SubUVPayload = *reinterpret_cast<const FSubUVPayload*>(
					reinterpret_cast<const uint8*>(&Particle) + SubUVOffset);
				CurrentFrame = SubUVPayload.ImageIndex;
			}
			Sprite.Constants.CurrentFrame = CurrentFrame;
			Sprite.Constants.AtlasRowSize = AtlasRowSize;
			Sprite.Constants.AtlasColSize = AtlasColSize;
			Sprite.Constants.Alpha = Particle.Color.A / 255.0f;
			Sprite.Constants.Tint = FVector4(Particle.Color.R / 255.0f, Particle.Color.G / 255.0f,
				Particle.Color.B / 255.0f, 1.0f);
			Sprite.Constants.DecodeSRGB = 0.0f;
			const float Radius = 0.5f * std::sqrt(Sprite.Width * Sprite.Width + Sprite.Height * Sprite.Height);
			const FVector Extent(Radius, Radius, Radius);
			const FVector Min = Sprite.Location - Extent;
			const FVector Max = Sprite.Location + Extent;
			if (!bHasBounds) { Bounds = FBox{ Min, Max }; bHasBounds = true; }
			else
			{
				Bounds.Min = FVector(std::min(Bounds.Min.X, Min.X), std::min(Bounds.Min.Y, Min.Y), std::min(Bounds.Min.Z, Min.Z));
				Bounds.Max = FVector(std::max(Bounds.Max.X, Max.X), std::max(Bounds.Max.Y, Max.Y), std::max(Bounds.Max.Z, Max.Z));
			}
		}
	}
}

void FParticleSceneProxy::SubmitToRenderQueue(FRenderQueue& Queue, const FViewContext& View) const
{
	if (!HasParticles()) return;
	for (const FParticleSpriteRenderData& Sprite : Sprites)
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
	if (!HasParticles()) return;

	struct FSortedSprite
	{
		int32 Index = 0;
		float DistanceSq = 0.0f;
	};

	const int32 Count = static_cast<int32>(Sprites.Num());
	TArray<FSortedSprite> Sorted;
	Sorted.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Delta = Sprites[Index].Location - View.CameraPosition;
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
		const FParticleSpriteRenderData& Sprite = Sprites[Entry.Index];
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
	if (!Component || !bHasGpuEmitters || !QuadMesh || !Material) return;

	FVector Forward = View.CameraForward.Normalized();
	if (Forward.Length() <= 1e-6f)
		Forward = (Component->GetWorldLocation() - View.CameraPosition).Normalized();
	FVector Right = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
	if (Right.Length() <= 1e-6f)
		Right = FVector(0.0f, 1.0f, 0.0f);
	const FVector Up = FVector::Cross(Forward, Right).Normalized();
	const FVector Facing = Forward * -1.0f;

	const FMatrix LocalToWorld = Component->GetWorldMatrix();
	const FVector Scale = Component->GetWorldScale3D();
	const FParticleInstanceParameters& InstanceParams = Component->GetInstanceParameters();

	for (FParticleEmitterInstance& Emitter : Component->GetEmitterInstancesMutable())
	{
		if (!Emitter.IsGpuEmitter() || Emitter.MaxActiveParticles <= 0) continue;
		const UParticleLODLevel* LOD = Emitter.CurrentLODLevel;
		if (!LOD || !LOD->RequiredModule) continue;

		if (!Emitter.GpuResources)
			Emitter.GpuResources = MakeShared<FGpuParticleEmitterResources>();

		FGpuParticleBatch& Batch = OutBatches.AddDefaulted_GetRef();
		Batch.GpuResources = Emitter.GpuResources;
		Batch.Mesh = QuadMesh;
		Batch.Material = LOD->RequiredModule->Material ? LOD->RequiredModule->Material : Material;
		Batch.MaxParticles = static_cast<uint32>(Emitter.MaxActiveParticles);
		Batch.ParticleStride = static_cast<uint32>(sizeof(FGpuParticle));
		Batch.bResetRequested = Emitter.bGpuResetRequested;
		Emitter.bGpuResetRequested = false;

		Batch.SpawnCount = static_cast<uint32>(Emitter.PendingGpuSpawns.Num());
		if (Batch.SpawnCount > 0)
		{
			const size_t Bytes = static_cast<size_t>(Batch.SpawnCount) * sizeof(FGpuParticle);
			Batch.PendingSpawnBytes.SetNum(static_cast<uint32>(Bytes));
			std::memcpy(Batch.PendingSpawnBytes.GetData(), Emitter.PendingGpuSpawns.GetData(), Bytes);
			Emitter.PendingGpuSpawns.Reset();
		}

		Batch.SimParams.DeltaTime = Emitter.LastGpuDeltaTime;
		Emitter.LastGpuDeltaTime = 0.0f;
		Batch.SimParams.MaxParticles = Batch.MaxParticles;
		Batch.SimParams.SpawnCount = Batch.SpawnCount;
		Batch.SimParams.WriteCursor = Emitter.GpuWriteCursor;
		if (Batch.MaxParticles > 0 && Batch.SpawnCount > 0)
			Emitter.GpuWriteCursor = (Emitter.GpuWriteCursor + Batch.SpawnCount) % Batch.MaxParticles;

		Batch.CurveLUT.SetNum(384);
		for (int32 i = 0; i < 384; ++i)
			Batch.CurveLUT[i] = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

		const UParticleModuleSubUV* SubUVMod = nullptr;
		for (const UParticleModule* Module : LOD->Modules)
		{
			if (!Module || !Module->bEnabled) continue;
			if (const auto* ColorMod = Cast<UParticleModuleColor>(Module))
			{
				Batch.SimParams.UseColorModule = 1;
				Batch.SimParams.ColorStart = FVector4(ColorMod->Start.R / 255.0f, ColorMod->Start.G / 255.0f, ColorMod->Start.B / 255.0f, ColorMod->Start.A / 255.0f);
				Batch.SimParams.ColorEnd = FVector4(ColorMod->End.R / 255.0f, ColorMod->End.G / 255.0f, ColorMod->End.B / 255.0f, ColorMod->End.A / 255.0f);
				if (!ColorMod->ParameterName.empty())
				{
					if (const FColor* Override = InstanceParams.ColorParams.Find(ColorMod->ParameterName))
					{
						Batch.SimParams.HasColorOverride = 1;
						Batch.SimParams.ColorOverride = FVector4(Override->R / 255.0f, Override->G / 255.0f, Override->B / 255.0f, Override->A / 255.0f);
					}
				}
			}
			else if (const auto* SizeMod = Cast<UParticleModuleSize>(Module))
			{
				Batch.SimParams.UseSizeModule = 1;
				Batch.SimParams.SizeStart = SizeMod->Start;
				Batch.SimParams.SizeEnd = SizeMod->End;
			}
			else if (const auto* SizeByLife = Cast<UParticleModuleSizeByLife>(Module))
			{
				Batch.SimParams.UseSizeByLife = 1;
				for (int32 i = 0; i < 128; ++i)
				{
					const float T = static_cast<float>(i) / 127.0f;
					Batch.CurveLUT[i] = FVector4(
						SizeByLife->ScaleX ? SizeByLife->ScaleX->GetFloatValue(T) : 1.0f,
						SizeByLife->ScaleY ? SizeByLife->ScaleY->GetFloatValue(T) : 1.0f,
						SizeByLife->ScaleZ ? SizeByLife->ScaleZ->GetFloatValue(T) : 1.0f,
						1.0f);
				}
			}
			else if (const auto* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
			{
				Batch.SimParams.UseColorOverLife = 1;
				for (int32 i = 0; i < 128; ++i)
				{
					const float T = static_cast<float>(i) / 127.0f;
					Batch.CurveLUT[128 + i] = FVector4(
						ColorOverLife->ColorR ? ColorOverLife->ColorR->GetFloatValue(T) : 1.0f,
						ColorOverLife->ColorG ? ColorOverLife->ColorG->GetFloatValue(T) : 1.0f,
						ColorOverLife->ColorB ? ColorOverLife->ColorB->GetFloatValue(T) : 1.0f,
						ColorOverLife->Alpha ? ColorOverLife->Alpha->GetFloatValue(T) : 1.0f);
				}
				if (!ColorOverLife->ParameterName.empty())
				{
					if (const FColor* Override = InstanceParams.ColorParams.Find(ColorOverLife->ParameterName))
					{
						Batch.SimParams.HasColorOverride = 1;
						Batch.SimParams.ColorOverride = FVector4(Override->R / 255.0f, Override->G / 255.0f, Override->B / 255.0f, Override->A / 255.0f);
					}
				}
			}
			else if (const auto* CandidateSubUV = Cast<UParticleModuleSubUV>(Module))
			{
				SubUVMod = CandidateSubUV;
				Batch.SimParams.UseSubUV = 1;
				Batch.SimParams.SubUVCols = static_cast<float>(std::max(1, CandidateSubUV->SubImagesHorizontal));
				Batch.SimParams.SubUVRows = static_cast<float>(std::max(1, CandidateSubUV->SubImagesVertical));
				Batch.SimParams.SubUVLinear = CandidateSubUV->bUseLinearOverLife ? 1u : 0u;
				Batch.SimParams.SubUVPlayRate = CandidateSubUV->PlayRate;
				if (!CandidateSubUV->bUseLinearOverLife && CandidateSubUV->SubImageIndex)
				{
					for (int32 i = 0; i < 128; ++i)
					{
						const float T = static_cast<float>(i) / 127.0f;
						const float V = CandidateSubUV->SubImageIndex->GetFloatValue(T);
						Batch.CurveLUT[256 + i] = FVector4(V, 0.0f, 0.0f, 0.0f);
					}
				}
			}
		}

		Batch.DrawParams.LocalToWorld = LocalToWorld;
		Batch.DrawParams.CameraFacing = FVector4(Facing.X, Facing.Y, Facing.Z, 0.0f);
		Batch.DrawParams.CameraRight = FVector4(Right.X, Right.Y, Right.Z, 0.0f);
		Batch.DrawParams.CameraUp = FVector4(Up.X, Up.Y, Up.Z, 0.0f);
		Batch.DrawParams.WorldScaleAndLocal = FVector4(
			Scale.Y, Scale.Z, LOD->RequiredModule->bUseLocalSpace ? 1.0f : 0.0f, 0.0f);
		Batch.DrawParams.AtlasParams = FVector4(
			SubUVMod ? static_cast<float>(std::max(1, SubUVMod->SubImagesVertical)) : 1.0f,
			SubUVMod ? static_cast<float>(std::max(1, SubUVMod->SubImagesHorizontal)) : 1.0f,
			0.0f,
			SubUVMod ? 1.0f : 0.0f);
	}
}
