#include "EnginePCH.h"
#include "Render/MeshPassProcessor.h"
#include "Render/SceneRenderData.h"
#include "Render/RenderResourceManager.h"
#include "Render/Mesh.h"
#include "Render/MaterialRenderProxy.h"
#include "Render/NormalMapUtils.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

namespace
{
	inline void HashCombine(uint32& Seed, uint64 Value)
	{
		uint64 Mixed = Value * 0x9E3779B97F4A7C15ull;
		Mixed ^= Mixed >> 30;
		Seed ^= static_cast<uint32>(Mixed) + 0x9E3779B9u + (Seed << 6) + (Seed >> 2);
		Seed ^= static_cast<uint32>(Mixed >> 32) + 0x9E3779B9u + (Seed << 6) + (Seed >> 2);
	}

	inline void HashBytes(uint32& Seed, const void* Data, uint32 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (uint32 i = 0; i < Size; ++i)
		{
			Seed ^= Bytes[i];
			Seed *= 16777619u;
		}
	}

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

	uint32 ResolveElementPrimitiveId(const FMeshBatchElement& Element)
	{
		if (Element.PrimitiveId != InvalidPrimitiveId)
			return Element.PrimitiveId;
		if (Element.Proxy && Element.Proxy->PackedIndex >= 0)
			return static_cast<uint32>(Element.Proxy->PackedIndex);
		return InvalidPrimitiveId;
	}

	FForwardMaterialConstants MakeForwardMaterial(const FMaterialRenderProxy& Material)
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
		Params.UVTransform = FVector4(0.0f, 0.0f, Material.UVTiling.X, Material.UVTiling.Y);
		Params.WorldUVParams = FVector4(Material.WorldUVTileSize.X, Material.WorldUVTileSize.Y,
			Material.bWorldUVLocalSpace ? 1.0f : 0.0f, Material.BlendState == EBlendState::Opaque ? 1.0f : 0.0f);
		Params.UVScrollAndFlags = FVector4(Material.UVScrollSpeed.X, Material.UVScrollSpeed.Y, 0.0f, 0.0f);
		return Params;
	}

	FStaticMeshMaterialParams MakeStaticMaterial(const FMaterialRenderProxy& Material)
	{
		const auto Forward = MakeForwardMaterial(Material);
		FStaticMeshMaterialParams Params{};
		Params.BaseColor = Forward.BaseColor;
		Params.UVOffset = FVector2(0.0f, 0.0f);
		Params.TextureFlags = Material.UVScrollSpeed;
		Params.UVTiling = Material.UVTiling;
		Params.WorldUVTileSize = Material.WorldUVTileSize;
		Params.Emissive = Forward.Emissive;
		Params.SpecularColorShininess = Forward.SpecularColorShininess;
		Params.WorldUVParams = FVector4(Material.bWorldUVLocalSpace ? 1.0f : 0.0f,
			Material.BlendState == EBlendState::Opaque ? 1.0f : 0.0f,
			Material.bOverrideShadingModel ? 1.0f : 0.0f, static_cast<float>(Material.ShadingModel));
		return Params;
	}

	FSceneRenderOptions ResolveMaterialLighting(const FMaterialRenderProxy& Material, const FSceneRenderOptions& Options)
	{
		FSceneRenderOptions Effective = Options;
		if (!Options.bUnlit && !Options.bOverrideMaterialLighting && Material.bOverrideShadingModel)
			Effective.SetLegacyLightingModel(Material.ShadingModel);
		return Effective;
	}

	FVertexBuffer* ResolveBatchVertexBuffer(const FVertexFactory& VF, const FMeshElementCollector* Collector)
	{
		if (VF.bUsesDynamicVertexBuffer)
			return Collector ? Collector->GetDynamicVertexBuffer() : nullptr;
		return VF.VertexBuffer;
	}

	FIndexBuffer* ResolveElementIndexBuffer(const FMeshBatchElement& Element, const FVertexFactory& VF, const FMeshElementCollector* Collector)
	{
		if (VF.bUsesDynamicIndexBuffer)
			return Collector ? Collector->GetDynamicIndexBuffer() : nullptr;
		return Element.IndexBuffer;
	}

	void AppendMaterialTextures(const FMaterialRenderProxy& Material, bool bUsesUberShader, TArray<FMeshDrawShaderResource>& OutResources, bool bUsesGraph = false)
	{
		// 그래프는 t2~t7, 포워드 조명은 t8 이상을 사용한다.
		const int32 TextureCount = std::min(Material.Textures.Num(), bUsesGraph ? 2 : bUsesUberShader ? 8 : Material.Textures.Num());
		OutResources.Reserve(OutResources.Num() + TextureCount);
		for (int32 Index = 0; Index < TextureCount; ++Index)
		{
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

	EMaterialUsage ResolveMaterialGraphUsage(EPSOType PSO, bool bSkeletalMesh)
	{
		switch (PSO)
		{
		case EPSOType::Particle_AlphaBlend:
		case EPSOType::Particle_Additive:
		case EPSOType::Particle_Sprite:
		case EPSOType::Particle_Instanced:
		case EPSOType::Particle_GpuInstanced:
			return EMaterialUsage::ParticleSprite;

		case EPSOType::Particle_MeshOpaque:
		case EPSOType::Particle_MeshTranslucent:
		case EPSOType::Particle_MeshAdditive:
			return EMaterialUsage::ParticleMesh;

		case EPSOType::Particle_BeamTrailTranslucent:
		case EPSOType::Particle_BeamTrailAdditive:
			return EMaterialUsage::ParticleBeamTrail;

		case EPSOType::SkeletalMesh_GBuffer:
			return EMaterialUsage::SkeletalMesh;

		case EPSOType::StaticMesh_GBuffer:
			return EMaterialUsage::StaticMesh;

		case EPSOType::StaticMesh_Forward:
		case EPSOType::StaticMesh_Translucent:
			return bSkeletalMesh ? EMaterialUsage::SkeletalMesh : EMaterialUsage::StaticMesh;

		default:
			return EMaterialUsage::None;
		}
	}

	FShaderProgram* FindGraphShader(const FMaterialRenderProxy& Material, EPSOType PSO,
		EReflectionModel Reflection = EReflectionModel::Lambert, bool bUnlit = false, bool bGPUSkinning = false, bool bSkeletalMesh = false)
	{
		const UMaterial* Base = Material.BaseMaterial;
		if (!Base || !Base->HasGraphOutput())
		{
			return nullptr;
		}

		const EMaterialUsage RequiredUsage = ResolveMaterialGraphUsage(PSO, bSkeletalMesh);
		if (RequiredUsage == EMaterialUsage::None || !Base->HasUsage(RequiredUsage))
		{
			return nullptr;
		}

		FString Error;
		return FRenderResourceManager::GetMaterialGraphShaderProgram(*Base, PSO, Reflection, bUnlit, bGPUSkinning, Error);
	}

	void AppendGraphBindings(const FMaterialRenderProxy& Material, TArray<FMeshDrawShaderResource>& Resources,
		TArray<FMeshDrawConstantBuffer>& ConstantBuffers)
	{
		for (int32 Index = 0; Index < Material.GraphTextures.Num(); ++Index)
		{
			Resources.Add({ 2u + static_cast<uint32>(Index), EShaderBindFlagBits::Pixel,
				Material.GraphTextures[Index], nullptr });
		}

		FMeshDrawConstantBuffer& CB = ConstantBuffers.AddDefaulted_GetRef();
		CB.Slot = 4;
		CB.Stages = EShaderBindFlagBits::Pixel;
		const uint32 BufferSize = sizeof(FVector4) * MaxMaterialGraphParameters;
		static_assert(BufferSize <= sizeof(CB.Data));

		int32 ParameterIndex = 0;

		for (const FMaterialExpressionNode& Node : Material.BaseMaterial->ExpressionNodes)
		{
			if (Node.Type != EMaterialExpressionType::ScalarParameter && Node.Type != EMaterialExpressionType::VectorParameter)
			{
				continue;
			}

			if (ParameterIndex >= MaxMaterialGraphParameters)
				break;

			FVector4 Value = Node.Type == EMaterialExpressionType::ScalarParameter
				? FVector4(Node.ScalarValue, 0.0f, 0.0f, 0.0f)
				: Node.VectorValue;

			const FName Name(Node.ParameterName);

			for (const FMaterialParameterOverride& Override : Material.ParameterOverrides)
			{
				if (Override.ParameterName != Name)
				{
					continue;
				}

				if (Node.Type == EMaterialExpressionType::ScalarParameter && Override.Type == EMaterialParameterType::Scalar)
				{
					Value.X = Override.ScalarValue;
				}
				else if (Node.Type == EMaterialExpressionType::VectorParameter && Override.Type == EMaterialParameterType::Vector)
				{
					Value = Override.VectorValue;
				}

				break;
			}

			std::memcpy(CB.Data.data() + ParameterIndex * sizeof(FVector4), &Value, sizeof(FVector4));
			++ParameterIndex;
		}

		CB.DataSize = BufferSize;
	}

	FMeshDrawConstantBuffer MakeForwardMaterialCB(const FMaterialRenderProxy& Material)
	{
		FMeshDrawConstantBuffer CB{};
		CB.Slot = 2;
		CB.Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
		const FForwardMaterialConstants Params = MakeForwardMaterial(Material);
		static_assert(sizeof(Params) <= sizeof(CB.Data));
		std::memcpy(CB.Data.data(), &Params, sizeof(Params));
		CB.DataSize = sizeof(Params);
		return CB;
	}

	void FinalizeCommandKeys(FMeshDrawCommand& Cmd, uint16 MaterialSortID, uint16 MeshSortID, uint8 LODIndex, uint64 OverrideSortKey = 0)
	{
		Cmd.StateBucketId = ComputeMeshCommandStateBucket(Cmd); // 불투명 패스 정렬 시 같은 GPU 상태를 쓰는 드로우 커맨드들이 배열에서 나란히 붙게 만듭니다
		Cmd.SortKey = OverrideSortKey != 0 ? OverrideSortKey
			: MakeOpaqueSortKey(Cmd.StateBucketId, MaterialSortID, MeshSortID, LODIndex);
	}

	void BuildForwardUberCommands(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
		const FMeshElementCollector* Collector, bool bWorldNormal, TArray<FMeshDrawCommand>& OutCommands) // 정적과 동적 경로가 함께 쓰는 공용 변환기
	{
		if (!Batch.Material)
			return;

		FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
		if (!VB)
			return;

		const bool bGPUSkinning = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) &&
			(Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
		const bool bSkeletalMesh = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) || (Batch.VertexFactory.SkinningMatricesBuffer != nullptr);

		FPipelineState* Base = FRenderResourceManager::GetPSO(bWorldNormal
			? EPSOType::StaticMesh_WorldNormal : EPSOType::StaticMesh_Forward);
		FMaterialRenderProxy Material{};
		Batch.Material->GetRenderProxy(Material);
		const FSceneRenderOptions Effective = ResolveMaterialLighting(Material, Options);
		FShaderProgram* GraphShader = bWorldNormal ? nullptr
												   : FindGraphShader(Material, EPSOType::StaticMesh_Forward, Effective.ReflectionModel, Effective.bUnlit, bGPUSkinning, bSkeletalMesh);
		FShaderProgram* Shader = bWorldNormal
			? FRenderResourceManager::GetWorldNormalShaderProgram(bGPUSkinning)
			: FRenderResourceManager::GetForwardShaderProgram(Effective.ReflectionModel, Effective.ShadingModel, bGPUSkinning, Effective.bUnlit);
		if (GraphShader)
			Shader = GraphShader;
		if (!Base || !Shader || !Shader->VertexShader || !Shader->PixelShader)
			return;

		FShaderProgram* UnlitGraphShader = GraphShader
			? (Effective.bUnlit ? GraphShader : FindGraphShader(Material, EPSOType::StaticMesh_Forward, Effective.ReflectionModel, true, bGPUSkinning, bSkeletalMesh))
			: nullptr;

		const FMeshDrawConstantBuffer MaterialCB = MakeForwardMaterialCB(Material);
		TArray<FMeshDrawShaderResource> BaseResources;
		AppendMaterialTextures(Material, true, BaseResources, GraphShader != nullptr);
		if (bGPUSkinning)
			BaseResources.Add({ 16, EShaderBindFlagBits::Vertex, nullptr, Batch.VertexFactory.SkinningMatricesBuffer });

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
			Cmd.Sampler = Material.SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			Cmd.ShaderResources = BaseResources;
			Cmd.ConstantBuffers.Add(MaterialCB);
			if (GraphShader)
				AppendGraphBindings(Material, Cmd.ShaderResources, Cmd.ConstantBuffers);
			Cmd.UnlitMaterialGraphShader = UnlitGraphShader;
			Cmd.bGPUSkinning = bGPUSkinning;
			Cmd.PrimitiveId = ResolveElementPrimitiveId(Element);
			Cmd.Material = Batch.Material;
			Cmd.bUsesGPUScene = (Element.InstanceCount == 0);
			Cmd.bSkeletalMesh = bSkeletalMesh;
			Cmd.bComputeSkinned = Batch.VertexFactory.bComputeSkinned;
			Cmd.bIsParticle = Batch.bIsParticle;
			Cmd.bReceiveDecal = Element.bReceiveDecal;
			Cmd.ObjectKey = Element.Proxy ? static_cast<const void*>(Element.Proxy) : static_cast<const void*>(&Element);
			Cmd.Proxy = Element.Proxy;
			FinalizeCommandKeys(Cmd, Batch.Material->GetSortID(), Batch.MeshSortID, Batch.LODIndex);
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
			if (Batch.Material && Batch.Material->GetBlendState() == EBlendState::Opaque)
				return EPSOType::Particle_MeshOpaque;
			if (Batch.Material && Batch.Material->GetBlendState() == EBlendState::Additive)
				return EPSOType::Particle_MeshAdditive;
			return EPSOType::Particle_MeshTranslucent;
		case EVertexFactoryType::ParticleBeamTrail:
			if (Batch.Material && Batch.Material->GetBlendState() == EBlendState::Additive)
				return EPSOType::Particle_BeamTrailAdditive;
			return EPSOType::Particle_BeamTrailTranslucent;
		default:
			return EPSOType::Particle_Instanced;
		}
	}

	// 파티클 머티리얼의 텍스처 유무와 기본 색상을 픽셀 셰이더 상수 버퍼로 전달한다
	void AppendParticleMaterialBindings(const FMaterialRenderProxy& Material, FMeshDrawCommand& Cmd)
	{
		UTexture2D* AtlasTex = Material.Textures.IsValidIndex(0) ? Material.Textures[0] : nullptr;
		const bool bHasTexture = AtlasTex && AtlasTex->GetResource() && AtlasTex->GetResource()->GetSRV();

		struct FParticleMaterialConstants
		{
			FVector4 BaseColor;
			float HasTexture = 0.0f;
			float Pad[3] = {};
		};

		FMeshDrawConstantBuffer MaterialCB{};
		MaterialCB.Slot = 2;
		MaterialCB.Stages = EShaderBindFlagBits::Pixel;
		FParticleMaterialConstants Constants{
			Material.BaseColor,
			bHasTexture ? 1.0f : 0.0f,
			{ Material.BlendState == EBlendState::Additive ? 1.0f : 0.0f, 0.0f, 0.0f }
		};
		std::memcpy(MaterialCB.Data.data(), &Constants, sizeof(Constants));
		MaterialCB.DataSize = sizeof(Constants);

		Cmd.ShaderResources.Add({ 0, EShaderBindFlagBits::Pixel, bHasTexture ? AtlasTex : nullptr, nullptr });
		Cmd.ConstantBuffers.Add(MaterialCB);
	}

	void BuildParticleInstancedCommands(const FMeshBatch& Batch, const FMeshElementCollector* Collector,
		uint64 SortKey, TArray<FMeshDrawCommand>& OutCommands)
	{
		if (!Batch.Material || !Collector)
			return;

		const FStructuredBuffer* InstanceBuffer = Collector->GetParticleInstanceBuffer();
		if (!InstanceBuffer)
			return;

		FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
		if (!VB)
			return;

		const EPSOType PSOType = ResolveParticlePSO(Batch);
		FPipelineState* PSO = FRenderResourceManager::GetPSO(PSOType);
		if (!PSO || !PSO->Shader || !PSO->Shader->VertexShader || !PSO->Shader->PixelShader)
			return;

		FMaterialRenderProxy Material{};
		Batch.Material->GetRenderProxy(Material);
		FShaderProgram* GraphShader = FindGraphShader(Material, PSOType);

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
			Cmd.Sampler = Material.SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Pixel;
			AppendParticleMaterialBindings(Material, Cmd);
			if (GraphShader)
			{
				Cmd.Pipeline.Shader = GraphShader;
				AppendGraphBindings(Material, Cmd.ShaderResources, Cmd.ConstantBuffers);
			}
			Cmd.ShaderResources.Add({ 1, EShaderBindFlagBits::Vertex, nullptr, InstanceBuffer });
			Cmd.ConstantBuffers.Add(BatchCB);
			Cmd.Material = Batch.Material;
			Cmd.bUsesGPUScene = false;
			Cmd.bIsParticle = true;
			Cmd.bReceiveDecal = false;
			Cmd.Proxy = Element.Proxy;
			FinalizeCommandKeys(Cmd, Batch.Material->GetSortID(), Batch.MeshSortID, Batch.LODIndex, SortKey);
			OutCommands.Add(std::move(Cmd));
		}
	}

	void BuildParticleBeamTrailCommands(const FMeshBatch& Batch, const FMeshElementCollector* Collector,
		uint64 SortKey, TArray<FMeshDrawCommand>& OutCommands)
	{
		if (!Batch.Material || !Collector)
			return;

		FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
		if (!VB)
			return;

		const EPSOType PSOType = ResolveParticlePSO(Batch);
		FPipelineState* PSO = FRenderResourceManager::GetPSO(PSOType);
		if (!PSO || !PSO->Shader || !PSO->Shader->VertexShader || !PSO->Shader->PixelShader)
			return;

		FMaterialRenderProxy Material{};
		Batch.Material->GetRenderProxy(Material);
		FShaderProgram* GraphShader = FindGraphShader(Material, PSOType);

		for (const FMeshBatchElement& Element : Batch.Elements)
		{
			if (Element.IndexCount == 0)
				continue;
			FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
			if (!IB)
				continue;

			FMeshDrawCommand Cmd{};
			Cmd.Pipeline = *PSO;
			Cmd.VertexBuffer = VB;
			Cmd.IndexBuffer = IB;
			Cmd.FirstIndex = Element.FirstIndex;
			Cmd.IndexCount = Element.IndexCount;
			Cmd.BaseVertexLocation = Element.BaseVertexLocation;
			Cmd.InstanceCount = 0;
			Cmd.Sampler = Material.SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Pixel;
			AppendParticleMaterialBindings(Material, Cmd);
			if (GraphShader)
			{
				Cmd.Pipeline.Shader = GraphShader;
				AppendGraphBindings(Material, Cmd.ShaderResources, Cmd.ConstantBuffers);
			}
			Cmd.Material = Batch.Material;
			Cmd.bUsesGPUScene = false;
			Cmd.bIsParticle = true;
			Cmd.bReceiveDecal = false;
			Cmd.Proxy = Element.Proxy;
			FinalizeCommandKeys(Cmd, Batch.Material->GetSortID(), Batch.MeshSortID, Batch.LODIndex, SortKey);
			OutCommands.Add(std::move(Cmd));
		}
	}

	void BuildParticleGpuCommands(const FMeshBatch& Batch, const FMeshElementCollector* Collector,
		uint64 SortKey, TArray<FMeshDrawCommand>& OutCommands)
	{
		if (!Batch.Material || !Collector)
			return;

		FGpuParticleEmitterResources* Res = Batch.VertexFactory.GpuResources;
		if (!Res || !Res->ParticleBuffer || !Res->ParticleBuffer->GetSRV())
			return;

		FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
		if (!VB)
			return;

		const EPSOType PSOType = EPSOType::Particle_GpuInstanced;
		FPipelineState* PSO = FRenderResourceManager::GetPSO(PSOType);
		if (!PSO || !PSO->Shader || !PSO->Shader->VertexShader || !PSO->Shader->PixelShader)
			return;

		FMaterialRenderProxy Material{};
		Batch.Material->GetRenderProxy(Material);
		FShaderProgram* GraphShader = FindGraphShader(Material, PSOType);

		for (const FMeshBatchElement& Element : Batch.Elements)
		{
			if (Element.InstanceCount == 0)
				continue;
			FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
			if (!IB)
				continue;

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
			Cmd.Sampler = Material.SamplerState;
			Cmd.SamplerStages = EShaderBindFlagBits::Pixel;
			AppendParticleMaterialBindings(Material, Cmd);
			if (GraphShader)
			{
				Cmd.Pipeline.Shader = GraphShader;
				AppendGraphBindings(Material, Cmd.ShaderResources, Cmd.ConstantBuffers);
			}
			Cmd.ShaderResources.Add({ 1, EShaderBindFlagBits::Vertex, nullptr, Res->ParticleBuffer.get() });
			Cmd.ConstantBuffers.Add(DrawCB);
			Cmd.Material = Batch.Material;
			Cmd.bUsesGPUScene = false;
			Cmd.bIsParticle = true;
			Cmd.bReceiveDecal = false;
			Cmd.Proxy = Element.Proxy;
			FinalizeCommandKeys(Cmd, Batch.Material->GetSortID(), Batch.MeshSortID, Batch.LODIndex, SortKey);
			OutCommands.Add(std::move(Cmd));
		}
	}

	bool AreShaderResourcesEqual(const TArray<FMeshDrawShaderResource>& A, const TArray<FMeshDrawShaderResource>& B)
	{
		if (A.Num() != B.Num())
			return false;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].Slot != B[i].Slot || A[i].Stages != B[i].Stages ||
				A[i].Texture != B[i].Texture || A[i].StructuredBuffer != B[i].StructuredBuffer)
				return false;
		}
		return true;
	}

	bool AreConstantBuffersEqual(const TArray<FMeshDrawConstantBuffer>& A, const TArray<FMeshDrawConstantBuffer>& B)
	{
		if (A.Num() != B.Num())
			return false;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].Slot != B[i].Slot || A[i].Stages != B[i].Stages || A[i].DataSize != B[i].DataSize)
				return false;
			if (A[i].DataSize > 0 && std::memcmp(A[i].Data.data(), B[i].Data.data(), A[i].DataSize) != 0)
				return false;
		}
		return true;
	}
}

uint32 ComputeMeshCommandStateBucket(const FMeshDrawCommand& Command) // FMeshDrawCommand 하나가 쓰는 GPU 파이프라인·메시·리소스·상수 버퍼 상태를 uint32 해시값(StateBucketId) 하나로 계산하는 함수
{
	uint32 Seed = 2166136261u;
	HashCombine(Seed, reinterpret_cast<uintptr_t>(Command.Pipeline.Shader));
	HashCombine(Seed, static_cast<uint64>(Command.Pipeline.Topology));
	HashCombine(Seed, static_cast<uint64>(Command.Pipeline.RasterizerState));
	HashCombine(Seed, static_cast<uint64>(Command.Pipeline.BlendState));
	HashCombine(Seed, static_cast<uint64>(Command.Pipeline.DepthStencilState));
	HashCombine(Seed, static_cast<uint64>(Command.Pipeline.StencilRef));
	HashCombine(Seed, reinterpret_cast<uintptr_t>(Command.VertexBuffer));
	HashCombine(Seed, reinterpret_cast<uintptr_t>(Command.IndexBuffer));
	HashCombine(Seed, static_cast<uint64>(Command.FirstIndex));
	HashCombine(Seed, static_cast<uint64>(Command.IndexCount));
	HashCombine(Seed, static_cast<uint64>(Command.BaseVertexLocation));
	HashCombine(Seed, static_cast<uint64>(Command.Sampler));
	HashCombine(Seed, static_cast<uint64>(Command.SamplerStages));
	HashCombine(Seed, static_cast<uint64>(Command.bUsesGPUScene ? 1 : 0));
	HashCombine(Seed, static_cast<uint64>(Command.bSkeletalMesh ? 1 : 0));
	HashCombine(Seed, static_cast<uint64>(Command.bComputeSkinned ? 1 : 0));
	HashCombine(Seed, static_cast<uint64>(Command.bIsParticle ? 1 : 0));

	for (const FMeshDrawShaderResource& Res : Command.ShaderResources)
	{
		HashCombine(Seed, Res.Slot);
		HashCombine(Seed, static_cast<uint64>(Res.Stages));
		HashCombine(Seed, reinterpret_cast<uintptr_t>(Res.Texture));
		HashCombine(Seed, reinterpret_cast<uintptr_t>(Res.StructuredBuffer));
	}
	for (const FMeshDrawConstantBuffer& CB : Command.ConstantBuffers)
	{
		HashCombine(Seed, CB.Slot);
		HashCombine(Seed, static_cast<uint64>(CB.Stages));
		HashCombine(Seed, CB.DataSize);
		if (CB.DataSize > 0)
			HashBytes(Seed, CB.Data.data(), CB.DataSize);
	}
	return Seed;
}

uint64 MakeOpaqueSortKey(uint32 StateBucketId, uint16 MaterialSortID, uint16 MeshSortID, uint8 LODIndex) // 불투명 드로우 커맨드 정렬에 쓰는 다중 기준을 하나의 uint64(64비트 정수) 키로 비트 패킹(Bit Packing)하는 함수
{
	return (static_cast<uint64>(StateBucketId) << 32)			// 1순위: GPU 상태(셰이더·PSO·VB/IB·리소스·재질 CB)가 완전히 같은 커맨드끼리 묶음
		| (static_cast<uint64>(MaterialSortID & 0x3FFFu) << 18) // 2순위: 같은 상태 버킷 내에서 같은 머티리얼끼리 모음 (최대 16,384종 구분)
		| (static_cast<uint64>(MeshSortID & 0xFFFFu) << 2)		// 3순위: 같은 머티리얼 내에서 같은 메시끼리 모음
		| static_cast<uint64>(LODIndex & 0x3u);					// 4순위: 같은 메시 내에서 LOD 0~3 순서로 정렬
}

uint64 MakeTranslucentSortKey(float CameraDistanceSq) // 멀리 있는 것부터 가까운 순서(Back-to-Front)"로 오름차순 커맨드 정렬
{
	const uint64 DistanceBits = static_cast<uint64>(std::bit_cast<uint32>(CameraDistanceSq));
	return (1ull << 63) | (~DistanceBits & 0xFFFFFFFFull);
}

bool AreVisibleCommandsInstanceCompatible(const FVisibleMeshDrawCommand& A, const FVisibleMeshDrawCommand& B)
{
	if (!A.Command || !B.Command)
		return false;
	const FMeshDrawCommand& CmdA = *A.Command;
	const FMeshDrawCommand& CmdB = *B.Command;
	if (!CmdA.bUsesGPUScene || !CmdB.bUsesGPUScene)
		return false;
	if (CmdA.InstanceCount != 0 || CmdB.InstanceCount != 0)
		return false;
	if (A.StateBucketId != B.StateBucketId || A.ShaderOverride != B.ShaderOverride)
		return false;
	if (A.bForceWireframe != B.bForceWireframe || A.bOverrideStencilRef != B.bOverrideStencilRef)
		return false;
	if (A.bOverrideStencilRef && A.StencilRef != B.StencilRef)
		return false;
	if (A.bHasDepthStencilOverride != B.bHasDepthStencilOverride)
		return false;
	if (A.bHasDepthStencilOverride && A.DepthStencilOverride != B.DepthStencilOverride)
		return false;

	return CmdA.Pipeline.Shader == CmdB.Pipeline.Shader &&
		CmdA.Pipeline.Topology == CmdB.Pipeline.Topology &&
		CmdA.Pipeline.RasterizerState == CmdB.Pipeline.RasterizerState &&
		CmdA.Pipeline.BlendState == CmdB.Pipeline.BlendState &&
		CmdA.Pipeline.DepthStencilState == CmdB.Pipeline.DepthStencilState &&
		CmdA.Pipeline.StencilRef == CmdB.Pipeline.StencilRef &&
		CmdA.VertexBuffer == CmdB.VertexBuffer &&
		CmdA.IndexBuffer == CmdB.IndexBuffer &&
		CmdA.FirstIndex == CmdB.FirstIndex &&
		CmdA.IndexCount == CmdB.IndexCount &&
		CmdA.BaseVertexLocation == CmdB.BaseVertexLocation &&
		CmdA.Sampler == CmdB.Sampler &&
		CmdA.SamplerStages == CmdB.SamplerStages &&
		AreShaderResourcesEqual(CmdA.ShaderResources, CmdB.ShaderResources) &&
		AreConstantBuffersEqual(CmdA.ConstantBuffers, CmdB.ConstantBuffers);
}

FShaderProgram* ResolveUnlitShaderOverride(const FMeshDrawCommand& Command, const FSceneRenderOptions& Options)
{
	if (!Options.bUnlit || Command.bIsParticle)
		return nullptr;
	if (Command.UnlitMaterialGraphShader)
		return Command.UnlitMaterialGraphShader;
	return FRenderResourceManager::GetForwardShaderProgram(Options.ReflectionModel, Options.ShadingModel, Command.bGPUSkinning, true);
}

bool FDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
	const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!Batch.IsOpaque() || Batch.bIsParticle)
		return true;

	FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
	if (!VB)
		return true;

	const bool bGPUSkinning = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) &&
		(Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
	FPipelineState* DepthPipeline = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_DepthOnly);
	FShaderProgram* Shader = FRenderResourceManager::GetDepthPrepassShaderProgram(bGPUSkinning);
	if (!DepthPipeline || !Shader || !Shader->VertexShader || !Shader->PixelShader)
		return false;

	const uint16 MaterialSort = Batch.Material ? Batch.Material->GetSortID() : 0;
	for (const FMeshBatchElement& Element : Batch.Elements)
	{
		FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
		if (!IB)
			continue;

		FMeshDrawCommand Cmd{};
		Cmd.Pipeline = *DepthPipeline;
		Cmd.Pipeline.Shader = Shader;
		Cmd.VertexBuffer = VB;
		Cmd.IndexBuffer = IB;
		Cmd.FirstIndex = Element.FirstIndex;
		Cmd.IndexCount = Element.IndexCount ? Element.IndexCount : IB->GetIndexCount();
		Cmd.BaseVertexLocation = Element.BaseVertexLocation;
		Cmd.InstanceCount = Element.InstanceCount;
		if (bGPUSkinning)
			Cmd.ShaderResources.Add({ 16, EShaderBindFlagBits::Vertex, nullptr, Batch.VertexFactory.SkinningMatricesBuffer });
		Cmd.PrimitiveId = ResolveElementPrimitiveId(Element);
		Cmd.Material = Batch.Material;
		Cmd.bUsesGPUScene = (Element.InstanceCount == 0);
		Cmd.bSkeletalMesh = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) || (Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
		Cmd.bComputeSkinned = Batch.VertexFactory.bComputeSkinned;
		Cmd.bReceiveDecal = Element.bReceiveDecal;
		Cmd.ObjectKey = Element.Proxy ? static_cast<const void*>(Element.Proxy) : static_cast<const void*>(&Element);
		Cmd.Proxy = Element.Proxy;
		FinalizeCommandKeys(Cmd, MaterialSort, Batch.MeshSortID, Batch.LODIndex);
		OutCommands.Add(std::move(Cmd));
	}
	return true;
}

bool FBasePassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
	const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!Batch.IsOpaque() || !Batch.Material)
		return true;

	if (Batch.VertexFactory.Type == EVertexFactoryType::ParticleMesh)
	{
		// 파티클 메시는 일반 조명 이후에 그려 상수 충돌을 방지한다
		BuildParticleInstancedCommands(Batch, Collector, (1ull << 63), OutCommands);
		return true;
	}

	if (Batch.VertexFactory.Type == EVertexFactoryType::Local ||
		Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin)
	{
		BuildForwardUberCommands(Batch, Options, Collector, false, OutCommands);
	}
	return true;
}

bool FWorldNormalPassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
	const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!Batch.IsOpaque() || Batch.bIsParticle || !Batch.Material)
		return true;
	if (Batch.Material->GetPSOType() != EPSOType::StaticMesh_Opaque)
		return true;

	BuildForwardUberCommands(Batch, Options, Collector, true, OutCommands);
	return true;
}

bool FTranslucencyPassMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
	const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const
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

	if (Batch.VertexFactory.Type == EVertexFactoryType::ParticleBeamTrail)
	{
		BuildParticleBeamTrailCommands(Batch, Collector, SortKey, OutCommands);
		return true;
	}

	FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
	if (!VB)
		return true;

	const bool bIsStaticMaterialPSO =
		Batch.Material->GetPSOType() == EPSOType::StaticMesh_Opaque ||
		Batch.Material->GetPSOType() == EPSOType::StaticMesh_Translucent ||
		Batch.Material->GetPSOType() == EPSOType::StaticMesh_Wireframe;
	// 빌보드나 파티클 머티리얼을 썸네일 등 정적 메시로 그릴 때는 정적 반투명 셰이더로 폴백한다
	const bool bStaticMeshFallback =
		Batch.OverridePSOType == EPSOType::Count && !Batch.MaterialParamData && !bIsStaticMaterialPSO;
	const EPSOType PSOType = bStaticMeshFallback
		? EPSOType::StaticMesh_Translucent
		: (Batch.OverridePSOType != EPSOType::Count ? Batch.OverridePSOType : Batch.Material->GetPSOType());
	FPipelineState* Pipeline = FRenderResourceManager::GetPSO(PSOType);
	if (!Pipeline)
		return false;

	const bool bGPUSkinning = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) &&
		(Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
	const bool bSkeletalMesh = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) || (Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
	const bool bUsesGPUSceneShader = (PSOType == EPSOType::StaticMesh_Translucent ||
		PSOType == EPSOType::StaticMesh_Opaque || PSOType == EPSOType::StaticMesh_Wireframe);

	FPipelineState EffectivePipeline = *Pipeline;
	if (bGPUSkinning && PSOType == EPSOType::StaticMesh_Translucent)
	{
		EffectivePipeline.Shader = FRenderResourceManager::GetTranslucentShaderProgram(true);
		if (!EffectivePipeline.Shader)
			return false;
	}

	FMaterialRenderProxy Material{};
	Batch.Material->GetRenderProxy(Material);
	FShaderProgram* GraphShader = nullptr;
	switch (PSOType)
	{
	case EPSOType::StaticMesh_Translucent:
	case EPSOType::Particle_AlphaBlend:
	case EPSOType::Particle_Additive:
	case EPSOType::Particle_Sprite:
		GraphShader = FindGraphShader(Material, PSOType, Options.ReflectionModel, Options.bUnlit, bGPUSkinning, bSkeletalMesh);
		if (GraphShader)
			EffectivePipeline.Shader = GraphShader;
		break;
	default:
		break;
	}

	FMeshDrawConstantBuffer MaterialCB{};
	MaterialCB.Slot = 1;
	MaterialCB.Stages = EShaderBindFlagBits::Pixel;
	if (bIsStaticMaterialPSO || bStaticMeshFallback)
	{
		const FStaticMeshMaterialParams Params = MakeStaticMaterial(Material);
		std::memcpy(MaterialCB.Data.data(), &Params, sizeof(Params));
		MaterialCB.DataSize = sizeof(Params);
	}
	else if (Batch.MaterialParamData && Batch.MaterialParamDataSize <= MaterialCB.Data.size())
	{
		std::memcpy(MaterialCB.Data.data(), Batch.MaterialParamData, Batch.MaterialParamDataSize);
		MaterialCB.DataSize = Batch.MaterialParamDataSize;
	}

	TArray<FMeshDrawShaderResource> Resources;
	AppendMaterialTextures(Material, false, Resources, GraphShader != nullptr);
	if (bGPUSkinning)
		Resources.Add({ 16, EShaderBindFlagBits::Vertex, nullptr, Batch.VertexFactory.SkinningMatricesBuffer });

	for (const FMeshBatchElement& Element : Batch.Elements)
	{
		FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
		if (!IB)
			continue;

		FMeshDrawCommand Cmd{};
		Cmd.Pipeline = EffectivePipeline;
		Cmd.VertexBuffer = VB;
		Cmd.IndexBuffer = IB;
		Cmd.FirstIndex = Element.FirstIndex;
		Cmd.IndexCount = Element.IndexCount ? Element.IndexCount : IB->GetIndexCount();
		Cmd.BaseVertexLocation = Element.BaseVertexLocation;
		Cmd.InstanceCount = Element.InstanceCount;
		Cmd.Sampler = Material.SamplerState;
		Cmd.SamplerStages = EShaderBindFlagBits::Pixel;
		Cmd.ShaderResources = Resources;
		if (MaterialCB.DataSize > 0)
			Cmd.ConstantBuffers.Add(MaterialCB);
		if (GraphShader)
			AppendGraphBindings(Material, Cmd.ShaderResources, Cmd.ConstantBuffers);

		if (bUsesGPUSceneShader)
		{
			Cmd.PrimitiveId = ResolveElementPrimitiveId(Element);
			Cmd.bUsesGPUScene = (Element.InstanceCount == 0);
		}
		else
		{
			FMeshDrawConstantBuffer ObjectCB{};
			ObjectCB.Slot = 2;
			ObjectCB.Stages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
			const FPerObjectConstants Constants = MakePerObjectConstants(ResolveElementWorld(Element), Element.bReceiveShadow, Element.bReceiveDecal);
			std::memcpy(ObjectCB.Data.data(), &Constants, sizeof(Constants));
			ObjectCB.DataSize = sizeof(Constants);
			Cmd.ConstantBuffers.Add(ObjectCB);
			Cmd.bUsesGPUScene = false;
		}

		Cmd.Material = Batch.Material;
		Cmd.bSkeletalMesh = bSkeletalMesh;
		Cmd.bComputeSkinned = Batch.VertexFactory.bComputeSkinned;
		Cmd.bIsParticle = Batch.bIsParticle;
		Cmd.bReceiveDecal = Element.bReceiveDecal;
		Cmd.ObjectKey = Element.Proxy ? static_cast<const void*>(Element.Proxy) : static_cast<const void*>(&Element);
		Cmd.Proxy = Element.Proxy;
		FinalizeCommandKeys(Cmd, Batch.Material->GetSortID(), Batch.MeshSortID, Batch.LODIndex, SortKey);
		OutCommands.Add(std::move(Cmd));
	}
	return true;
}

bool FShadowDepthMeshProcessor::AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
	const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const
{
	if (!Batch.bCastShadow || Batch.bIsParticle || Batch.LODIndex != 0)
		return true;

	FVertexBuffer* VB = ResolveBatchVertexBuffer(Batch.VertexFactory, Collector);
	if (!VB)
		return true;

	FPipelineState* Pipeline = FRenderResourceManager::GetPSO(EPSOType::ShadowDepth);
	if (!Pipeline || !Pipeline->Shader || !Pipeline->Shader->VertexShader)
		return false;

	const bool bGPUSkinning = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) &&
		(Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
	FPipelineState EffectivePipeline = *Pipeline;
	if (bGPUSkinning)
	{
		EffectivePipeline.Shader = FRenderResourceManager::GetGPUSkinningShadowDepthShaderProgram();
		if (!EffectivePipeline.Shader)
			return false;
	}

	const uint16 MaterialSort = Batch.Material ? Batch.Material->GetSortID() : 0;
	for (const FMeshBatchElement& Element : Batch.Elements)
	{
		FIndexBuffer* IB = ResolveElementIndexBuffer(Element, Batch.VertexFactory, Collector);
		if (!IB)
			continue;

		FMeshDrawCommand Cmd{};
		Cmd.Pipeline = EffectivePipeline;
		Cmd.VertexBuffer = VB;
		Cmd.IndexBuffer = IB;
		Cmd.FirstIndex = Element.FirstIndex;
		Cmd.IndexCount = Element.IndexCount ? Element.IndexCount : IB->GetIndexCount();
		Cmd.BaseVertexLocation = Element.BaseVertexLocation;
		Cmd.InstanceCount = Element.InstanceCount;
		if (bGPUSkinning)
			Cmd.ShaderResources.Add({ 16, EShaderBindFlagBits::Vertex, nullptr, Batch.VertexFactory.SkinningMatricesBuffer });
		Cmd.PrimitiveId = ResolveElementPrimitiveId(Element);
		Cmd.Material = Batch.Material;
		Cmd.bUsesGPUScene = (Element.InstanceCount == 0);
		Cmd.bSkeletalMesh = (Batch.VertexFactory.Type == EVertexFactoryType::GPUSkin) || (Batch.VertexFactory.SkinningMatricesBuffer != nullptr);
		Cmd.bComputeSkinned = Batch.VertexFactory.bComputeSkinned;
		Cmd.ObjectKey = Element.Proxy ? static_cast<const void*>(Element.Proxy) : static_cast<const void*>(&Element);
		Cmd.Proxy = Element.Proxy;
		FinalizeCommandKeys(Cmd, MaterialSort, Batch.MeshSortID, Batch.LODIndex);
		OutCommands.Add(std::move(Cmd));
	}
	return true;
}
