#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"

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
	if (QuadMesh == nullptr || Material == nullptr || Particles.IsEmpty())
	{
		return;
	}

	Constants.Reset();
	Constants.Reserve(Particles.Num());
	
	for (int32 i = 0; i < Particles.Num(); i++)
	{
		// GPU에 하나만 올려둔 사각형을 파티클마다 어떻게 놓을지 행렬 만들어 큐에 담기
		FMatrix WorldMatrix = ViewContext.BuildBillboardMatrix(Particles[i].Location, Particles[i].Size, Particles[i].Size);

		FVector Delta = Particles[i].Location - ViewContext.CameraPosition;
		float DistSquared = Delta.Dot(Delta);

		// FSubUVConstatns 데이터 채우기 
		FSubUVConstants Constant;
		Constant.CurrentFrame = 0.0f;
		Constant.AtlasColSize = 1.0f;
		Constant.AtlasRowSize = 1.0f;
		Constant.Alpha = Particles[i].Color.A;
		Constant.DecodeSRGB = 0.0f;
		Constants.Add(Constant);

		// RenderQueue 에 보낼 데이터 넣기 
		FRenderPacket Packet;
		Packet.Model = RenderQueue.StoreWorldMatrix(WorldMatrix);
		Packet.Mesh = QuadMesh;
		Packet.Material = Material;
		Packet.CameraToParticleDistance = DistSquared;
		Packet.MaterialParamData = &Constants[i];
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);

		RenderQueue.Add(Packet);
	}
}

void UParticleSystemComponent::SubmitParticleToRenderQueue(
	FRenderQueue& RenderQueue,
	const int32 ParticleIndex,
	const FMatrix& WorldMatrix,
	const float CameraDistanceSquared)
{
}
