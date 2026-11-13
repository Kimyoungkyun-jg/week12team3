#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Render/Mesh.h"
#include "Math/Transform.h"

#include <cmath>
#include <algorithm>

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	if (Template == InTemplate)
	{
		return;
	}

	Template = InTemplate;
	EmitterRuntimeData.Reset();
	Material = nullptr;
	bEmitterInitialized = false;
	OnTransformDirty();
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (!bEmitterInitialized)
	{
		InitializeParticleSystem();
	}

	if (!bEmitterInitialized)
	{
		return;
	}

	bool bNeedsBoundsUpdate = false;

	for (const TUniquePtr<FParticleEmitterRuntimeData>& Runtime : EmitterRuntimeData)
	{
		if (!Runtime)
		{
			continue;
		}

		const int32 PreviousCount = Runtime->Instance.GetActiveParticleCount();
		Runtime->Instance.Tick(DeltaTime, FVector::ZeroVector);

		if (PreviousCount != Runtime->Instance.GetActiveParticleCount() || Runtime->Instance.GetActiveParticleCount() > 0)
		{
			bNeedsBoundsUpdate = true;
		}
	}

	if (bNeedsBoundsUpdate)
	{
		OnTransformDirty();
	}
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	float Extent = 0.5f;

	for (const TUniquePtr<FParticleEmitterRuntimeData>& Runtime : EmitterRuntimeData)
	{
		if (!Runtime)
		{
			continue;
		}

		const bool bMeshEmitter = Runtime->MeshTypeDataModule != nullptr;
		float ParticleRadius = 0.5f;

		if (bMeshEmitter && Runtime->MeshTypeDataModule->Mesh)
		{
			const FBox& MeshBounds = Runtime->MeshTypeDataModule->Mesh->GetRenderBounds();
			const FVector Scale = Runtime->MeshTypeDataModule->MeshScale;
			const FVector ScaledMin = MeshBounds.Min * Scale;
			const FVector ScaledMax = MeshBounds.Max * Scale;
			ParticleRadius = std::max(ScaledMin.Size(), ScaledMax.Size());
		}

		for (int32 Index = 0; Index < Runtime->Instance.GetActiveParticleCount(); ++Index)
		{
			const FBaseParticle* Particle = Runtime->Instance.GetParticle(Index);
			if (!Particle)
			{
				continue;
			}

			if (!bMeshEmitter)
			{
				ParticleRadius = std::max(std::abs(Particle->Size.X), std::abs(Particle->Size.Y)) * 0.5f;
			}

			Extent = std::max(Extent, Particle->Location.Size() + ParticleRadius);
		}
	}

	return FBox{ FVector(-Extent, -Extent, -Extent), FVector(Extent, Extent, Extent) };
}


void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	if (!bEmitterInitialized)
	{
		return;
	}

	const FMatrix ComponentWorld = GetWorldMatrix();
	const FVector ComponentScale = GetWorldScale3D();

	for (const TUniquePtr<FParticleEmitterRuntimeData>& Runtime : EmitterRuntimeData)
	{
		if (!Runtime)
		{
			continue;
		}

		const int32 ParticleCount = Runtime->Instance.GetActiveParticleCount();
		if (ParticleCount == 0)
		{
			continue;
		}

		// Mesh Emitter
		if (Runtime->MeshTypeDataModule)
		{
			UStaticMesh* Mesh = Runtime->MeshTypeDataModule->Mesh;

			if (!Mesh || !Mesh->GetVertexBuffer(0) || !Mesh->GetIndexBuffer(0))
			{
				continue;
			}

			const FStaticMeshData& MeshData = Mesh->GetMeshData();
			if (MeshData.Indices.IsEmpty())
			{
				continue;
			}

			for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
			{
				const FBaseParticle* Particle = Runtime->Instance.GetParticle(ParticleIndex);
				if (!Particle)
				{
					continue;
				}

				const FVector WorldPosition = ComponentWorld.TransformPosition(Particle->Location);
				const FVector ToCamera = WorldPosition - ViewContext.CameraPosition;
				const float DistanceSquared = ToCamera.Dot(ToCamera);
				const float Rotation = std::isfinite(Particle->Rotation) ? Particle->Rotation : 0.0f;

				const FRotator ParticleRotation(0.0f, FMath::RadiansToDegrees(Rotation), 0.0f);
				const FMatrix ParticleLocalMatrix = FTransform(Particle->Location, Runtime->MeshTypeDataModule->MeshScale, ParticleRotation).GetLocalMatrix();
				const FMatrix* ParticleWorldMatrix = RenderQueue.StoreWorldMatrix(ParticleLocalMatrix * ComponentWorld);

				const auto AddMeshSection = [&](uint32 StartIndex, uint32 IndexCount, uint32 MaterialSlotIndex)
				{
					if (IndexCount == 0)
					{
						return;
					}

					UMaterial* SectionMaterial = Mesh->GetMaterial(MaterialSlotIndex);
					if (!SectionMaterial)
					{
						return;
					}

					FRenderPacket Packet;
					Packet.Mesh = Mesh;
					Packet.Material = SectionMaterial;
					Packet.Model = ParticleWorldMatrix;
					Packet.StartIndex = StartIndex;
					Packet.IndexCount = IndexCount;
					Packet.CameraToParticleDistance = DistanceSquared;

					RenderQueue.Add(Packet);
				};

				if (MeshData.Sections.IsEmpty())
				{
					AddMeshSection(0, static_cast<uint32>(MeshData.Indices.Num()), 0);
				}
				else
				{
					for (const FStaticMeshSection& Section : MeshData.Sections)
					{
						AddMeshSection(Section.StartIndex, Section.IndexCount, Section.MaterialSlotIndex);
					}
				}
			}

			continue;
		}

		// Sprite Emitter
		if (!QuadMesh || !Runtime->SpriteMaterial)
		{
			continue;
		}

		FDynamicSpriteEmitterReplayData ReplayData;
		Runtime->Instance.GetReplayData(ReplayData);

		ReplayData.ComponentToWorld = ComponentWorld;
		ReplayData.ComponentScale = ComponentScale;
		ReplayData.Material = Runtime->SpriteMaterial;

		if (!Runtime->SpriteEmitterData.BuildVertexData(ReplayData, ViewContext))
		{
			continue;
		}

		Runtime->ParticleParams.CurrentFrame = 0.0f;
		Runtime->ParticleParams.AtlasRowSize = 1.0f;
		Runtime->ParticleParams.AtlasColSize = 1.0f;
		Runtime->ParticleParams.Alpha = 1.0f;
		Runtime->ParticleParams.Tint = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Runtime->ParticleParams.DecodeSRGB = 0.0f;

		FRenderPacket Packet;
		Packet.Mesh = QuadMesh;
		Packet.Material = Runtime->SpriteMaterial;
		Packet.Model = RenderQueue.StoreWorldMatrix(FMatrix::Identity);
		Packet.VertexBufferOverride = Runtime->SpriteEmitterData.GetVertexBuffer();
		Packet.IndexBufferOverride = Runtime->SpriteEmitterData.GetIndexBuffer();
		Packet.IndexCount = Runtime->SpriteEmitterData.GetIndexCount();
		Packet.CameraToParticleDistance = Runtime->SpriteEmitterData.GetSortDistance();
		Packet.MaterialParamData = &Runtime->ParticleParams;
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);

		RenderQueue.Add(Packet);
	}
}


void UParticleSystemComponent::InitializeParticleSystem()
{
	if (!Template)
	{
		Template = NewObject<UParticleSystem>(this, FName("ParticleSystemTemplate"));
	}

	if (!Template)
	{
		return;
	}

	Template->CreateDefaultSetup();

	if (Template->GetEmitters().Num() == 1)
	{
		UParticleEmitter* MeshEmitter = Template->CreateEmitter(FName("Emitter_Mesh"));

		if (MeshEmitter)
		{
			UParticleModuleTypeDataMesh* MeshTypeData = MeshEmitter->CreateMeshTypeData();
			MeshTypeData->Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
			MeshTypeData->MeshScale = FVector(10.0f, 10.0f, 10.0f);
			MeshEmitter->GetLODLevel(0)->GetSpawnModule()->SpawnRate = 2.0f;

			for (UParticleModule* Module : MeshEmitter->GetLODLevel(0)->GetModules())
			{
				if (UParticleModuleLocation* LocationModule = Cast<UParticleModuleLocation>(Module))
				{
					LocationModule->MinOffset = FVector(100.0f, 0.0f, 0.0f);
					LocationModule->MaxOffset = FVector(100.0f, 0.0f, 0.0f);
				}
			}
		}
	}

	EmitterRuntimeData.Reset();
	Material = nullptr;

	for (UParticleEmitter* Emitter : Template->GetEmitters())
	{
		if (!Emitter)
		{
			continue;
		}

		UParticleLODLevel* LODLevel = Emitter->GetLODLevel(0);
		if (!LODLevel)
		{
			continue;
		}

		const int32 MaxParticles = std::clamp(Emitter->GetMaxActiveParticles(), 1, 65536);
		TUniquePtr<FParticleEmitterRuntimeData> Runtime = MakeUnique<FParticleEmitterRuntimeData>();

		if (!Runtime->Instance.Initialize(static_cast<uint32>(MaxParticles)))
		{
			continue;
		}

		Runtime->MeshTypeDataModule = Emitter->GetMeshTypeDataModule();

		if (!Runtime->MeshTypeDataModule && !Runtime->SpriteEmitterData.Initialize(static_cast<uint32>(MaxParticles)))
		{
			continue;
		}

		Runtime->Instance.SetSpawnModule(LODLevel->GetSpawnModule());
		Runtime->Instance.SetModules(LODLevel->GetModules());

		if (!Runtime->MeshTypeDataModule)
		{
			UParticleModuleRequired* RequiredModule = LODLevel->GetRequiredModule();
			Runtime->SpriteMaterial = RequiredModule ? Cast<UMaterial>(RequiredModule->Material) : nullptr;

			if (!Runtime->SpriteMaterial || Runtime->SpriteMaterial->PSOType != EPSOType::Particle_Sprite)
			{
				Runtime->SpriteMaterial = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteMaterial");

				if (RequiredModule && Runtime->SpriteMaterial)
				{
					RequiredModule->Material = Runtime->SpriteMaterial;
				}
			}

			if (!Material && Runtime->SpriteMaterial)
			{
				Material = Runtime->SpriteMaterial;
			}
		}

		EmitterRuntimeData.Add(std::move(Runtime));
	}

	bEmitterInitialized = !EmitterRuntimeData.IsEmpty();
}