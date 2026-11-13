#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Asset/AssetManager.h"

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

	if (!QuadMesh || !Material || ParticleCount == 0 || !bEmitterInitialized)
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
	UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (!Emitter || !LODLevel)
	{
		return;
	}

	const int32 MaxParticles = std::clamp(Emitter->GetMaxActiveParticles(), 1, 65536);
	if (!EmitterInstance.Initialize(static_cast<uint32>(MaxParticles)))
	{
		EmitterInstance.Reset();
		return;
	}

	EmitterInstance.SetSpawnModule(LODLevel->GetSpawnModule());
	EmitterInstance.SetModules(LODLevel->GetModules());

	UParticleModuleRequired* RequiredModule = LODLevel->GetRequiredModule();
	Material = RequiredModule ? Cast<UMaterial>(RequiredModule->Material) : nullptr;

	if (!Material)
	{
		Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
		if (RequiredModule && Material)
		{
			RequiredModule->Material = Material;
		}
	}

	bEmitterInitialized = true;
}