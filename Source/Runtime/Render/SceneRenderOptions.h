#pragma once

#include "Render/RenderEnums.h"

// 뷰 하나의 씬 렌더링 옵션. 에디터와 게임 뷰포트가 같은 설정을 전달한다.
struct FSceneRenderOptions
{
	bool bDrawPrimitives = true;
	EPcfMode PcfMode = EPcfMode::Sample32;   // 그림자 Projection 필터 (ShadowSetup.cpp PcfModeToQuality)
	float ShadowDepthBias = 0.0001f;
	bool bCacheShadows = true; // r.Shadow.CacheWholeSceneShadows: 바뀌지 않은 그림자 타일은 다시 그리지 않는다
	bool bEnableFog = true;
	bool bDrawSkeletalMeshes = true;   // Show 메뉴의 Skeletal Meshes. 끄면 화면과 그림자 수집에서 스켈레탈 프록시를 뺀다
	bool bDrawDecals = true;   // Show 메뉴의 Decals. 끄면 데칼을 모으지 않아 DBuffer가 비어 있는 채로 넘어간다
	bool bEnableFXAA = false;
	ELightCullingMode LightCullingMode = ELightCullingMode::Tiled;
	bool bShowSceneDepth = false;
	bool bShowCascade = false;
	bool bShowShadowMask = false;   // 언리얼 구조 그림자의 ScreenShadowMask를 화면에 띄운다 (Deferred)
	bool bShowWorldNormal = false;
	bool bShowLightCulling = false;
	float SceneDepthRange = 1.0f;
	uint32 DirectionalShadowMapSize = 0;
	uint32 PointShadowMapSize = 0;
	uint32 SpotShadowMapSize = 0;

	bool bGammaCorrection = true;
	float DisplayGamma = 2.2f;
	bool bShowVignette = true;
	bool bShowCameraOverlay = true;

	ERenderingPath RenderingPath = ERenderingPath::Deferred;
	EReflectionModel ReflectionModel = EReflectionModel::BlinnPhong;
	EShadingModel ShadingModel = EShadingModel::Phong;
	bool bUnlit = false;
	bool bOverrideMaterialLighting = false;

	// Old "Phong" was actually Blinn-Phong; keep existing files visually compatible.
	void SetLegacyLightingModel(ELightingModel Model)
	{
		ReflectionModel = Model == ELightingModel::Lambert ? EReflectionModel::Lambert : EReflectionModel::BlinnPhong;
		ShadingModel = Model == ELightingModel::Gouraud ? EShadingModel::Gouraud : EShadingModel::Phong;
		bUnlit = Model == ELightingModel::Unlit;
	}
	EDecalRenderMethod DecalRenderMethod = EDecalRenderMethod::MeshRerendering;

	bool bDecalReceiverStencil = true;
};
