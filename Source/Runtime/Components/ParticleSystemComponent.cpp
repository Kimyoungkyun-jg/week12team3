#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Asset/AssetManager.h"

#include <algorithm>

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
	CreateInitialParticle();
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	EmitterInstance.Tick(DeltaTime);
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

	if (!QuadMesh || !Material || ParticleCount == 0)
	{
		return;
	}

	const FMatrix ComponentWorldMatrix = GetWorldMatrix();
	const FVector ComponentScale = GetWorldScale3D();

	ParticleConstants.Reset();
	ParticleConstants.Reserve(ParticleCount);

	for (int32 Index = 0; Index < ParticleCount; ++Index)
	{
		const FBaseParticle* Particle = EmitterInstance.GetParticle(Index);

		if (!Particle)
		{
			continue;
		}

		FSubUVConstants& Params = ParticleConstants.AddDefaulted_GetRef();
		Params.CurrentFrame = 0.0f;
		Params.AtlasRowSize = 1.0f;
		Params.AtlasColSize = 1.0f;
		Params.Alpha = Particle->Color.A;
		Params.Tint = FVector4(Particle->Color.R, Particle->Color.G, Particle->Color.B, 1.0f);
		Params.DecodeSRGB = 0.0f;

		const FVector WorldPosition = ComponentWorldMatrix.TransformPosition(Particle->Location);
		const float Width = Particle->Size.X * ComponentScale.Y;
		const float Height = Particle->Size.Y * ComponentScale.Z;

		const FMatrix BillboardMatrix = ViewContext.BuildBillboardMatrix(WorldPosition, Width, Height);
		const FVector Delta = WorldPosition - ViewContext.CameraPosition;

		FRenderPacket Packet;
		Packet.Model = RenderQueue.StoreWorldMatrix(BillboardMatrix);
		Packet.Mesh = QuadMesh;
		Packet.Material = Material;
		Packet.CameraToParticleDistance = Delta.Dot(Delta);
		Packet.MaterialParamData = &Params;
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);

		RenderQueue.Add(Packet);
	}
}

void UParticleSystemComponent::CreateInitialParticle()
{
	if (!EmitterInstance.Initialize(1))
	{
		return;
	}
	FBaseParticle* Particle = EmitterInstance.SpawnParticle();
	if (!Particle)
	{
		return;
	}
	Particle->Location = FVector::ZeroVector;
	Particle->Velocity = FVector::ZeroVector;
	Particle->Size = FVector(40.0f, 40.0f, 1.0f);
	Particle->Color = FColor(255, 255, 255, 255);
	Particle->RelativeTime = 0.0f;
	Particle->Lifetime = 5.0f;
}