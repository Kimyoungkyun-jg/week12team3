#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "BillboardComponent.h"

#include "Engine/World.h"


UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	Material = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
}

UParticleSystemComponent::~UParticleSystemComponent()
{
}

void UParticleSystemComponent::BeginPlay()
{
	Super::BeginPlay();
	Particles.Reset();

	FBaseParticle Particle{};
	Particle.Size = 10.0f;
	Particle.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
	Particle.Location = GetWorldLocation();
	Particles.Add(Particle);
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	float Extent = 10.0f;
	return FBox{ FVector(-Extent, -Extent, -Extent), FVector(Extent, Extent, Extent) };
}


void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

void UParticleSystemComponent::SubmitParticleToRenderQueue(
	FRenderQueue& RenderQueue,
	const int32 ParticleIndex,
	const FMatrix& WorldMatrix,
	const float CameraDistanceSquared)
{
}
