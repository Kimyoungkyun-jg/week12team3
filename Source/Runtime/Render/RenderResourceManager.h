#pragma once
#include "Core/ResourcePaths.h"

#include "Shader.h"
#include "PipelineState.h"

// 렌더링에 사용할 파이프라인 종류 정의
enum class EPSOType : uint8
{
	StaticMesh_Opaque,       // 기본 불투명
	StaticMesh_Translucent,  // 반투명
	StaticMesh_Wireframe,    
	Particle_AlphaBlend,     
	Particle_Additive,       
	Skybox,                  
	Grid,                    
	Outline_Mask,            
	Outline_Draw,
	StaticMesh_GBuffer,
	DeferredLighting,
	DeferredPointLighting,      // 이전 재질 파일의 PSOType 숫자와 호환을 위해 ID 유지
	ToneMap,
	Reserved_13,                // 삭제된 FireBall 자리. 씬 파일의 PSOType 숫자(Billboard=14 등)가 밀리지 않게 남긴다
	Billboard,
	StaticMesh_Forward,
	StaticMesh_WorldNormal,
	StaticMesh_DepthOnly,
	StaticMesh_LightShadow,
	LightCullingDebug,
	ShadowTileClear,         // 그림자 캐시: 다시 그릴 아틀라스 타일만 깊이 1로 지운다
	ShadowDepth,
	ShadowProjection,        // 그림자 한 장을 화면 마스크(ScreenShadowMask)에 그린다
	ShadowMaskVisualize,     // Shadow Mask 뷰 모드: 마스크를 화면에 띄운다
	StaticMesh_DBufferDecal,
	DBufferDecal_DepthReconstruction,
	Particle_Instanced,
	Particle_GpuInstanced,
	Count
};

class FRenderResourceManager
{
public:
	static FRenderResourceManager& Get()
	{
		static FRenderResourceManager* Instance = new FRenderResourceManager();
		return *Instance;
	}

	static void Init()
	{
		Get().ScanShaders(ResourcePaths::ShaderFolder);
		Get().InitPipelineStates(); // PSO 초기화 추가
	}

	void ScanShaders(const fs::path& ShaderRoot);
	static void Shutdown();
	static FShaderProgram* GetShaderProgram(const FString& InPath);

	static FPipelineState* GetPSO(const EPSOType& InType);
	static FShaderProgram* GetForwardShaderProgram(EReflectionModel Reflection, EShadingModel Shading, bool bGPUSkinning = false, bool bUnlit = false);
	static FShaderProgram* GetWorldNormalShaderProgram(bool bGPUSkinning);
	static FShaderProgram* GetTranslucentShaderProgram(bool bGPUSkinning);
	static FShaderProgram* GetShadowDepthShaderProgram();
	static FShaderProgram* GetGPUSkinningShadowDepthShaderProgram();
	static FComputeShader* GetComputeSkinningShader();
	static FComputeShader* GetGpuParticleInjectShader();
	static FComputeShader* GetGpuParticleSimulateShader();
private:
	TUniquePtr<FComputeShader> ComputeSkinningShader;
	bool bComputeSkinningShaderAttempted = false;
	TUniquePtr<FComputeShader> GpuParticleInjectShader;
	bool bGpuParticleInjectAttempted = false;
	TUniquePtr<FComputeShader> GpuParticleSimulateShader;
	bool bGpuParticleSimulateAttempted = false;
	
	void LoadOrCompileShader(const FString& Path);
	void InitPipelineStates();

	TMap<FString, TUniquePtr<FVertexShader>> VertexShaderMap;
	TMap<FString, TUniquePtr<FPixelShader>>  PixelShaderMap;
	TMap<FString, TUniquePtr<FShaderProgram>> ShaderProgramMap;

	TMap<EPSOType, TUniquePtr<FPipelineState>> PipelineStateMap;
};
