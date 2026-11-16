#pragma once

#include "Render/MeshBatch.h"
#include "Render/ObjectConstants.h"
#include "Texture2D.h"
#include "Text/Font.h"
#include "SceneView.h"
#include "Occlusion/GPUOcclusion.h"
#include "Engine/LightSceneProxy.h"
#include "Render/Buffer.h"
#include "MeshPassProcessor.h"
#include <array>
#include <memory>
#include <span>

struct FGpuTimingHistory;

class FSkyboxRenderer;
class FHeightFogRenderer;
class FFXAARenderer;
class FCameraOverlayRenderer;

class FTextRenderer;
class FProjectedDecalRenderer;

class FRenderer
{
public:
	FRenderer();
	~FRenderer();

	bool Init();

	void UploadViewConstants(const FMatrix& ViewProjection, const FMatrix& ViewMatrix,
		const FMatrix& ProjectionMatrix, float TotalTime = 0.0f);

	void SubmitVisibleCommands(std::span<const FVisibleMeshDrawCommand> Commands,
		const FStructuredBuffer* GPUSceneBuffer = nullptr,
		uint32* OutDrawCalls = nullptr,
		const std::shared_ptr<FGpuTimingHistory>& SkeletalTiming = {},
		ESkinningMode MeasuredMode = ESkinningMode::CPU);

	FGPUOcclusion& GetGPUOcclusion() { return GPUOcclusion; }

	FOcclusionMeasureResult MeasureDrawVisibility(std::span<const FVisibleMeshDrawCommand> Commands,
		const FStructuredBuffer* GPUSceneBuffer = nullptr);

	void InitScenePasses(const FString& SkyPanoramaPath);

	FSkyboxRenderer* GetSkybox() const { return Skybox.get(); }
	FHeightFogRenderer* GetHeightFog() const { return HeightFog.get(); }
	FFXAARenderer* GetFXAA() const { return FXAA.get(); }
	FCameraOverlayRenderer* GetCameraOverlay() const { return CameraOverlay.get(); }
	FTextRenderer* GetSceneText() const { return SceneText.get(); }
	FProjectedDecalRenderer* GetProjectedDecal() const { return ProjectedDecal.get(); }

	FConstantBuffer* GetToneMapConstants() const { return ToneMapConstants.get(); }

private:
	struct FDrawRun
	{
		const FVisibleMeshDrawCommand* Visible = nullptr;
		uint32 StartInstance = 0;
		uint32 InstanceCount = 0;
		bool bGPUSceneInstanced = false;
	};

	void EnsureInstanceIdCapacity(uint32 Count);
	void UnbindUnusedResources(const TArray<FMeshDrawShaderResource>& NextResources,
		TArray<FMeshDrawShaderResource>& InOutBoundResources);

	static constexpr uint32 MaxCommandSlotCBs = 5;
	std::array<TUniquePtr<FConstantBuffer>, MaxCommandSlotCBs> CommandSlotCBs;
	TUniquePtr<FConstantBuffer> ViewCB;
	TUniquePtr<FConstantBuffer> DrawBatchCB;
	TUniquePtr<FConstantBuffer> ToneMapConstants;
	TUniquePtr<FStructuredBuffer> InstanceIdBuffer;
	uint32 InstanceIdCapacity = 0;

	TArray<uint32> ScratchInstanceIds;
	TArray<FDrawRun> ScratchDrawRuns;

	TArray<ComPtr<ID3D11Query>> OcclusionQueries;
	ComPtr<ID3D11DepthStencilState> DepthLessEqualReadOnly;
	FGPUOcclusion GPUOcclusion;

	TUniquePtr<FSkyboxRenderer> Skybox;
	TUniquePtr<FHeightFogRenderer> HeightFog;
	TUniquePtr<FFXAARenderer> FXAA;
	TUniquePtr<FCameraOverlayRenderer> CameraOverlay;
	TUniquePtr<FTextRenderer> SceneText;
	TUniquePtr<FProjectedDecalRenderer> ProjectedDecal;
};
