#include "EnginePCH.h"
#include "Engine/ParticleSceneProxy.h"
#include "Components/ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderPacket.h"
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
	if (!Component) return;
	bShownInGame = Component->IsShown(true);
	bShownInEditor = Component->IsShown(false);
	const FMatrix LocalToWorld = Component->GetWorldMatrix();
	const FVector Scale = Component->GetWorldScale3D();
	bool bHasBounds = false;
	for (const FParticleEmitterInstance& Emitter : Component->GetEmitterInstances())
	{
		const UParticleLODLevel* LOD = Emitter.CurrentLODLevel;
		if (!LOD || !LOD->bEnabled || !LOD->RequiredModule ||
			!LOD->TypeDataModule || LOD->TypeDataModule->GetEmitterType() != EParticleEmitterType::Sprite) continue;
		const bool bLocal = LOD->RequiredModule->bUseLocalSpace;
		for (const FBaseParticle& Particle : Emitter.Particles)
		{
			FParticleSpriteRenderData& Sprite = Sprites.AddDefaulted_GetRef();
			Sprite.Location = bLocal ? LocalToWorld.TransformPosition(Particle.Location) : Particle.Location;
			Sprite.Width = std::abs(Particle.Size.X * (bLocal ? Scale.Y : 1.0f));
			Sprite.Height = std::abs(Particle.Size.Y * (bLocal ? Scale.Z : 1.0f));
			Sprite.Rotation = Particle.Rotation;
			Sprite.Constants.CurrentFrame = 0.0f;
			Sprite.Constants.AtlasRowSize = 1.0f;
			Sprite.Constants.AtlasColSize = 1.0f;
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
		Packet.Material = Material;
		Packet.Model = Queue.StoreWorldMatrix(World);
		const FVector Delta = Sprite.Location - View.CameraPosition;
		Packet.CameraToParticleDistance = Delta.Dot(Delta);
		Packet.MaterialParamData = &Sprite.Constants;
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
		Queue.Add(Packet);
	}
}
