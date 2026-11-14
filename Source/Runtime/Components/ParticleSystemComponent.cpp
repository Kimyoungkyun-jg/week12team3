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

void UParticleSystemComponent::SubmitSpriteEmitter(const FParticleEmitterInstance& Instance, FRenderQueue& RenderQueue,
	const FViewContext& ViewContext, uint32& ConstantIndex)
{
	const uint32 FirstConstant = ConstantIndex;
	ConstantIndex += static_cast<uint32>(Instance.ActiveParticles);

	UStaticMesh* Quad = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	const auto* Required = Instance.CurrentLODLevel->RequiredModule;
	UMaterial* Material = Required && Required->Material
		? Required->Material
		: UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");

	if (!Quad || !Material)
	{
		return;
	}

	for (int32 Index = 0; Index < Instance.ActiveParticles; ++Index)
	{
		const FBaseParticle& Particle = Instance.GetActiveParticle(Index);
		const FMatrix World = ViewContext.BuildBillboardMatrix(Particle.Location, Particle.Size.X, Particle.Size.Y);
		const FVector CameraDelta = Particle.Location - ViewContext.CameraPosition;

		FRenderPacket Packet{};
		Packet.Mesh = Quad;
		Packet.Material = Material;
		Packet.Model = RenderQueue.StoreWorldMatrix(World);
		Packet.CameraToParticleDistance = CameraDelta.Dot(CameraDelta);
		Packet.MaterialParamData = &SpriteConstants[FirstConstant + static_cast<uint32>(Index)];
		Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
		RenderQueue.Add(Packet);
	}
}

void UParticleSystemComponent::SubmitMeshEmitter(const FParticleEmitterInstance& Instance, const UParticleModuleTypeDataMesh& TypeData,
	FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	UStaticMesh* Mesh = TypeData.Mesh;
	constexpr uint8 MeshLODIndex = 0;

	if (!Mesh || !Mesh->GetVertexBuffer(MeshLODIndex) || !Mesh->GetIndexBuffer(MeshLODIndex))
	{
		return;
	}

	const FStaticMeshData& MeshData = Mesh->GetMeshData(MeshLODIndex);

	for (int32 Index = 0; Index < Instance.ActiveParticles; ++Index)
	{
		const FBaseParticle& Particle = Instance.GetActiveParticle(Index);

		FMatrix World = FMatrix::MakeTranslation(Particle.Location);

		// Scale
		constexpr float MeshScale = 0.2f;
		World.M[0][0] = Particle.Size.X * MeshScale;
		World.M[1][1] = Particle.Size.Y * MeshScale;
		World.M[2][2] = Particle.Size.Z * MeshScale;

		const FMatrix* StoredWorld = RenderQueue.StoreWorldMatrix(World);
		const FVector CameraDelta = Particle.Location - ViewContext.CameraPosition;
		const float DistanceSquared = CameraDelta.Dot(CameraDelta);

		for (const FStaticMeshSection& Section : MeshData.Sections)
		{
			UMaterial* Material = Mesh->GetMaterial(Section.MaterialSlotIndex);
			if (!Material || Section.IndexCount == 0)
			{
				continue;
			}

			FRenderPacket Packet{};
			Packet.Mesh = Mesh;
			Packet.Model = StoredWorld;
			Packet.Material = Material;
			Packet.StartIndex = Section.StartIndex;
			Packet.IndexCount = Section.IndexCount;
			Packet.LODIndex = MeshLODIndex;
			Packet.CameraToParticleDistance = DistanceSquared;
			RenderQueue.Add(Packet);
		}
	}
}

void UParticleSystemComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	const UWorld* World = GetWorld();
	if (World && World->GetWorldType() == EWorldType::Editor && !ShouldPreviewInEditor())
	{
		return;
	}

	uint32 ConstantIndex = 0;

	for (const auto& Instance : EmitterInstances)
	{
		const auto* TypeMeshData = Instance->CurrentLODLevel->TypeDataModule;

		if (!TypeMeshData)
		{
			SubmitSpriteEmitter(*Instance, RenderQueue, ViewContext, ConstantIndex);
		}
		else if (const auto* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(TypeMeshData))
		{
			SubmitMeshEmitter(*Instance, *MeshTypeData, RenderQueue, ViewContext);
		}
	}

}