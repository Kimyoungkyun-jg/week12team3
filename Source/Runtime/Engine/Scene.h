#pragma once

#include <span>
#include <unordered_map>
#include "PrimitiveSceneProxy.h"
#include "LightSceneProxy.h"
#include "Components/BillboardComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Render/Renderer.h"
#include "Render/MeshPassProcessor.h"
#include "Render/ObjectConstants.h"
#include "Render/SceneRenderOptions.h"
#include "Render/SkyLightEnvironment.h"
#include "Render/LightRenderData.h"
#include "Render/LightSceneInfo.h"

class UParticleSystemComponent;

struct FCachedCommandSlotRef
{
	EMeshPass Pass = EMeshPass::BasePass;
	uint32 CommandIndex = 0;
};

struct FCachedMeshDrawEntry
{
	FMeshDrawCommand Command;
	uint32 PrimitiveIndex = InvalidPrimitiveId;
	uint32 OwnerEntryIndex = 0;
	uint8 LODIndex = 0;
};

class FScene
{
public:
	void AddPrimitive(UPrimitiveComponent* Component);
	void RemovePrimitive(UPrimitiveComponent* Component);
	// 모든 프록시를 한 번에 지운다. 액터를 통째로 지우기 전에 불러야 댕글링 포인터가 남지 않는다.
	void RemoveAllPrimitives();
	void UpdateParticleLODs(const FVector& CameraPosition);

	void AddLight(ULightComponent* Component);
	void RemoveLight(ULightComponent* Component);

	void RemoveAllLights();

	const class FAmbientLightSceneProxy* GetActiveAmbientLight(bool bGameView) const { return nullptr; }
	FAmbientLightRenderData GetSkyAmbientLight(bool bGameView) const;
	void PrepareSkyLight(const FSkyLightRenderData& SkyLight);
	const FSkyLightEnvironment* FindSkyLightEnvironment(const FString& SourcePath) const;
	FSkyLightEnvironment* FindPendingSkyLightEnvironment(const FString& SourcePath);

	const TArray<FDirectionalLightSceneProxy*>& GetDirectionalLights() const { return DirectionalLightProxies; }
	// Point Spot Rect 를 한 배열로 다룬다.
	const TArray<FLocalLightSceneProxy*>& GetLocalLights() const { return LocalLightProxies; }

	void UpdateAllTransforms();
	void FlushStaticCaches(const FSceneRenderOptions& Options);
	const FStructuredBuffer* SyncGPUScene(std::span<const FPrimitiveSceneData> DynamicPrimitives = {});
	const TArray<FCachedMeshDrawEntry>& GetCachedDrawList(EMeshPass Pass) const
	{
		return CachedDrawLists[static_cast<size_t>(Pass)];
	}
	uint32 GetNumPrimitives() const { return static_cast<uint32>(PrimitiveSceneProxies.Num()); }

	void BuildBVH();

	void MarkDirty(FPrimitiveSceneProxy* Proxy);
	void MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy);
	void MarkStaticCacheDirty(FPrimitiveSceneProxy* Proxy);
	void MarkGPUSceneDirty(uint32 PrimitiveIndex);
	UPrimitiveComponent* FindPrimitiveComponent(const FPrimitiveSceneProxy* Proxy) const;

	// 그림자 캐시 무효화 기록. 뷰마다 처리한 Version 이 다르므로 지우지 않고 쌓는다.
	FShadowInvalidationList GetShadowInvalidations() const
	{
		return { &ShadowInvalidations, ShadowOldestVersion, ShadowVersion };
	}

	TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
	TArray<FPrimitiveSceneProxy*> DirtyProxies;
	TArray<FPrimitiveSceneProxy*> RenderStateDirtyProxies;
	TArray<FPrimitiveSceneProxy*> StaticCacheDirtyProxies;
	TArray<FAABB> PrimitiveBounds;
	TArray<uint8> PrimitiveFlags;

	// 컬링 결과로 프록시를 바로 내보내 컴포넌트를 역참조하지 않는다.
	TBVH<FPrimitiveSceneProxy*> BVH{
		[](const FPrimitiveSceneProxy* Proxy) -> FBox
		{
			const FAABB& B = Proxy->GetBounds();
			return FBox{ B.Center - B.Extent, B.Center + B.Extent };
		}
	};
	bool bElementListChanged = false;

	// 그림자 렌더러가 쓰는 라이트 목록
	TArray<FLightSceneInfo*>              Lights;
	TArray<FLocalLightSceneProxy*>        LocalLightProxies;
	TArray<FDirectionalLightSceneProxy*>  DirectionalLightProxies;
	TArray<USkyLightComponent*>           SkyLights;

private:
	void ClearProxyStaticCache(uint32 PrimitiveIndex);
	void RebuildProxyStaticCache(FPrimitiveSceneProxy* Proxy, const FSceneRenderOptions& Options);
	void UpdateGPUScenePrimitiveEntry(uint32 PrimitiveIndex, const FPrimitiveSceneProxy* Proxy);

	TArray<UParticleSystemComponent*> ParticleComponents;
	TMap<FLightSceneProxy*, ULightComponent*> LightProxyComponents;
	TMap<FPrimitiveSceneProxy*, UPrimitiveComponent*> PrimitiveProxyComponents;

	std::array<TArray<FCachedMeshDrawEntry>, static_cast<size_t>(EMeshPass::Num)> CachedDrawLists;
	TArray<TArray<FCachedCommandSlotRef>> ProxyCachedCommands;

	TArray<FPrimitiveSceneData> GPUScenePrimitives;
	TArray<uint32> GPUSceneDirtyIndices;
	TArray<uint8> GPUSceneDirtyMask;
	TUniquePtr<FStructuredBuffer> GPUSceneBuffer;
	uint32 GPUSceneBufferCapacity = 0;

	bool bHasCachedOptions = false;
	EReflectionModel CachedReflectionModel = EReflectionModel::BlinnPhong;
	EShadingModel CachedShadingModel = EShadingModel::Phong;
	bool bCachedOverrideMaterialLighting = false;

	void RecordShadowInvalidation(const FAABB& Bounds);
	void InvalidateAllShadows();

	static constexpr int32 MaxShadowInvalidations = 4096;
	TArray<FShadowInvalidation> ShadowInvalidations;
	uint64 ShadowVersion = 0;
	uint64 ShadowOldestVersion = 0;

	std::unordered_map<FString, TUniquePtr<FSkyLightEnvironment>> SkyLightEnvironments;
};
