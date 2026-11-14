#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "BillboardComponent.h"

#include "Engine/World.h"
#include <random>

namespace
{
	static std::mt19937 Generator{
		std::random_device{}()
	};
	static std::uniform_real_distribution<float> Distribution(0.0f, 1.0f);

	float RandomRange(float Min, float Max) {
		return Min + Distribution(Generator) * (Max - Min);
	}
}

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

	for (int32 i = 0; i < ParticleCount; i++)
	{
		FBaseParticle Particle{};
		InitParticle(Particle);
		Particles.Add(Particle);
	}
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	for (auto & Particle : Particles)
	{
		Particle.RelativeTime += DeltaTime / Particle.Lifetime;
		
		if (Particle.RelativeTime >= 1.0f)
		{
			InitParticle(Particle);
		}


		if (Particle.RelativeTime < FadeInEnd)
		{
			float Fade = Particle.RelativeTime / FadeInEnd;
			Particle.Color.A = ParticleColor.A * FMath::Lerp(0.0, 1.0, Fade);
		}
		else if (Particle.RelativeTime > FadeOutStart)
		{
			float Fade = (Particle.RelativeTime - FadeOutStart) / (1 - FadeOutStart);
			Particle.Color.A = ParticleColor.A * FMath::Clamp(1 - FMath::Lerp(0.0, 1.0, Fade), 0.0, 1.0);
		}
		else
		{
			Particle.Color.A = ParticleColor.A * 1.0f;
		}

		Particle.Location += Particle.Velocity * DeltaTime;
	}
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	float Extent = 10.0f;
	return FBox{ FVector(-Extent, -Extent, -Extent), FVector(Extent, Extent, Extent) };
}

void UParticleSystemComponent::InitParticle(FBaseParticle& Particle)
{
	FVector WorldLocation = GetWorldLocation();
	Particle.Location = { WorldLocation.X + RandomRange(-SpreadRadius, SpreadRadius), WorldLocation.Y + RandomRange(-SpreadRadius, SpreadRadius), WorldLocation.Z };
	Particle.Velocity = { RandomRange(-SpreadRadius, SpreadRadius), RandomRange(-SpreadRadius, SpreadRadius), RandomRange(MinRiseSpeed, MaxRiseSpeed) };
	Particle.Lifetime = RandomRange(MinLifetime, MaxLifetime);
	Particle.Size = RandomRange(MinSize, MaxSize);
	Particle.Color = ParticleColor;
	Particle.RelativeTime = 0.0f;
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
		Constant.Tint = FVector4(Particles[i].Color.R, Particles[i].Color.G, Particles[i].Color.B, 1.0f);
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
