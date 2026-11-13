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
	EmitterInstance.Reset();
	SpriteEmitterData = FDynamicSpriteEmitterData();
	MeshTypeDataModule = nullptr;
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

	const int32 PreviousCount = EmitterInstance.GetActiveParticleCount();
	EmitterInstance.Tick(DeltaTime, FVector::ZeroVector);

	if (PreviousCount != EmitterInstance.GetActiveParticleCount() || EmitterInstance.GetActiveParticleCount() > 0)
	{
		OnTransformDirty();
	}
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	float Extent = 0.5f;

	if (MeshTypeDataModule && MeshTypeDataModule->Mesh)
	{
		const FBox& MeshBounds = MeshTypeDataModule->Mesh->GetRenderBounds();
		const FVector Scale = MeshTypeDataModule->MeshScale;
		const FVector ScaledMin = MeshBounds.Min * Scale;
		const FVector ScaledMax = MeshBounds.Max * Scale;
		const float MeshRadius = std::max(ScaledMin.Size(), ScaledMax.Size());

		for (int32 Index = 0; Index < EmitterInstance.GetActiveParticleCount(); ++Index)
		{
			const FBaseParticle* Particle = EmitterInstance.GetParticle(Index);
			if (!Particle)
			{
				continue;
			}

			Extent = std::max(Extent, Particle->Location.Size() + MeshRadius);
		}

		return FBox{ FVector(-Extent, -Extent, -Extent), FVector(Extent, Extent, Extent) };
	}

	for (int32 Index = 0; Index < EmitterInstance.GetActiveParticleCount(); ++Index)
	{
		const FBaseParticle* Particle = EmitterInstance.GetParticle(Index);
		if (!Particle)
		{
			continue;
		}

		const float ParticleRadius = std::max(Particle->Size.X, Particle->Size.Y) * 0.5f;
		Extent = std::max(Extent, Particle->Location.Size() + ParticleRadius);
	}

	return FBox{ FVector(-Extent, -Extent, -Extent), FVector(Extent, Extent, Extent) };
}


void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	const int32 ParticleCount = EmitterInstance.GetActiveParticleCount();

	if (!bEmitterInitialized || ParticleCount == 0)
	{
		return;
	}

	// Mesh Emitter
	if (MeshTypeDataModule)
	{
		UStaticMesh* Mesh = MeshTypeDataModule->Mesh;

		if (!Mesh || !Mesh->GetVertexBuffer(0) || !Mesh->GetIndexBuffer(0))
		{
			return;
		}

		const FStaticMeshData& MeshData = Mesh->GetMeshData();
		if (MeshData.Indices.IsEmpty())
		{
			return;
		}

		const FMatrix ComponentWorld = GetWorldMatrix();

		for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
		{
			const FBaseParticle* Particle = EmitterInstance.GetParticle(ParticleIndex);
			if (!Particle)
			{
				continue;
			}

			const FVector WorldPosition = ComponentWorld.TransformPosition(Particle->Location);
			const FVector ToCamera = WorldPosition - ViewContext.CameraPosition;
			const float DistanceSquared = ToCamera.Dot(ToCamera);
			const float Rotation = std::isfinite(Particle->Rotation) ? Particle->Rotation : 0.0f;

			const FRotator ParticleRotation(0.0f, FMath::RadiansToDegrees(Rotation), 0.0f);
			const FMatrix ParticleLocalMatrix = FTransform(Particle->Location, MeshTypeDataModule->MeshScale, ParticleRotation).GetLocalMatrix();
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

		return;
	}

	// Sprite Emitter
	if (!QuadMesh || !Material)
	{
		return;
	}

	FDynamicSpriteEmitterReplayData ReplayData;
	EmitterInstance.GetReplayData(ReplayData);

	ReplayData.ComponentToWorld = GetWorldMatrix();
	ReplayData.ComponentScale = GetWorldScale3D();
	ReplayData.Material = Material;

	if (!SpriteEmitterData.BuildVertexData(ReplayData, ViewContext))
	{
		return;
	}

	ParticleParams.CurrentFrame = 0.0f;
	ParticleParams.AtlasRowSize = 1.0f;
	ParticleParams.AtlasColSize = 1.0f;
	ParticleParams.Alpha = 1.0f;
	ParticleParams.Tint = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	ParticleParams.DecodeSRGB = 0.0f;

	FRenderPacket Packet;
	Packet.Mesh = QuadMesh;
	Packet.Material = Material;
	Packet.Model = RenderQueue.StoreWorldMatrix(FMatrix::Identity);
	Packet.VertexBufferOverride = SpriteEmitterData.GetVertexBuffer();
	Packet.IndexBufferOverride = SpriteEmitterData.GetIndexBuffer();
	Packet.IndexCount = SpriteEmitterData.GetIndexCount();
	Packet.CameraToParticleDistance = SpriteEmitterData.GetSortDistance();
	Packet.MaterialParamData = &ParticleParams;
	Packet.MaterialParamDataSize = sizeof(FSubUVConstants);

	RenderQueue.Add(Packet);
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

	UParticleEmitter* Emitter = Template->GetEmitter(0);

	if (Emitter)
	{
		UParticleModuleTypeDataMesh* MeshTypeData = Emitter->CreateMeshTypeData();
		MeshTypeData->Mesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
		MeshTypeData->MeshScale = FVector(10.0f, 10.0f, 10.0f);
	}

	UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (!Emitter || !LODLevel)
	{
		return;
	}

	MeshTypeDataModule = Emitter->GetMeshTypeDataModule();
	const int32 MaxParticles = std::clamp(Emitter->GetMaxActiveParticles(), 1, 65536);
	if (!EmitterInstance.Initialize(static_cast<uint32>(MaxParticles)))
	{
		EmitterInstance.Reset();
		return;
	}

	if (!MeshTypeDataModule && !SpriteEmitterData.Initialize(static_cast<uint32>(MaxParticles)))
	{
		EmitterInstance.Reset();
		return;
	}

	EmitterInstance.SetSpawnModule(LODLevel->GetSpawnModule());
	EmitterInstance.SetModules(LODLevel->GetModules());

	if (MeshTypeDataModule)
	{
		Material = nullptr;
	}
	else
	{
		UParticleModuleRequired* RequiredModule = LODLevel->GetRequiredModule();
		Material = RequiredModule ? Cast<UMaterial>(RequiredModule->Material) : nullptr;

		if (!Material || Material->PSOType != EPSOType::Particle_Sprite)
		{
			Material = UAssetManager::GetAssetByPath<UMaterial>("ParticleSpriteMaterial");
			if (RequiredModule && Material)
			{
				RequiredModule->Material = Material;
			}
		}
	}

	bEmitterInitialized = true;
}