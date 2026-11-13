#pragma once

#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Render/Texture2D.h"
#include "Render/Decal/ProjectedDecal.h"
#include "Render/Decal/DecalMeshDrawList.h"

#include <span>

class FRenderer;
struct FMeshDrawBatch;
struct FSceneView;
struct FDecalFrameStats;
class FProjectedDecalRenderer
{
public:
	bool Init(FRenderer* InRenderer);

	bool IsReady() const;

	// 호출 전제:
	// 1. DBuffer RTV가 바인딩되어 있다.
	// 2. Prepass Detpth가 DSV로 바인딩되어 있다.
	//
	// 렌더 타깃 설정과 Clear는 RenderGraphs가 담당한다.
	bool OnRenderMesh(
		std::span<const FMeshDrawBatch> ReceiverBatches,
		std::span<const FProjectedDecalRenderData> Decals,
		std::span<const FProjectedDecalMeshDrawList> DrawLists,
		bool bUnlitPass,
		FDecalFrameStats* Stats = nullptr);

	bool OnRenderDepth(
		const FSceneView& View,
		uint32 Width,
		uint32 Height,
		std::span<const FProjectedDecalRenderData> Decals,
		bool bUnlitPass,
		FDecalFrameStats* Stats = nullptr,
		bool bUseReceiverStencil = false);
private:
	bool InitVolumeResources();
	bool IsVolumeResourcesReady() const;

	// 비소유 포인터.
	FRenderer* Renderer = nullptr;

	// PSO
	FPipelineState MeshPipelineState;
	FPipelineState DepthPipelineState;

	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TUniquePtr<FConstantBuffer> DepthViewConstantBuffer;

	FPipelineState VolumePipelineState;

	TUniquePtr<FVertexBuffer> VolumeVertexBuffer;
	TUniquePtr<FIndexBuffer> VolumeIndexBuffer;
	TUniquePtr<FConstantBuffer> VolumeConstantBuffer;
};