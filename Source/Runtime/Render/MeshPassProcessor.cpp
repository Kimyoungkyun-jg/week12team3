#include "EnginePCH.h"
#include "Render/MeshPassProcessor.h"
#include "Render/SceneRenderData.h"
#include "Render/SceneView.h"
#include "Render/RenderResourceManager.h"
#include "Render/Mesh.h"
#include "Render/NormalMapUtils.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/EngineTimer.h"
#include <algorithm>
#include <cstring>

namespace
{
	float GetNormalEncoding(UTexture2D* Texture)
	{
		return NormalMapUtils::GetEncoding(
			Texture ? Texture->GetResource() : nullptr);
	}

	const FMatrix& PacketWorld(const FRenderPacket& Packet)
	{
		if (Packet.Proxy) return Packet.Proxy->GetLocalToWorld();
		return Packet.Model ? *Packet.Model : FMatrix::Identity;
	}

	FForwardMaterialConstants MakeForwardMaterial(const UMaterial& Material)
	{
		FForwardMaterialConstants Params{};
		Params.BaseColor = Material.BaseColor;
		const FVector Specular = Material.SpecularColor * Material.SpecularFactor;
		Params.SpecularColorShininess = FVector4(Specular.X, Specular.Y, Specular.Z, std::max(1.0f, Material.Shininess));
		const FVector Emissive = Material.EmissiveColor * Material.EmissiveFactor;
		Params.Emissive = FVector4(Emissive.X, Emissive.Y, Emissive.Z, 0.0f);
		UTexture2D* Color = Material.Textures.IsEmpty() ? nullptr : Material.Textures[0];
		UTexture2D* Normal = Material.Textures.Num() > 1 ? Material.Textures[1] : nullptr;
		Params.SurfaceParams = FVector4(Material.bFlipNormalY ? 1.0f : 0.0f, 0.0f,
			Color && Color->GetResource() && Color->GetResource()->GetSRV() ? 1.0f : 0.0f, GetNormalEncoding(Normal));
		// 누적 시간은 double로 곱한 뒤 UV 한 주기로 줄여서 float로 넘긴다. 큰 float 값은 정밀도가 떨어진다.
		// 이동량은 타일링 뒤에 더해지므로(ResolveMaterialUV), 샘플러가 Wrap이면 한 주기를 뺀 값도 결과가 같다.
		const double TotalTime = EngineTimer::GetTotalTime();
		const FVector2 UV(static_cast<float>(std::fmod(Material.UVScrollSpeed.X * TotalTime, 1.0)),
			static_cast<float>(std::fmod(Material.UVScrollSpeed.Y * TotalTime, 1.0)));
		Params.UVTransform = FVector4(UV.X, UV.Y, Material.UVTiling.X, Material.UVTiling.Y);
		Params.WorldUVParams = FVector4(Material.WorldUVTileSize.X, Material.WorldUVTileSize.Y,
			Material.bWorldUVLocalSpace ? 1.0f : 0.0f, Material.BlendState == EBlendState::Opaque ? 1.0f : 0.0f);
		return Params;
	}

	FStaticMeshMaterialParams MakeStaticMaterial(const UMaterial& Material)
	{
		const auto Forward = MakeForwardMaterial(Material);
		FStaticMeshMaterialParams Params{};
		Params.BaseColor = Forward.BaseColor;
		Params.UVOffset = FVector2(Forward.UVTransform.X, Forward.UVTransform.Y);
		Params.TextureFlags = FVector2(Forward.SurfaceParams.W, Forward.SurfaceParams.X);
		Params.UVTiling = Material.UVTiling;
		Params.WorldUVTileSize = Material.WorldUVTileSize;
		Params.Emissive = Forward.Emissive;
		Params.SpecularColorShininess = Forward.SpecularColorShininess;
		Params.WorldUVParams = FVector4(Material.bWorldUVLocalSpace ? 1.0f : 0.0f,
			Material.BlendState == EBlendState::Opaque ? 1.0f : 0.0f,
			Material.bOverrideShadingModel ? 1.0f : 0.0f, static_cast<float>(Material.ShadingModel));
		return Params;
	}

	uint64 GroupKey(const FStaticDrawGroup& Group)
	{
		return (static_cast<uint64>(Group.Material->SortID) << 47)
			| (static_cast<uint64>(Group.Mesh->SortID) << 31)
			| (static_cast<uint64>(Group.LODIndex & 0x3) << 29);
	}

	FShaderProgram* GetMaterialForwardShader(const UMaterial& Material, const FSceneRenderOptions& Options, bool bGPUSkinning = false)
	{
		FSceneRenderOptions Effective = Options;
		if (!Options.bUnlit && !Options.bOverrideMaterialLighting && Material.bOverrideShadingModel)
			Effective.SetLegacyLightingModel(Material.ShadingModel);
		return FRenderResourceManager::GetForwardShaderProgram(Effective.ReflectionModel, Effective.ShadingModel, bGPUSkinning, Effective.bUnlit);
	}

	bool BuildBatch(FMeshDrawBatch& Batch, UStaticMesh* Mesh, uint8 LOD,
		UMaterial* Material, EMeshPassType Pass, const FSceneView& View, const FSceneRenderOptions& Options,
		const void* OverrideParams = nullptr, uint32 OverrideSize = 0)
	{
		if (!Mesh || !Material) return false;
		Batch.VertexBuffer = Mesh->GetVertexBuffer(LOD);
		Batch.IndexBuffer = Mesh->GetIndexBuffer(LOD);
		if (!Batch.VertexBuffer || !Batch.IndexBuffer) return false;
		const bool bUsesUberShader = Pass == EMeshPassType::Forward || Pass == EMeshPassType::WorldNormal;
		if (bUsesUberShader)
		{
			const bool bWorldNormal = Pass == EMeshPassType::WorldNormal;
			FPipelineState* Base = FRenderResourceManager::GetPSO(bWorldNormal
				? EPSOType::StaticMesh_WorldNormal : EPSOType::StaticMesh_Forward);
			FShaderProgram* Shader = bWorldNormal ? (Base ? Base->Shader : nullptr)
				: GetMaterialForwardShader(*Material, Options);
			if (!Base || !Shader || !Shader->VertexShader || !Shader->PixelShader) return false;
			Batch.Pipeline = *Base;
			Batch.Pipeline.Shader = Shader;
			Batch.bUseObjectSlots = false;
			Batch.bBindViewConstants = false;
			Batch.ObjectBindingSlot = 0;
			Batch.ObjectStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Batch.MaterialSlot = 2;
			Batch.MaterialStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Batch.SamplerStages = Batch.MaterialStages;
			Batch.bForceWireframe = !bWorldNormal && View.bIsWireframe;

			FForwardMaterialConstants Params = MakeForwardMaterial(*Material);
			std::memcpy(Batch.MaterialData.data(), &Params, sizeof(Params));
			Batch.MaterialDataSize = sizeof(Params);
		}
		else
		{
			EPSOType Type = Material->PSOType;
			if (Pass == EMeshPassType::GBuffer && Type == EPSOType::StaticMesh_Opaque)
				Type = EPSOType::StaticMesh_GBuffer;
			else if (View.bIsWireframe && Type == EPSOType::StaticMesh_Opaque)
				Type = EPSOType::StaticMesh_Wireframe;
			FPipelineState* Pipeline = FRenderResourceManager::GetPSO(Type);
			if (!Pipeline) return false;
			Batch.Pipeline = *Pipeline;
			Batch.bForceWireframe = Pass == EMeshPassType::GBuffer && View.bIsWireframe;
            Batch.ObjectStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			if (Material->PSOType == EPSOType::StaticMesh_Opaque ||
				Material->PSOType == EPSOType::StaticMesh_Translucent ||
				Material->PSOType == EPSOType::StaticMesh_Wireframe)
			{
				FStaticMeshMaterialParams Params = MakeStaticMaterial(*Material);
				std::memcpy(Batch.MaterialData.data(), &Params, sizeof(Params));
				Batch.MaterialDataSize = sizeof(Params);
			}
			else if (OverrideParams && OverrideSize <= Batch.MaterialData.size())
			{
				std::memcpy(Batch.MaterialData.data(), OverrideParams, OverrideSize);
				Batch.MaterialDataSize = OverrideSize;
			}
		}
		Batch.Sampler = Material->SamplerState;
		Batch.Textures.Reserve(Material->Textures.Num());
		for (int32 Index = 0; Index < Material->Textures.Num(); ++Index)
		{
			// Forward 조명은 t8 이상을 전역 리소스에 사용한다.
			if (bUsesUberShader && Index >= 8) break;
			UTexture2D* Texture = Material->Textures[Index];
			EShaderBindFlagBits Stages = EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 1)
				Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 0 &&
				(!Texture || !Texture->GetResource() || !Texture->GetResource()->GetSRV()))
				Texture = nullptr;
			if (bUsesUberShader && Index == 1 && GetNormalEncoding(Texture) <= 0.5f)
				Texture = nullptr;
			Batch.Textures.Add({ Texture, Stages });
		}
		return true;
	}

	bool BuildBatch(FMeshDrawBatch& Batch, USkeletalMesh* Mesh,
		UMaterial* Material, EMeshPassType Pass, const FSceneView& View, const FSceneRenderOptions& Options, bool bGPUSkinning, uint8 LOD,
		const void* OverrideParams = nullptr, uint32 OverrideSize = 0)
	{
		if (!Mesh || !Material) return false;
		Batch.VertexBuffer = Mesh->GetVertexBuffer(LOD);
		Batch.IndexBuffer = Mesh->GetIndexBuffer(LOD);
		if (!Batch.VertexBuffer || !Batch.IndexBuffer) return false;

		const bool bUsesUberShader = Pass == EMeshPassType::Forward || Pass == EMeshPassType::WorldNormal;
		if (bUsesUberShader)
		{
			const bool bWorldNormal = Pass == EMeshPassType::WorldNormal;
			FPipelineState* Base = FRenderResourceManager::GetPSO(bWorldNormal
				? EPSOType::StaticMesh_WorldNormal : EPSOType::StaticMesh_Forward);
			FShaderProgram* Shader = bWorldNormal ? FRenderResourceManager::GetWorldNormalShaderProgram(bGPUSkinning)
				: GetMaterialForwardShader(*Material, Options, bGPUSkinning);
			if (!Base || !Shader || !Shader->VertexShader || !Shader->PixelShader) return false;
			Batch.Pipeline = *Base;
			Batch.Pipeline.Shader = Shader;
			Batch.bUseObjectSlots = false;
			Batch.bBindViewConstants = false;
			Batch.ObjectBindingSlot = 0;
			Batch.ObjectStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Batch.MaterialSlot = 2;
			Batch.MaterialStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Batch.SamplerStages = Batch.MaterialStages;
			Batch.bForceWireframe = !bWorldNormal && View.bIsWireframe;

			FForwardMaterialConstants Params = MakeForwardMaterial(*Material);
			std::memcpy(Batch.MaterialData.data(), &Params, sizeof(Params));
			Batch.MaterialDataSize = sizeof(Params);
		}
		else
		{
			EPSOType Type = Material->PSOType;
			if (Pass == EMeshPassType::GBuffer && Type == EPSOType::StaticMesh_Opaque)
				Type = EPSOType::StaticMesh_GBuffer;
			else if (View.bIsWireframe && Type == EPSOType::StaticMesh_Opaque)
				Type = EPSOType::StaticMesh_Wireframe;
			FPipelineState* Pipeline = FRenderResourceManager::GetPSO(Type);
			if (!Pipeline) return false;
			Batch.Pipeline = *Pipeline;
			if (bGPUSkinning && Type == EPSOType::StaticMesh_Translucent)
			{
				Batch.Pipeline.Shader = FRenderResourceManager::GetTranslucentShaderProgram(true);
				if (!Batch.Pipeline.Shader) return false;
			}
			Batch.bForceWireframe = Pass == EMeshPassType::GBuffer && View.bIsWireframe;
            Batch.ObjectStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			if (Material->PSOType == EPSOType::StaticMesh_Opaque ||
				Material->PSOType == EPSOType::StaticMesh_Translucent ||
				Material->PSOType == EPSOType::StaticMesh_Wireframe)
			{
				FStaticMeshMaterialParams Params = MakeStaticMaterial(*Material);
				std::memcpy(Batch.MaterialData.data(), &Params, sizeof(Params));
				Batch.MaterialDataSize = sizeof(Params);
			}
			else if (OverrideParams && OverrideSize <= Batch.MaterialData.size())
			{
				std::memcpy(Batch.MaterialData.data(), OverrideParams, OverrideSize);
				Batch.MaterialDataSize = OverrideSize;
			}
		}
		Batch.Sampler = Material->SamplerState;
		Batch.Textures.Reserve(Material->Textures.Num());
		for (int32 Index = 0; Index < Material->Textures.Num(); ++Index)
		{
			// Forward 조명은 t8 이상을 전역 리소스에 사용한다.
			if (bUsesUberShader && Index >= 8) break;
			UTexture2D* Texture = Material->Textures[Index];
			EShaderBindFlagBits Stages = EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 1)
				Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 0 &&
				(!Texture || !Texture->GetResource() || !Texture->GetResource()->GetSRV()))
				Texture = nullptr;
			if (bUsesUberShader && Index == 1 && GetNormalEncoding(Texture) <= 0.5f)
				Texture = nullptr;
			Batch.Textures.Add({ Texture, Stages });
		}
		return true;
	}

	FMeshDrawBatch::FItem BuildItem(const FMatrix& World, const FSceneView& View,
		EMeshPassType Pass, uint32 Slot, uint32 StartIndex, uint32 IndexCount,
		const void* ObjectKey, bool bOccludedByGpu, bool bReceiveShadow, bool bReceiveDecal,
		const FPrimitiveSceneProxy* Proxy)
	{
		FMeshDrawBatch::FItem Item{};
		Item.ObjectSlot = Slot;
		Item.StartIndex = StartIndex;
		Item.IndexCount = IndexCount;
		Item.ObjectKey = ObjectKey;
		Item.bOccludedByGpu = bOccludedByGpu;
		Item.bReceiveDecal = bReceiveDecal;
		Item.Proxy = Proxy;

		if (Pass == EMeshPassType::Forward || Pass == EMeshPassType::WorldNormal)
		{
			FForwardObjectConstants Constants{};
			Constants.World = World;
			Constants.View = View.ViewMatrix;
			Constants.Projection = View.ProjectionMatrix;
			Constants.NormalMatrix = World.Inverse().GetTransposed();
			Constants.ShadowFlags.X = static_cast<float>(bReceiveShadow);
			Constants.ShadowFlags.Y = static_cast<float>(bReceiveDecal);
			std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
			Item.ObjectDataSize = sizeof(Constants);
		}
		else
		{
			const FPerObjectConstants Constants = MakePerObjectConstants(World, bReceiveShadow, bReceiveDecal);
			std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
			Item.ObjectDataSize = sizeof(Constants);
		}
		return Item;
	}

	FMeshDrawBatch::FItem BuildBatchItem(const FMatrix& World, const FMatrix& View, const FMatrix& Projection,
		uint32 Slot, uint32 StartIndex, uint32 IndexCount,
		const void* ObjectKey, bool bOccludedByGpu)
	{
		FMeshDrawBatch::FItem Item{};
		Item.ObjectSlot = Slot;
		Item.StartIndex = StartIndex;
		Item.IndexCount = IndexCount;
		Item.ObjectKey = ObjectKey;
		Item.bOccludedByGpu = bOccludedByGpu;
		Item.bReceiveDecal = true;

		FForwardObjectConstants Constants{};
		Constants.World = World;
		Constants.View = View;
		Constants.Projection = Projection;
		Constants.NormalMatrix = World.Inverse().GetTransposed();
		Constants.ShadowFlags.Y = 1.0f; // bReceiveDecal
		std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
		Item.ObjectDataSize = sizeof(Constants);
		
		return Item;
	}

}

bool FMeshPassProcessor::Build(const FSceneView& View, const FRenderQueue& Queue,
	const FSceneRenderData* SceneData, EMeshPassType Pass,
	TArray<FMeshDrawBatch>& Out, const FSceneRenderOptions& Options) const
{
	Out.Reset();
	if (Pass != EMeshPassType::Translucent && SceneData)
	{
		TArray<const FStaticDrawGroup*> Groups;
		for (const FSceneRenderData::FGatherChunk& Chunk : SceneData->GatherChunks)
			for (const FStaticDrawGroup& Group : Chunk.Groups)
				if (!Group.Items.empty()) Groups.Add(&Group);

		std::sort(Groups.begin(), Groups.end(),
			[](const FStaticDrawGroup* A, const FStaticDrawGroup* B) { return GroupKey(*A) < GroupKey(*B); }); // 재질, 메시, LOD 순으로 정렬

		for (const FStaticDrawGroup* Group : Groups)
		{
			FMeshDrawBatch Batch; // Render에게 전달하는 최종 구조체
			if (!BuildBatch(Batch, Group->Mesh, Group->LODIndex, Group->Material, Pass, View, Options))
				return false;

			Batch.Items.Reserve(static_cast<uint32>(Group->Items.size()));

			for (const FStaticDrawItem& Item : Group->Items)
				Batch.Items.Add(BuildItem(Item.Proxy->GetLocalToWorld(), View, Pass, Item.Slot,
					Item.StartIndex, Item.IndexCount, Item.Proxy, Item.bOccludedByGpu != 0, Item.Proxy->bReceiveShadow, Item.Proxy->bReceiveDecal, Item.Proxy));
			Out.Add(std::move(Batch));
		}
	}
	const uint32 Begin = Pass == EMeshPassType::Translucent ? Queue.GetFirstTranslucentIndex() : 0;
	const uint32 End = Pass == EMeshPassType::Translucent ? Queue.Num() : Queue.GetFirstTranslucentIndex();
	Out.Reserve(Out.Num() + End - Begin);
	for (uint32 Index = Begin; Index < End; ++Index)
	{
		const FRenderPacket& Packet = Queue[Index];
		if (Pass == EMeshPassType::WorldNormal &&
			(!Packet.Material || Packet.Material->PSOType != EPSOType::StaticMesh_Opaque)) continue;
		FMeshDrawBatch Batch;
		bool bBatchBuilt = false;
		if (Packet.Mesh)
		{
			bBatchBuilt = BuildBatch(Batch, Packet.Mesh, Packet.LODIndex, Packet.Material, Pass, View,
				Options, Packet.MaterialParamData, Packet.MaterialParamDataSize);
		}
		else if (Packet.SkeletalMesh)
		{
			const bool bGPUSkinning = Packet.SkinningMatricesBuffer != nullptr;
			bBatchBuilt = BuildBatch(Batch, Packet.SkeletalMesh, Packet.Material, Pass, View,
				Options, bGPUSkinning, Packet.LODIndex, Packet.MaterialParamData, Packet.MaterialParamDataSize);
			if (bBatchBuilt && Packet.SkeletalVertexBuffer)
				Batch.VertexBuffer = Packet.SkeletalVertexBuffer;
			if (bBatchBuilt)
			{
				Batch.SkinningMatricesBuffer = Packet.SkinningMatricesBuffer;
				Batch.bSkeletalMesh = true;
				Batch.bComputeSkinned = Packet.bComputeSkinned;
			}
		}
		if (!bBatchBuilt) return false;

		const uint32 IndexCount = Packet.IndexCount ? Packet.IndexCount : Batch.IndexBuffer->GetIndexCount();
		const void* ObjectKey = Packet.Proxy ? static_cast<const void*>(Packet.Proxy) : static_cast<const void*>(&Packet);

		Batch.Items.Add(BuildItem(PacketWorld(Packet), View, Pass, Packet.Slot,
			Packet.StartIndex, IndexCount, ObjectKey, Packet.bOccludedByGpu, !Packet.Proxy || Packet.Proxy->bReceiveShadow, !Packet.Proxy || Packet.Proxy->bReceiveDecal, Packet.Proxy));
		Out.Add(std::move(Batch));
	}
	return true;
}

bool FMeshPassProcessor::Build(const TArray<FPrimitiveSceneProxy*>& ShadowMeshes, EPSOType PSOType, TArray<FMeshDrawBatch>& OutBatches,
	const FVector& PreTranslation)
{
	OutBatches.Reset();

	const FPipelineState* Pipeline = FRenderResourceManager::GetPSO(PSOType);

	if (!Pipeline || !Pipeline->Shader || !Pipeline->Shader->VertexShader)
	{
		return false;
	}

	for (FPrimitiveSceneProxy* Proxy : ShadowMeshes)
	{
		if (!Proxy || !Proxy->GetRenderState())
		{
			continue;
		}

		if (Proxy->GetLODCount() == 0)
		{
			continue;
		}

		if (!Proxy->bCastsShadow) continue;

		// 우선 LOD 0 사용
		constexpr uint8 LOD = 0;
		const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);

		FMeshDrawBatch Batch{};
		Batch.Pipeline = *Pipeline;
		Batch.VertexBuffer = Proxy->GetVertexBuffer(LOD);
		Batch.IndexBuffer = Proxy->GetIndexBuffer(LOD);
		if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Proxy->GetComponent()))
		{
			Batch.bSkeletalMesh = true;
			Batch.bComputeSkinned = SkelComp->GetAppliedSkinningMode() == ESkinningMode::Compute;
			Batch.SkinningMatricesBuffer = SkelComp->GetSkinningMatricesBuffer();
			if (Batch.SkinningMatricesBuffer)
			{
				Batch.VertexBuffer = Proxy->GetSkeletalMesh()->GetVertexBuffer(LOD);
				Batch.Pipeline.Shader = FRenderResourceManager::GetGPUSkinningShadowDepthShaderProgram();
			}
		}
		Batch.bUseObjectSlots = false;
		Batch.bBindViewConstants = true;
		Batch.ObjectBindingSlot = 2;
		Batch.ObjectStages = EShaderBindFlagBits::Vertex;

		if (!Batch.VertexBuffer || !Batch.IndexBuffer || !Batch.Pipeline.Shader)
		{
			continue;
		}

		for (uint32 Index = 0; Index < CachedLOD.NumSections; ++Index)
		{
			const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + Index);

			if (Section.IndexCount == 0)
			{
				continue;
			}

			FMeshDrawBatch::FItem Item{};
			Item.StartIndex = Section.StartIndex;
			Item.IndexCount = Section.IndexCount;
			Item.ObjectKey = Proxy;

			// ShadowDepth.hlsl은 법선(NormalMatrix)으로 기울기 bias를 구하므로 World만이 아니라 오브젝트 상수 전체를 올린다.
			FPerObjectConstants Constants = MakePerObjectConstants(Proxy->GetLocalToWorld());
			Constants.World.M[3][0] += PreTranslation.X;
			Constants.World.M[3][1] += PreTranslation.Y;
			Constants.World.M[3][2] += PreTranslation.Z;
			std::memcpy(Item.ObjectData.data(), &Constants, sizeof(Constants));
			Item.ObjectDataSize = sizeof(Constants);

			Batch.Items.Add(std::move(Item));
		}

		if (!Batch.Items.IsEmpty())
		{
			OutBatches.Add(std::move(Batch));
		}
	}

	return true;
}

