#include "EnginePCH.h"
#include "Render/MeshPassProcessor.h"
#include "Render/SceneRenderData.h"
#include "Render/SceneView.h"
#include "Render/RenderResourceManager.h"
#include "Render/Mesh.h"
#include "Render/NormalMapUtils.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Core/EngineTimer.h"
#include <algorithm>
#include <bit>
#include <cstring>

namespace
{
	float GetNormalEncoding(UTexture2D* Texture)
	{
		return NormalMapUtils::GetEncoding(Texture ? Texture->GetResource() : nullptr);
	}

	const FMatrix& ResolveElementWorld(const FMeshBatchElement& Element)
	{
		if (Element.WorldMatrix)
			return *Element.WorldMatrix;
		return Element.Proxy ? Element.Proxy->GetLocalToWorld() : FMatrix::Identity;
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

	uint64 GroupSortKey(const FStaticDrawGroup& Group)
	{
		return (static_cast<uint64>(Group.Material->SortID) << 47)
			| (static_cast<uint64>(Group.Mesh->SortID) << 31)
			| (static_cast<uint64>(Group.LODIndex & 0x3) << 29);
	}

	uint64 BatchOpaqueSortKey(const UMaterial& Material, uint16 MeshSortID, uint8 LODIndex)
	{
		return (static_cast<uint64>(Material.SortID) << 47)
			| (static_cast<uint64>(MeshSortID) << 31)
			| (static_cast<uint64>(LODIndex & 0x3) << 29);
	}

	uint64 MakeTranslucentSortKey(float CameraDistanceSq)
	{
		const uint64 DistanceBits = static_cast<uint64>(std::bit_cast<uint32>(CameraDistanceSq));
		return (1ull << 63) | (~DistanceBits & 0xFFFFFFFFull);
	}

	FShaderProgram* GetMaterialForwardShader(const UMaterial& Material, const FSceneRenderOptions& Options, bool bGPUSkinning)
	{
		FSceneRenderOptions Effective = Options;
		if (!Options.bUnlit && !Options.bOverrideMaterialLighting && Material.bOverrideShadingModel)
			Effective.SetLegacyLightingModel(Material.ShadingModel);
		return FRenderResourceManager::GetForwardShaderProgram(Effective.ReflectionModel, Effective.ShadingModel, bGPUSkinning, Effective.bUnlit);
	}

	FVertexBuffer* ResolveBatchVertexBuffer(const FVertexFactory& VF, const FMeshElementCollector& Collector)
	{
		return VF.bUsesDynamicVertexBuffer ? Collector.GetDynamicVertexBuffer() : VF.VertexBuffer;
	}

	FIndexBuffer* ResolveElementIndexBuffer(const FMeshBatchElement& Element, const FVertexFactory& VF, const FMeshElementCollector& Collector)
	{
		return VF.bUsesDynamicIndexBuffer ? Collector.GetDynamicIndexBuffer() : Element.IndexBuffer;
	}

	void AppendMaterialTextures(const UMaterial& Material, bool bUsesUberShader, TArray<FMeshDrawShaderResource>& OutResources)
	{
		OutResources.Reserve(OutResources.Num() + Material.Textures.Num());
		for (int32 Index = 0; Index < Material.Textures.Num(); ++Index)
		{
			// Forward 조명은 t8 이상을 전역 리소스에 사용한다.
			if (bUsesUberShader && Index >= 8)
				break;
			UTexture2D* Texture = Material.Textures[Index];
			EShaderBindFlagBits Stages = EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 1)
				Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			if (bUsesUberShader && Index == 0 && (!Texture || !Texture->GetResource() || !Texture->GetResource()->GetSRV()))
				Texture = nullptr;
			if (bUsesUberShader && Index == 1 && GetNormalEncoding(Texture) <= 0.5f)
				Texture = nullptr;
			OutResources.Add({ static_cast<uint32>(Index), Stages, Texture, nullptr });
		}
	}

	FMeshDrawConstantBuffer MakeForwardObjectCB(const FMatrix& World, const FSceneView& View, bool bReceiveShadow, bool bReceiveDecal)
	{
		FMeshDrawConstantBuffer CB{};
		CB.Slot = 0;
		CB.Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
		FForwardObjectConstants Constants{};
		Constants.World = World;
		Constants.View = View.ViewMatrix;
		Constants.Projection = View.ProjectionMatrix;
		Constants.NormalMatrix = World.Inverse().GetTransposed();
		Constants.ShadowFlags.X = static_cast<float>(bReceiveShadow);
		Constants.ShadowFlags.Y = static_cast<float>(bReceiveDecal);
		std::memcpy(CB.Data.data(), &Constants, sizeof(Constants));
		CB.DataSize = sizeof(Constants);
		return CB;
	}

	FMeshDrawConstantBuffer MakeForwardMaterialCB(const UMaterial& Material)
	{
		FMeshDrawConstantBuffer CB{};
		CB.Slot = 2;
		CB.Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
		const FForwardMaterialConstants Params = MakeForwardMaterial(Material);
		std::memcpy(CB.Data.data(), &Params, sizeof(Params));
		CB.DataSize = sizeof(Params);
		return CB;
	}

	void BuildForwardUberCommands(const FMeshBatch& Batch, const FSceneView& View, const FSceneRenderOptions& Options,
		const FMeshElementCollector& Collector, bool bWorldNormal, TArray<FMeshDrawCommand>& OutCommands)
	{
		if (!Batch.Material)
			return;

		FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
		if (!VB)
			return;

		const bool bGPUSkinning = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) &&
			(Batch.VertexFactory.SkinningMatricesBuffer != nullptr);

		FPipelineState* Base = FRenderResourceManager::GetPSO(bWorldNormal
			? EPSOType::StaticMesh_WorldNormal : EPSOType::StaticMesh_Forward);
		FShaderProgram* Shader = bWorldNormal
			? (bGPUSkinning ? FRenderResourceManager::GetWorldNormalShaderProgram(true) : (Base ? Base->Shader : nullptr))
			: GetMaterialForwardShader(*Batch.Material, Options, bGPUSkinning);
		if (!Base || !Shader || !Shader->VertexShader || !Shader->PixelShader)
			return;

		const FMeshDrawConstantBuffer MaterialCB = MakeForwardMaterialCB(*Batch.Material);
		TArray<FMeshDrawShaderResource> BaseResources;
		AppendMaterialTextures(*Batch.Material, true, BaseResources);
		if (bGPUSkinning)
		{
			BaseResources.Add({ 16, EShaderBindFlagBits::Vertex, nullptr, Batch.VertexFactory.SkinningMatricesBuffer });
		}

		const uint64 SortKey = BatchOpaqueSortKey(*Batch.Material, Batch.MeshSortID, Batch.LODIndex);
		for (const FMeshBatchElement& Element : Batch.Elements)
		{
			FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
			if (!IB)
				continue;

			FMeshDrawCommand Cmd{};
			Cmd.Pipeline = *Base;
			Cmd.Pipeline.Shader = Shader;
			Cmd.VertexBuffer = VB;
			Cmd.IndexBuffer = IB;
			Cmd.FirstIndex = Element.FirstIndex;
			Cmd.IndexCount = Element.IndexCount ? Element.IndexCount : IB->GetIndexCount();
			Cmd.BaseVertexLocation = Element.BaseVertexLocation;
			Cmd.InstanceCount = Element.InstanceCount;
			Cmd.Sampler = Batch.Material->SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Cmd.ShaderResources = BaseResources;
			Cmd.ConstantBuffers.Add(MakeForwardObjectCB(ResolveElementWorld(Element), View, Element.bReceiveShadow, Element.bReceiveDecal));
			Cmd.ConstantBuffers.Add(MaterialCB);
			Cmd.bUseObjectSlots = false;
			Cmd.bForceWireframe = !bWorldNormal && View.bIsWireframe;
			Cmd.bSkeletalMesh = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) || (Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
			Cmd.bComputeSkinned = Batch.VertexFactory.bComputeSkinned;
			Cmd.bIsParticle = Batch.bIsParticle;
			Cmd.bOccludedByGpu = Element.bOccludedByGpu;
			Cmd.bReceiveDecal = Element.bReceiveDecal;
			Cmd.ObjectKey = Element.Proxy ? static_cast<const void*>(Element.Proxy) : static_cast<const void*>(&Element);
			Cmd.Proxy = Element.Proxy;
			Cmd.SortKey = SortKey;
			OutCommands.Add(std::move(Cmd));
		}
	}

	EPSOType ResolveParticlePSO(const FMeshBatch& Batch)
	{
		if (Batch.OverridePSOType != EPSOType::Count)
			return Batch.OverridePSOType;

		switch (Batch.VertexFactory.Type)
		{
		case EVertexFactoryType::ParticleSprite:
			return EPSOType::Particle_Instanced;
		case EVertexFactoryType::ParticleGpuSprite:
			return EPSOType::Particle_GpuInstanced;
		case EVertexFactoryType::ParticleMesh:
			if (Batch.Material && Batch.Material->BlendState == EBlendState::Opaque)
				return EPSOType::Particle_MeshOpaque;
			if (Batch.Material && Batch.Material->BlendState == EBlendState::Additive)
				return EPSOType::Particle_MeshAdditive;
			return EPSOType::Particle_MeshTranslucent;
		default:
			return EPSOType::Particle_Instanced;
		}
	}

	void BuildParticleInstancedCommands(const FMeshBatch& Batch, const FMeshElementCollector& Collector,
		uint64 SortKey, TArray<FMeshDrawCommand>& OutCommands)
	{
		if (!Batch.Material)
			return;

		const FStructuredBuffer* InstanceBuffer = Collector.GetParticleInstanceBuffer();
		if (!InstanceBuffer)
			return;

		FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
		if (!VB)
			return;

		const EPSOType PSOType = ResolveParticlePSO(Batch);
		FPipelineState* PSO = FRenderResourceManager::GetPSO(PSOType);
		if (!PSO || !PSO->Shader || !PSO->Shader->VertexShader || !PSO->Shader->PixelShader)
			return;

		UTexture2D* AtlasTex = !Batch.Material->Textures.IsEmpty() ? Batch.Material->Textures[0] : nullptr;

		for (const FMeshBatchElement& Element : Batch.Elements)
		{
			if (Element.InstanceCount == 0)
				continue;
			FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
			if (!IB)
				continue;

			struct FParticleBatchConstants
			{
				uint32 StartInstance = 0;
				uint32 Pad[3] = {};
			};

			FMeshDrawConstantBuffer ViewCB{};
			ViewCB.Slot = 0;
			ViewCB.Stages = EShaderBindFlagBits::Vertex;
			ViewCB.bBindViewMatrix = true;

			FMeshDrawConstantBuffer BatchCB{};
			BatchCB.Slot = 1;
			BatchCB.Stages = EShaderBindFlagBits::Vertex;
			FParticleBatchConstants Constants{ Element.StartInstance, { 0, 0, 0 } };
			std::memcpy(BatchCB.Data.data(), &Constants, sizeof(Constants));
			BatchCB.DataSize = sizeof(Constants);

			FMeshDrawCommand Cmd{};
			Cmd.Pipeline = *PSO;
			Cmd.VertexBuffer = VB;
			Cmd.IndexBuffer = IB;
			Cmd.FirstIndex = Element.FirstIndex;
			Cmd.IndexCount = Element.IndexCount ? Element.IndexCount : IB->GetIndexCount();
			Cmd.BaseVertexLocation = Element.BaseVertexLocation;
			Cmd.InstanceCount = Element.InstanceCount;
			Cmd.Sampler = Batch.Material->SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Pixel;
			Cmd.ShaderResources.Add({ 0, EShaderBindFlagBits::Pixel, AtlasTex, nullptr });
			Cmd.ShaderResources.Add({ 1, EShaderBindFlagBits::Vertex, nullptr, InstanceBuffer });
			Cmd.ConstantBuffers.Add(ViewCB);
			Cmd.ConstantBuffers.Add(BatchCB);
			Cmd.bUseObjectSlots = false;
			Cmd.bIsParticle = true;
			Cmd.bReceiveDecal = false;
			Cmd.Proxy = Element.Proxy;
			Cmd.SortKey = SortKey;
			OutCommands.Add(std::move(Cmd));
		}
	}

	void BuildParticleGpuCommands(const FMeshBatch& Batch, const FMeshElementCollector& Collector,
		uint64 SortKey, TArray<FMeshDrawCommand>& OutCommands)
	{
		if (!Batch.Material)
			return;

		FGpuParticleEmitterResources* Res = Batch.VertexFactory.GpuResources;
		if (!Res || !Res->ParticleBuffer || !Res->ParticleBuffer->GetSRV())
			return;

		FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
		if (!VB)
			return;

		FPipelineState* PSO = FRenderResourceManager::GetPSO(EPSOType::Particle_GpuInstanced);
		if (!PSO || !PSO->Shader || !PSO->Shader->VertexShader || !PSO->Shader->PixelShader)
			return;

		UTexture2D* AtlasTex = !Batch.Material->Textures.IsEmpty() ? Batch.Material->Textures[0] : nullptr;

		for (const FMeshBatchElement& Element : Batch.Elements)
		{
			if (Element.InstanceCount == 0)
				continue;
			FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
			if (!IB)
				continue;

			FMeshDrawConstantBuffer ViewCB{};
			ViewCB.Slot = 0;
			ViewCB.Stages = EShaderBindFlagBits::Vertex;
			ViewCB.bBindViewMatrix = true;

			FMeshDrawConstantBuffer DrawCB{};
			DrawCB.Slot = 1;
			DrawCB.Stages = EShaderBindFlagBits::Vertex;
			DrawCB.DataSize = Batch.VertexFactory.DrawParamsSize;
			if (DrawCB.DataSize > 0)
				std::memcpy(DrawCB.Data.data(), Batch.VertexFactory.DrawParamsData.data(), DrawCB.DataSize);

			FMeshDrawCommand Cmd{};
			Cmd.Pipeline = *PSO;
			Cmd.VertexBuffer = VB;
			Cmd.IndexBuffer = IB;
			Cmd.FirstIndex = Element.FirstIndex;
			Cmd.IndexCount = Element.IndexCount ? Element.IndexCount : IB->GetIndexCount();
			Cmd.BaseVertexLocation = Element.BaseVertexLocation;
			Cmd.InstanceCount = Element.InstanceCount;
			Cmd.Sampler = Batch.Material->SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Pixel;
			Cmd.ShaderResources.Add({ 0, EShaderBindFlagBits::Pixel, AtlasTex, nullptr });
			Cmd.ShaderResources.Add({ 1, EShaderBindFlagBits::Vertex, nullptr, Res->ParticleBuffer.get() });
			Cmd.ConstantBuffers.Add(ViewCB);
			Cmd.ConstantBuffers.Add(DrawCB);
			Cmd.bUseObjectSlots = false;
			Cmd.bIsParticle = true;
			Cmd.bReceiveDecal = false;
			Cmd.Proxy = Element.Proxy;
			Cmd.SortKey = SortKey;
			OutCommands.Add(std::move(Cmd));
		}
	}
}

bool FDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
	const FSceneRenderOptions& Options, const FMeshElementCollector& Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!Batch.IsOpaque() || Batch.bIsParticle)
		return true;

	FPipelineState* DepthPipeline = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_DepthOnly);
	if (!DepthPipeline || !DepthPipeline->Shader || !DepthPipeline->Shader->PixelShader)
		return false;

	TArray<FMeshDrawCommand> TempCommands;
	BuildForwardUberCommands(Batch, View, Options, Collector, false, TempCommands);
	for (FMeshDrawCommand& Cmd : TempCommands)
	{
		if (!Cmd.Pipeline.Shader || !Cmd.Pipeline.Shader->VertexShader)
			return false;
		OutCommands.Add(std::move(Cmd));
	}
	return true;
}

bool FDepthPassMeshProcessor::BuildDepthCommands(const TArray<FMeshDrawCommand>& BaseCommands,
	FPipelineState* DepthPipeline, bool bWriteReceiverStencil,
	TArray<FShaderProgram>& OutPrograms, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!DepthPipeline || !DepthPipeline->Shader || !DepthPipeline->Shader->PixelShader)
		return false;

	OutCommands.Reset();
	OutPrograms.Reset();
	OutPrograms.Reserve(BaseCommands.Num());

	for (const FMeshDrawCommand& Source : BaseCommands)
	{
		if (Source.bIsParticle || Source.IndexCount == 0)
			continue;
		if (!Source.Pipeline.Shader || !Source.Pipeline.Shader->VertexShader)
			return false;

		OutPrograms.Add(FShaderProgram(Source.Pipeline.Shader->VertexShader, DepthPipeline->Shader->PixelShader));

		FMeshDrawCommand Cmd = Source;
		Cmd.Pipeline = *DepthPipeline;
		Cmd.Pipeline.Shader = &OutPrograms[OutPrograms.Num() - 1];
		Cmd.Pipeline.RasterizerState = Source.Pipeline.RasterizerState;
		Cmd.Pipeline.StencilRef = 0;
		if (bWriteReceiverStencil)
			Cmd.Pipeline.DepthStencilState = EDepthStencilState::DecalReceiverWrite;
		Cmd.bOverrideStencilRef = bWriteReceiverStencil;
		Cmd.StencilRef = Source.bReceiveDecal ? StencilBits::ReceiveDecal : 0;
		Cmd.bForceWireframe = false;
		OutCommands.Add(std::move(Cmd));
	}
	return true;
}

bool FBasePassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
	const FSceneRenderOptions& Options, const FMeshElementCollector& Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!Batch.IsOpaque() || !Batch.Material)
		return true;

	if (Batch.VertexFactory.Type == EVertexFactoryType::ParticleMesh)
	{
		// 파티클 메쉬는 일반 UberLit 뒤에 그려 조명 상수 충돌을 방지한다
		BuildParticleInstancedCommands(Batch, Collector, (1ull << 63), OutCommands);
		return true;
	}

	if (Batch.VertexFactory.Type == EVertexFactoryType::Local ||
		Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin)
	{
		BuildForwardUberCommands(Batch, View, Options, Collector, false, OutCommands);
	}
	return true;
}

bool FBasePassMeshProcessor::BuildBasePassCommands(const FSceneView& View, const FSceneRenderData& SceneData,
	const FSceneRenderOptions& Options, TArray<FMeshDrawCommand>& OutCommands) const
{
	OutCommands.Reset();

	TArray<const FStaticDrawGroup*> Groups;
	for (const FSceneRenderData::FGatherChunk& Chunk : SceneData.GatherChunks)
		for (const FStaticDrawGroup& Group : Chunk.Groups)
			if (!Group.Items.empty())
				Groups.Add(&Group);

	std::sort(Groups.begin(), Groups.end(),
		[](const FStaticDrawGroup* A, const FStaticDrawGroup* B) { return GroupSortKey(*A) < GroupSortKey(*B); });

	for (const FStaticDrawGroup* Group : Groups)
	{
		if (!Group->Mesh || !Group->Material)
			continue;
		FVertexBuffer* VB = Group->Mesh->GetVertexBuffer(Group->LODIndex);
		FIndexBuffer* IB = Group->Mesh->GetIndexBuffer(Group->LODIndex);
		if (!VB || !IB)
			return false;

		FPipelineState* Base = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_Forward);
		FShaderProgram* Shader = GetMaterialForwardShader(*Group->Material, Options, false);
		if (!Base || !Shader || !Shader->VertexShader || !Shader->PixelShader)
			return false;

		const FMeshDrawConstantBuffer MaterialCB = MakeForwardMaterialCB(*Group->Material);
		TArray<FMeshDrawShaderResource> Resources;
		AppendMaterialTextures(*Group->Material, true, Resources);
		const uint64 SortKey = GroupSortKey(*Group);

		for (const FStaticDrawItem& Item : Group->Items)
		{
			FMeshDrawCommand Cmd{};
			Cmd.Pipeline = *Base;
			Cmd.Pipeline.Shader = Shader;
			Cmd.VertexBuffer = VB;
			Cmd.IndexBuffer = IB;
			Cmd.FirstIndex = Item.StartIndex;
			Cmd.IndexCount = Item.IndexCount;
			Cmd.Sampler = Group->Material->SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Cmd.ShaderResources = Resources;
			Cmd.ConstantBuffers.Add(MakeForwardObjectCB(Item.Proxy->GetLocalToWorld(), View, Item.Proxy->bReceiveShadow, Item.Proxy->bReceiveDecal));
			Cmd.ConstantBuffers.Add(MaterialCB);
			Cmd.bUseObjectSlots = false;
			Cmd.bForceWireframe = View.bIsWireframe;
			Cmd.bOccludedByGpu = Item.bOccludedByGpu != 0;
			Cmd.bReceiveDecal = Item.Proxy->bReceiveDecal;
			Cmd.ObjectKey = Item.Proxy;
			Cmd.Proxy = Item.Proxy;
			Cmd.SortKey = SortKey;
			OutCommands.Add(std::move(Cmd));
		}
	}

	for (const FMeshBatch& Batch : SceneData.Collector.GetMeshBatches())
	{
		if (!AddMeshBatch(Batch, View, Options, SceneData.Collector, OutCommands))
			return false;
	}

	std::stable_sort(OutCommands.begin(), OutCommands.end(),
		[](const FMeshDrawCommand& A, const FMeshDrawCommand& B) { return A.SortKey < B.SortKey; });
	return true;
}

bool FWorldNormalPassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
	const FSceneRenderOptions& Options, const FMeshElementCollector& Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!Batch.IsOpaque() || Batch.bIsParticle || !Batch.Material)
		return true;
	if (Batch.Material->PSOType != EPSOType::StaticMesh_Opaque)
		return true;

	BuildForwardUberCommands(Batch, View, Options, Collector, true, OutCommands);
	return true;
}

bool FWorldNormalPassMeshProcessor::BuildWorldNormalCommands(const FSceneView& View, const FSceneRenderData& SceneData,
	const FSceneRenderOptions& Options, TArray<FMeshDrawCommand>& OutCommands) const
{
	OutCommands.Reset();

	TArray<const FStaticDrawGroup*> Groups;
	for (const FSceneRenderData::FGatherChunk& Chunk : SceneData.GatherChunks)
		for (const FStaticDrawGroup& Group : Chunk.Groups)
			if (!Group.Items.empty())
				Groups.Add(&Group);

	std::sort(Groups.begin(), Groups.end(),
		[](const FStaticDrawGroup* A, const FStaticDrawGroup* B) { return GroupSortKey(*A) < GroupSortKey(*B); });

	FPipelineState* Base = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_WorldNormal);
	if (!Base || !Base->Shader || !Base->Shader->VertexShader || !Base->Shader->PixelShader)
		return false;

	for (const FStaticDrawGroup* Group : Groups)
	{
		if (!Group->Mesh || !Group->Material)
			continue;
		FVertexBuffer* VB = Group->Mesh->GetVertexBuffer(Group->LODIndex);
		FIndexBuffer* IB = Group->Mesh->GetIndexBuffer(Group->LODIndex);
		if (!VB || !IB)
			return false;

		const FMeshDrawConstantBuffer MaterialCB = MakeForwardMaterialCB(*Group->Material);
		TArray<FMeshDrawShaderResource> Resources;
		AppendMaterialTextures(*Group->Material, true, Resources);

		for (const FStaticDrawItem& Item : Group->Items)
		{
			FMeshDrawCommand Cmd{};
			Cmd.Pipeline = *Base;
			Cmd.VertexBuffer = VB;
			Cmd.IndexBuffer = IB;
			Cmd.FirstIndex = Item.StartIndex;
			Cmd.IndexCount = Item.IndexCount;
			Cmd.Sampler = Group->Material->SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Cmd.ShaderResources = Resources;
			Cmd.ConstantBuffers.Add(MakeForwardObjectCB(Item.Proxy->GetLocalToWorld(), View, Item.Proxy->bReceiveShadow, Item.Proxy->bReceiveDecal));
			Cmd.ConstantBuffers.Add(MaterialCB);
			Cmd.bUseObjectSlots = false;
			Cmd.bReceiveDecal = Item.Proxy->bReceiveDecal;
			Cmd.ObjectKey = Item.Proxy;
			Cmd.Proxy = Item.Proxy;
			OutCommands.Add(std::move(Cmd));
		}
	}

	for (const FMeshBatch& Batch : SceneData.Collector.GetMeshBatches())
	{
		if (!AddMeshBatch(Batch, View, Options, SceneData.Collector, OutCommands))
			return false;
	}
	return true;
}

bool FTranslucencyPassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
	const FSceneRenderOptions& Options, const FMeshElementCollector& Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (Batch.IsOpaque() || !Batch.Material)
		return true;

	const uint64 SortKey = MakeTranslucentSortKey(Batch.CameraDistanceSq);

	if (Batch.VertexFactory.Type == EVertexFactoryType::ParticleSprite ||
		Batch.VertexFactory.Type == EVertexFactoryType::ParticleMesh)
	{
		BuildParticleInstancedCommands(Batch, Collector, SortKey, OutCommands);
		return true;
	}

	if (Batch.VertexFactory.Type == EVertexFactoryType::ParticleGpuSprite)
	{
		BuildParticleGpuCommands(Batch, Collector, SortKey, OutCommands);
		return true;
	}

	FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
	if (!VB)
		return true;

	EPSOType PSOType = Batch.OverridePSOType != EPSOType::Count ? Batch.OverridePSOType : Batch.Material->PSOType;
	if (View.bIsWireframe && PSOType == EPSOType::StaticMesh_Opaque)
		PSOType = EPSOType::StaticMesh_Wireframe;

	FPipelineState* Pipeline = FRenderResourceManager::GetPSO(PSOType);
	if (!Pipeline)
		return false;

	const bool bGPUSkinning = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) &&
		(Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
	FPipelineState EffectivePipeline = *Pipeline;
	if (bGPUSkinning && PSOType == EPSOType::StaticMesh_Translucent)
	{
		EffectivePipeline.Shader = FRenderResourceManager::GetTranslucentShaderProgram(true);
		if (!EffectivePipeline.Shader)
			return false;
	}

	FMeshDrawConstantBuffer ViewCB{};
	ViewCB.Slot = 0;
	ViewCB.Stages = EShaderBindFlagBits::Vertex;
	ViewCB.bBindViewMatrix = true;

	FMeshDrawConstantBuffer MaterialCB{};
	MaterialCB.Slot = 1;
	MaterialCB.Stages = EShaderBindFlagBits::Pixel;
	if (Batch.Material->PSOType == EPSOType::StaticMesh_Opaque ||
		Batch.Material->PSOType == EPSOType::StaticMesh_Translucent ||
		Batch.Material->PSOType == EPSOType::StaticMesh_Wireframe)
	{
		const FStaticMeshMaterialParams Params = MakeStaticMaterial(*Batch.Material);
		std::memcpy(MaterialCB.Data.data(), &Params, sizeof(Params));
		MaterialCB.DataSize = sizeof(Params);
	}
	else if (Batch.MaterialParamData && Batch.MaterialParamDataSize <= MaterialCB.Data.size())
	{
		std::memcpy(MaterialCB.Data.data(), Batch.MaterialParamData, Batch.MaterialParamDataSize);
		MaterialCB.DataSize = Batch.MaterialParamDataSize;
	}

	TArray<FMeshDrawShaderResource> Resources;
	AppendMaterialTextures(*Batch.Material, false, Resources);
	if (bGPUSkinning)
		Resources.Add({ 16, EShaderBindFlagBits::Vertex, nullptr, Batch.VertexFactory.SkinningMatricesBuffer });

	for (const FMeshBatchElement& Element : Batch.Elements)
	{
		FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
		if (!IB)
			continue;

		FMeshDrawConstantBuffer ObjectCB{};
		ObjectCB.Slot = 2;
		ObjectCB.Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
		ObjectCB.ObjectSlot = Element.ObjectSlot;
		const FPerObjectConstants Constants = MakePerObjectConstants(ResolveElementWorld(Element), Element.bReceiveShadow, Element.bReceiveDecal);
		std::memcpy(ObjectCB.Data.data(), &Constants, sizeof(Constants));
		ObjectCB.DataSize = sizeof(Constants);

		FMeshDrawCommand Cmd{};
		Cmd.Pipeline = EffectivePipeline;
		Cmd.VertexBuffer = VB;
		Cmd.IndexBuffer = IB;
		Cmd.FirstIndex = Element.FirstIndex;
		Cmd.IndexCount = Element.IndexCount ? Element.IndexCount : IB->GetIndexCount();
		Cmd.BaseVertexLocation = Element.BaseVertexLocation;
		Cmd.InstanceCount = Element.InstanceCount;
		Cmd.Sampler = Batch.Material->SamplerState;
		Cmd.SamplerStages = EShaderBindFlagBits::Pixel;
		Cmd.ShaderResources = Resources;
		Cmd.ConstantBuffers.Add(ViewCB);
		if (MaterialCB.DataSize > 0)
			Cmd.ConstantBuffers.Add(MaterialCB);
		Cmd.ConstantBuffers.Add(ObjectCB);
		Cmd.bUseObjectSlots = true;
		Cmd.bSkeletalMesh = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) || (Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
		Cmd.bComputeSkinned = Batch.VertexFactory.bComputeSkinned;
		Cmd.bIsParticle = Batch.bIsParticle;
		Cmd.bOccludedByGpu = Element.bOccludedByGpu;
		Cmd.bReceiveDecal = Element.bReceiveDecal;
		Cmd.ObjectKey = Element.Proxy ? static_cast<const void*>(Element.Proxy) : static_cast<const void*>(&Element);
		Cmd.Proxy = Element.Proxy;
		Cmd.SortKey = SortKey;
		OutCommands.Add(std::move(Cmd));
	}
	return true;
}

bool FTranslucencyPassMeshProcessor::BuildTranslucencyCommands(const FSceneView& View, const FSceneRenderData& SceneData,
	const FSceneRenderOptions& Options, TArray<FMeshDrawCommand>& OutCommands) const
{
	OutCommands.Reset();

	for (const FMeshBatch& Batch : SceneData.Collector.GetMeshBatches())
	{
		if (!AddMeshBatch(Batch, View, Options, SceneData.Collector, OutCommands))
			return false;
	}

	std::stable_sort(OutCommands.begin(), OutCommands.end(),
		[](const FMeshDrawCommand& A, const FMeshDrawCommand& B) { return A.SortKey < B.SortKey; });
	return true;
}

bool FShadowDepthMeshProcessor::BuildShadowDepthCommands(const TArray<FPrimitiveSceneProxy*>& ShadowMeshes, EPSOType PSOType,
	TArray<FMeshDrawCommand>& OutCommands, const FVector& PreTranslation) const
{
	OutCommands.Reset();

	const FPipelineState* Pipeline = FRenderResourceManager::GetPSO(PSOType);
	if (!Pipeline || !Pipeline->Shader || !Pipeline->Shader->VertexShader)
		return false;

	for (FPrimitiveSceneProxy* Proxy : ShadowMeshes)
	{
		if (!Proxy || !Proxy->GetRenderState() || Proxy->GetLODCount() == 0 || !Proxy->bCastsShadow)
			continue;

		constexpr uint8 LOD = 0;
		const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);

		FPipelineState EffectivePipeline = *Pipeline;
		FVertexBuffer* VB = Proxy->GetVertexBuffer(LOD);
		FIndexBuffer* IB = Proxy->GetIndexBuffer(LOD);
		FStructuredBuffer* SkinningBuffer = nullptr;
		bool bSkeletal = false;
		bool bComputeSkinned = false;

		if (USkeletalMesh* SkelMesh = Proxy->GetSkeletalMesh())
		{
			bSkeletal = true;
			bComputeSkinned = Proxy->IsComputeSkinned();
			SkinningBuffer = Proxy->GetSkinningMatricesBuffer();
			if (SkinningBuffer)
			{
				VB = SkelMesh->GetVertexBuffer(LOD);
				EffectivePipeline.Shader = FRenderResourceManager::GetGPUSkinningShadowDepthShaderProgram();
			}
		}

		if (!VB || !IB || !EffectivePipeline.Shader)
			continue;

		FMeshDrawConstantBuffer ViewCB{};
		ViewCB.Slot = 0;
		ViewCB.Stages = EShaderBindFlagBits::Vertex;
		ViewCB.bBindViewMatrix = true;

		FMeshDrawConstantBuffer ObjectCB{};
		ObjectCB.Slot = 2;
		ObjectCB.Stages = EShaderBindFlagBits::Vertex;
		FPerObjectConstants Constants = MakePerObjectConstants(Proxy->GetLocalToWorld());
		Constants.World.M[3][0] += PreTranslation.X;
		Constants.World.M[3][1] += PreTranslation.Y;
		Constants.World.M[3][2] += PreTranslation.Z;
		std::memcpy(ObjectCB.Data.data(), &Constants, sizeof(Constants));
		ObjectCB.DataSize = sizeof(Constants);

		for (uint32 Index = 0; Index < CachedLOD.NumSections; ++Index)
		{
			const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + Index);
			if (Section.IndexCount == 0)
				continue;

			FMeshDrawCommand Cmd{};
			Cmd.Pipeline = EffectivePipeline;
			Cmd.VertexBuffer = VB;
			Cmd.IndexBuffer = IB;
			Cmd.FirstIndex = Section.StartIndex;
			Cmd.IndexCount = Section.IndexCount;
			if (SkinningBuffer)
				Cmd.ShaderResources.Add({ 16, EShaderBindFlagBits::Vertex, nullptr, SkinningBuffer });
			Cmd.ConstantBuffers.Add(ViewCB);
			Cmd.ConstantBuffers.Add(ObjectCB);
			Cmd.bUseObjectSlots = false;
			Cmd.bSkeletalMesh = bSkeletal;
			Cmd.bComputeSkinned = bComputeSkinned;
			Cmd.ObjectKey = Proxy;
			Cmd.Proxy = Proxy;
			OutCommands.Add(std::move(Cmd));
		}
	}
	return true;
}
