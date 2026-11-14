#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"

#include "Particles/ParticleSystem.h" 
#include "Particles/ParticleModule.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Asset/AssetManager.h"
#include "Render/RenderPacket.h"
#include "Engine/World.h"

namespace
{
	class FParticleSystemSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		explicit FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
		{
		}

		void UpdateRenderState() override
		{
			bVisible = Component->IsShown(false);
			bHiddenInGame = !Component->IsShown(true);
			bCastsShadow = false; 
			bReceiveShadow = false;
			bReceiveDecal = false;
		}

		FVertexBuffer* GetVertexBuffer(uint8) const override { return nullptr; }
		FIndexBuffer* GetIndexBuffer(uint8) const override { return nullptr; }
	};
}

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	EmitterInstances.Reset();
	SpriteConstants.Reset();
	Template = InTemplate;

	if (!Template)
	{
		return;
	}

	for (UParticleEmitter* Emitter : Template->Emitters)
	{
		if (!Emitter || Emitter->LODLevels.IsEmpty() || !Emitter->LODLevels[0])
		{
			continue;
		}

		Emitter->CacheEmitterModuleInfo();

		auto Instance = std::make_unique<FParticleEmitterInstance>();
		Instance->SpriteTemplate = Emitter;
		Instance->Component = this;
		Instance->CurrentLODLevelIndex = 0;
		Instance->CurrentLODLevel = Emitter->LODLevels[0];
		Instance->InitStorage(4);

		EmitterInstances.Add(std::move(Instance));
	}
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	const FVector EmitterLocation = GetWorldLocation();
	SpriteConstants.Reset();

	for (const auto& Instance : EmitterInstances)
	{
		Instance->Tick(DeltaTime, EmitterLocation);

		if (Instance->CurrentLODLevel->TypeDataModule)
		{
			continue;
		}

		// 비어 있는 기본 Sprite 이미터를 그림
		for (int32 Index = 0; Index < Instance->ActiveParticles; ++Index)
		{
			const FBaseParticle& Particle = Instance->GetActiveParticle(Index);
			const FVector4 Color = Particle.Color.ToVector4();

			FSubUVConstants Constants{};
			Constants.AtlasRowSize = 8.0f;
			Constants.AtlasColSize = 8.0f;
			Constants.CurrentFrame = 0.0f;
			Constants.Alpha = Color.W;
			Constants.Tint = Color;
			Constants.DecodeSRGB = 0.0f;

			SpriteConstants.Add(Constants);
		}
	}
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	constexpr float Extent = 64.0f;
	return FBox{
		FVector(-Extent, -Extent, -Extent),
		FVector(Extent, Extent, Extent)
	};
}

void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	const UWorld* World = GetWorld();
	if (World && World->GetWorldType() == EWorldType::Editor && !ShouldPreviewInEditor())
	{
		return;
	}

	UStaticMesh* Quad = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");

	if (!Quad || !DefaultMaterial)
	{
		return;
	}

	uint32 ConstantIndex = 0;

	for (const auto& Instance : EmitterInstances)
	{
		if (Instance->CurrentLODLevel->TypeDataModule)
		{
			continue;
		}

		const auto* Required = Instance->CurrentLODLevel->RequiredModule;
		UMaterial* Material = Required && Required->Material
			? Required->Material
			: DefaultMaterial;

		for (int32 Index = 0; Index < Instance->ActiveParticles; ++Index)
		{
			const FBaseParticle& Particle = Instance->GetActiveParticle(Index);
			const FMatrix World = ViewContext.BuildBillboardMatrix(Particle.Location, Particle.Size.X, Particle.Size.Y);
			const FVector CameraDelta = Particle.Location - ViewContext.CameraPosition;

			FRenderPacket Packet{};
			Packet.Mesh = Quad;
			Packet.Material = Material;
			Packet.Model = RenderQueue.StoreWorldMatrix(World);
			Packet.CameraToParticleDistance = CameraDelta.Dot(CameraDelta);
			Packet.MaterialParamData = &SpriteConstants[ConstantIndex];
			Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
			RenderQueue.Add(Packet);

			++ConstantIndex;
		}
	}
}