#pragma once

#include "Render/MeshBatch.h"
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

enum class EMeshPass : uint8
{
	BasePass = 0,
	DepthPass,
	WorldNormal,
	Translucency,
	ShadowDepth,
	Num
};

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
	std::array<uint8, 256> Data{};
	uint32 DataSize = 0;
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

	uint32 PrimitiveId = InvalidPrimitiveId;
	uint32 StateBucketId = 0;
	uint64 SortKey = 0;
	const UMaterial* Material = nullptr;

	bool bUsesGPUScene = false;
	bool bForceWireframe = false;
	bool bSkeletalMesh = false;
	bool bComputeSkinned = false;
	bool bIsParticle = false;
	bool bReceiveDecal = true;
	bool bOverrideStencilRef = false;
	uint32 StencilRef = 0;
	const void* ObjectKey = nullptr;
	const FPrimitiveSceneProxy* Proxy = nullptr;
};

// 가시성 판정을 통과해 패스에 제출할 경량 드로우 참조
struct FVisibleMeshDrawCommand
{
	const FMeshDrawCommand* Command = nullptr;
	FShaderProgram* ShaderOverride = nullptr;
	uint64 SortKey = 0;
	uint32 PrimitiveId = InvalidPrimitiveId;
	uint32 StateBucketId = 0;
	uint32 StencilRef = 0;
	EDepthStencilState DepthStencilOverride = EDepthStencilState::Default;
	bool bForceWireframe = false;
	bool bOccludedByGpu = false;
	bool bHasDepthStencilOverride = false;
	bool bOverrideStencilRef = false;
};

uint32 ComputeMeshCommandStateBucket(const FMeshDrawCommand& Command);
uint64 MakeOpaqueSortKey(uint32 StateBucketId, uint16 MaterialSortID, uint16 MeshSortID, uint8 LODIndex);
uint64 MakeTranslucentSortKey(float CameraDistanceSq);
bool AreVisibleCommandsInstanceCompatible(const FVisibleMeshDrawCommand& A, const FVisibleMeshDrawCommand& B);
FShaderProgram* ResolveUnlitShaderOverride(const FMeshDrawCommand& Command, const FSceneRenderOptions& Options);

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

// 패스별 메시 프로세서 기반 인터페이스
class FMeshPassProcessor
{
public:
	virtual ~FMeshPassProcessor() = default;

	virtual bool AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
		const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const = 0;
};

class FDepthPassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
		const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const override;
};

class FBasePassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
		const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const override;
};

class FWorldNormalPassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
		const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const override;
};

class FTranslucencyPassMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
		const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const override;
};

class FShadowDepthMeshProcessor : public FMeshPassProcessor
{
public:
	bool AddMeshBatch(const FMeshBatch& Batch, const FSceneRenderOptions& Options,
		const FMeshElementCollector* Collector, TArray<FMeshDrawCommand>& OutCommands) const override;
};
