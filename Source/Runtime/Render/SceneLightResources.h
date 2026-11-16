#pragma once

#include "Render/LightRenderData.h"
#include "Render/ForwardLightTiles.h"
#include "Render/Buffer.h"
#include "Render/Texture2D.h"
#include "Containers/Array.h"

// 뷰별 조명 GPU 리소스. 패스나 드로우는 실행하지 않는다.
class FSceneLightResources
{
public:
	bool Init();

	// 광원을 종류별 구간(방향광, 포인트, 스포트, 렉트 순)으로 정렬해 업로드한다.
	// Counts/Offsets 순서는 x: 포인트, y: 스포트, z: 방향광, w: 렉트다.
	bool UploadViewLights(const TArray<FLightRenderParameters>& Lights);

	FStructuredBuffer* GetSceneLights() const { return SceneLightBuffer.get(); }

	FConstantBuffer* GetForwardConstants() const { return ForwardLightCB.get(); }

	FForwardLightTileGrid& GetTiles() { return ForwardTileGrid; }

	void CreateShadowDepthAtlasIfNeed(uint32 SliceCount);
	FTexture2DArray* GetShadowDepthAtlas() const { return ShadowDepthAtlas.get(); }
	FConstantBuffer* GetShadowDepthPassCB() const { return ShadowDepthPassCB.get(); }
	FConstantBuffer* GetShadowProjectionCB() const { return ShadowProjectionCB.get(); }
private:
	TUniquePtr<FTexture2DArray> ShadowDepthAtlas;
	TUniquePtr<FConstantBuffer> ShadowDepthPassCB;   // Init()에서 CreateConstantBuffer(sizeof(FShadowDepthPassConstants))
	TUniquePtr<FConstantBuffer> ShadowProjectionCB;  // ShadowProjection.hlsl b0 (FShadowProjectionConstants)

	TUniquePtr<FStructuredBuffer> SceneLightBuffer;


	TUniquePtr<FConstantBuffer> ForwardLightCB;

	FForwardLightTileGrid ForwardTileGrid;
	bool bInitialized = false;
};
