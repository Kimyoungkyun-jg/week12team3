#pragma once

#include "Render/RenderPacket.h"
#include "Render/PipelineState.h"
#include "Render/ObjectConstants.h"
#include "Render/SceneRenderOptions.h"
#include "Render/Buffer.h"
#include "Render/RenderCommand.h"
#include "Containers/Array.h"
#include "Math/Rect.h"
#include "Render/LightRenderData.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <array>

struct FSceneView;
struct FSceneRenderData;
class FScene;

struct FMeshDrawShaderResource
{
	uint32 Slot = 0;
	EShaderBindFlagBits Stages = EShaderBindFlagBits::Pixel;
	UTexture2D* Texture = nullptr;
	const FStructuredBuffer* StructuredBuffer = nullptr;
};

struct FMeshDrawConstantBuffer
{
	uint32 Slot = 0;
	EShaderBindFlagBits Stages = EShaderBindFlagBits::Vertex;
	std::array<uint8, sizeof(FForwardObjectConstants)> Data{};
	uint32 DataSize = 0;
	uint32 ObjectSlot = InvalidObjectSlot;
	bool bBindViewMatrix = false;
};

// 패스 프로세서가 생성하는 단일 GPU 드로우 명령
struct FMeshDrawCommand
{
	FPipelineState Pipeline;
	FVertexBuffer* VertexBuffer = nullptr;
	FIndexBuffer* IndexBuffer = nullptr;

	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;
	int32 BaseVertexLocation = 0;
	uint32 InstanceCount = 0;
	uint32 StartInstanceLocation = 0;

	ESamplerState Sampler = ESamplerState::LinearClamp;
	EShaderBindFlagBits SamplerStages = EShaderBindFlagBits::Pixel;
	TArray<FMeshDrawShaderResource> ShaderResources;
	TArray<FMeshDrawConstantBuffer> ConstantBuffers;

	bool bUseObjectSlots = true;
	bool bForceWireframe = false;
	bool bSkeletalMesh = false;
	bool bComputeSkinned = false;
	bool bIsParticle = false;
	bool bOccludedByGpu = false;
	bool bReceiveDecal = true;
	bool bOverrideStencilRef = false;
	uint32 StencilRef = 0;
	const void* ObjectKey = nullptr;
	const FPrimitiveSceneProxy* Proxy = nullptr;
	uint64 SortKey = 0;
};

struct FOcclusionMeasureResult
{
	bool bValid = false;
	uint32 TotalObjects = 0;
	uint32 VisibleObjects = 0;
	uint32 TotalDraws = 0;
	uint32 VisibleDraws = 0;
	uint64 TotalTriangles = 0;
	uint64 VisibleTriangles = 0;
	double ElapsedMs = 0.0;
	uint32 GPUOccludedDraws = 0;
	uint32 FalseCulls = 0;
};

// 언리얼식 패스별 메쉬 프로세서 기반 인터페이스
class FMeshPassProcessor
{
public:
	virtual ~FMeshPassProcessor() = default;

	virtual bool AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
		const FSceneRenderOptions& Options, const FMeshElementCollector& Collector,
		TArray<FMeshDrawCommand>& OutCommands) const = 0;
};

class FDepthPassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
		const FSceneRenderOptions& Options, const FMeshElementCollector& Collector,
		TArray<FMeshDrawCommand>& OutCommands) const override;

	bool BuildDepthCommands(const TArray<FMeshDrawCommand>& BaseCommands,
		FPipelineState* DepthPipeline, bool bWriteReceiverStencil,
		TArray<FShaderProgram>& OutPrograms, TArray<FMeshDrawCommand>& OutCommands) const;
};

class FBasePassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
		const FSceneRenderOptions& Options, const FMeshElementCollector& Collector,
		TArray<FMeshDrawCommand>& OutCommands) const override;

	bool BuildBasePassCommands(const FSceneView& View, const FSceneRenderData& SceneData,
		const FSceneRenderOptions& Options, TArray<FMeshDrawCommand>& OutCommands) const;
};

class FWorldNormalPassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
		const FSceneRenderOptions& Options, const FMeshElementCollector& Collector,
		TArray<FMeshDrawCommand>& OutCommands) const override;

	bool BuildWorldNormalCommands(const FSceneView& View, const FSceneRenderData& SceneData,
		const FSceneRenderOptions& Options, TArray<FMeshDrawCommand>& OutCommands) const;
};

class FTranslucencyPassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneView& View,
		const FSceneRenderOptions& Options, const FMeshElementCollector& Collector,
		TArray<FMeshDrawCommand>& OutCommands) const override;

	bool BuildTranslucencyCommands(const FSceneView& View, const FSceneRenderData& SceneData,
		const FSceneRenderOptions& Options, TArray<FMeshDrawCommand>& OutCommands) const;
};

class FShadowDepthMeshProcessor
{
public:
	bool BuildShadowDepthCommands(const TArray<FPrimitiveSceneProxy*>& ShadowMeshes, EPSOType PSOType,
		TArray<FMeshDrawCommand>& OutCommands, const FVector& PreTranslation = FVector::ZeroVector) const;
};
