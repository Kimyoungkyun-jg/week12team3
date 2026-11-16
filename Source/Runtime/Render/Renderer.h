#pragma once

#include "RenderPacket.h"
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

	// 패스의 의미를 해석하지 않고 준비된 GPU 드로우 명령만 제출한다.
	void UploadViewMatrix(const FMatrix& ViewProjection);
	FConstantBuffer* GetViewConstantBuffer() const { return ViewCB.get(); }
	void SubmitDrawCommands(std::span<const FMeshDrawCommand> Commands,
		const std::shared_ptr<FGpuTimingHistory>& SkeletalTiming = {},
		ESkinningMode MeasuredMode = ESkinningMode::CPU);

	FGPUOcclusion& GetGPUOcclusion() { return GPUOcclusion; }

	FOcclusionMeasureResult MeasureDrawVisibility(std::span<const FMeshDrawCommand> Commands);

	uint8* BeginObjectConstants(uint32 MaxSlots);
	void EndObjectConstants();

	void InitScenePasses(const FString& SkyPanoramaPath);

	FSkyboxRenderer* GetSkybox() const { return Skybox.get(); }
	FHeightFogRenderer* GetHeightFog() const { return HeightFog.get(); }
	FFXAARenderer* GetFXAA() const { return FXAA.get(); }
	FCameraOverlayRenderer* GetCameraOverlay() const { return CameraOverlay.get(); }
	FTextRenderer* GetSceneText() const { return SceneText.get(); }
	FProjectedDecalRenderer* GetProjectedDecal() const { return ProjectedDecal.get(); }

	FConstantBuffer* GetToneMapConstants() const { return ToneMapConstants.get(); }

private:
	void EnsurePerObjectSlotCapacity(uint32 SlotCount);
	void BindCommand(const FMeshDrawCommand& Command, TArray<FMeshDrawShaderResource>& InOutBoundResources);
	void UnbindUnusedResources(const TArray<FMeshDrawShaderResource>& NextResources,
		TArray<FMeshDrawShaderResource>& InOutBoundResources);

	static constexpr uint32 MaxCommandSlotCBs = 4;
	std::array<TUniquePtr<FConstantBuffer>, MaxCommandSlotCBs> CommandSlotCBs;
	TUniquePtr<FConstantBuffer> ViewCB;
	TUniquePtr<FConstantBuffer> PerObjectSlotCB;
	TUniquePtr<FConstantBuffer> ToneMapConstants;
	uint32 PerObjectSlotCapacity = 0;
	bool bUsePerObjectSlots = false;
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
