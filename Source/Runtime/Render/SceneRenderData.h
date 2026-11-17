#pragma once

#include "Render/MeshBatch.h"
#include "Render/MeshPassProcessor.h"
#include "Render/ObjectConstants.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/LightSceneProxy.h"
#include "Render/LightRenderData.h"
#include "Render/SceneLightResources.h"
#include "Render/SceneRendering.h"
#include "Render/Decal/DecalStats.h"
#include "Math/Matrix.h"
#include "Math/QuadTree.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

class ULightComponent;

struct FRenderStats
{
	uint32 TotalPrimitives = 0;
	uint32 VisiblePrimitives = 0;
	uint32 DrawCalls = 0;
	uint64 Triangles = 0;
	uint32 LODCounts[4] = {};
	uint64 LODTriangles[4] = {};

	uint32 DirectionalCount = 0;
	uint32 PointCount = 0;
	uint32 SpotCount = 0;
	uint32 RectCount = 0;
	float AtlasCapacity = 0.0f;
	float ShadowTiles = 0.0f;
	uint32 ShadowViewsRedrawn = 0;
	uint32 ShadowViewsCached = 0;
	uint32 ShadowLightsSkippedOutsideView = 0;
	uint32 ShadowLightsSkippedNoReceiver = 0;
	uint32 ShadowLightsSkippedNoCaster = 0;
	uint32 ShadowCastersSkippedSmall = 0;
	uint32 ShadowDepthDraws = 0;

	uint32 CascadeViewsRedrawn = 0;
	uint32 CascadeViewsCached = 0;

	void Reset() { *this = FRenderStats(); }
};

// 뷰 하나의 렌더 상태를 보관한다. SceneRenderer 는 프레임 동안 이를 빌려 쓴다.
struct FSceneRenderData
{
	static constexpr int32 PointLightSlotIndex = 0;
	static constexpr int32 SpotLightSlotIndex = 1;
	static constexpr int32 DirectionalLightSlotIndex = 2;
	static constexpr int32 RectLightSlotIndex = 3;

	static constexpr uint32 ShadowMapSize = 4096;
	static constexpr uint32 MaxCascadeShadowCount = 4;
	static constexpr float MaxCascadeDistance = 1000.f;

	static constexpr float CascadeBlendRatio = 0.1f;

	FMeshElementCollector Collector;
	FRenderStats RenderStats;
	FDecalStats DecalStats;
	std::shared_ptr<FGpuTimingHistory> BaseSkeletalDrawGpu;
	std::shared_ptr<FGpuTimingHistory> ShadowSkeletalDrawGpu;
	ESkinningMode SkeletalDrawGpuMode = ESkinningMode::CPU;
	TArray<FPrimitiveSceneProxy*> VisibleProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;

	// 정적 캐시 필터링용 뷰별 가시성 및 선택 결과
	TArray<uint8> PrimitiveVisibilityMap;
	TArray<uint8> PrimitiveOccludedMap;
	TArray<uint8> PrimitiveSelectedLODs;

	// 동적 요소용 임시 GPUScene 슬롯 및 패스 커맨드
	TArray<FPrimitiveSceneData> DynamicPrimitiveData;
	std::array<TArray<FMeshDrawCommand>, static_cast<size_t>(EMeshPass::Num)> DynamicPassCommands;
	TArray<FVisibleMeshDrawCommand> VisibleBasePassCommands;
	TArray<FVisibleMeshDrawCommand> VisibleDepthPassCommands;
	TArray<FVisibleMeshDrawCommand> VisibleTranslucencyCommands;
	TArray<FVisibleMeshDrawCommand> VisibleShadowCommands;
	TArray<uint8> ShadowVisibilityMap;
	const FStructuredBuffer* GPUSceneBuffer = nullptr;

	FSceneLightResources ViewLightResources;

	FAmbientLightRenderData AmbientLight{};
	FSkyLightRenderData SkyLight{};
	bool bHasSkyLight = false;

	TArray<FLightRenderParameters> VisibleLights;
	TArray<const FLightSceneProxy*> VisibleLightProxies;
	uint32 LightParameterOffsets[4];
	uint32 LightParameterCounts[4];

	TArray<FLightShadowRenderData> LightShadows;
	static constexpr uint32 InvalidShadowView = 0xFFFFFFFFu;
	TArray<uint32> VisibleLightShadowViews;

	float CascadeShadowDistances[MaxCascadeShadowCount]{};
	uint32 CascadeShadowCount = 0;
	const ULightComponent* PreviewLight = nullptr;
	int32 PreviewLightIndex = INDEX_NONE;
	FLightShadowRange PreviewShadowRange{};

	TArray<TUniquePtr<FProjectedShadowInfo>> ProjectedShadows;
	TArray<FVisibleLightInfo> VisibleLightInfos;
	uint32 ShadowAtlasSliceCount = 0;

	bool GetShadowPreview(const ULightComponent* Light, FTexture2DArray*& OutAtlas,
		TArray<FLightShadowRenderData>& OutShadows) const
	{
		OutAtlas = nullptr;
		OutShadows.Reset();
		const uint32 Start = PreviewShadowRange.StartIndex;
		const uint32 Count = PreviewShadowRange.ViewCount;
		if (!Light || Light != PreviewLight || !Count || Start > uint32(LightShadows.Num()) ||
			Count > uint32(LightShadows.Num()) - Start)
			return false;
		FTexture2DArray* Atlas = ViewLightResources.GetShadowDepthAtlas();
		if (!Atlas || !Atlas->GetSRV()) return false;
		for (uint32 i = 0; i < Count; ++i)
		{
			const auto& Shadow = LightShadows[Start + i];
			if (Shadow.AtlasIndex >= Atlas->GetArraySize() || Shadow.AtlasRect.Width <= 0 || Shadow.AtlasRect.Height <= 0)
			{
				OutShadows.Reset();
				return false;
			}
			OutShadows.Add(Shadow);
		}
		OutAtlas = Atlas;
		return true;
	}

	void ResetScene()
	{
		ResetFrame();

		DecalStats.Frame = {};
		DecalStats.bCollecting = false;
		DecalStats.ResetHistory();
		BaseSkeletalDrawGpu = {};
		ShadowSkeletalDrawGpu = {};
	}

	void ResetFrame()
	{
		Collector.Reset();
		RenderStats.Reset();
		VisibleProxies.Reset();
		LODInputs.Reset();
		SelectedLODs.Reset();
		PrimitiveVisibilityMap.Reset();
		PrimitiveOccludedMap.Reset();
		PrimitiveSelectedLODs.Reset();
		DynamicPrimitiveData.Reset();
		for (TArray<FMeshDrawCommand>& List : DynamicPassCommands)
			List.Reset();
		VisibleBasePassCommands.Reset();
		VisibleDepthPassCommands.Reset();
		VisibleTranslucencyCommands.Reset();
		VisibleShadowCommands.Reset();
		ShadowVisibilityMap.Reset();
		GPUSceneBuffer = nullptr;

		AmbientLight = {};
		SkyLight = {};
		bHasSkyLight = false;
		VisibleLights.Reset();
		VisibleLightProxies.Reset();
		VisibleLightShadowViews.Reset();
		LightShadows.Reset();
		PreviewLightIndex = INDEX_NONE;
		PreviewShadowRange = {};
		VisibleLightInfos.Reset();
		ProjectedShadows.Reset();
		ShadowAtlasSliceCount = 0;
		CascadeShadowCount = 0;
		std::fill(std::begin(CascadeShadowDistances), std::end(CascadeShadowDistances), 0.0f);
		std::memset(LightParameterOffsets, 0, sizeof(LightParameterOffsets));
		std::memset(LightParameterCounts, 0, sizeof(LightParameterCounts));
	}

	void Commit(bool& bLightUploadSucceeded)
	{
		if (VisibleLights.Num())
		{
			constexpr uint32 TypeCount = 4;
			constexpr uint32 TypeToSlot[TypeCount] = {
				FSceneRenderData::DirectionalLightSlotIndex,
				FSceneRenderData::PointLightSlotIndex,
				FSceneRenderData::SpotLightSlotIndex,
				FSceneRenderData::RectLightSlotIndex,
			};

			TArray<int32> LightOrder;
			LightOrder.SetNum(VisibleLights.Num());
			for (int32 i = 0; i < VisibleLights.Num(); ++i) LightOrder[i] = i;
			// 같은 종류 순서를 유지해 채널 배정이 흔들리지 않게 한다.
			std::stable_sort(LightOrder.begin(), LightOrder.end(), [&](int32 A, int32 B) {
				const uint32 ASlot = TypeToSlot[VisibleLights[A].Type];
				const uint32 BSlot = TypeToSlot[VisibleLights[B].Type];
				return ASlot < BSlot;
				});
			TArray<FLightRenderParameters> SortedLights;
			TArray<const FLightSceneProxy*> SortedProxies;
			int32 SortedPreviewIndex = INDEX_NONE;
			for (int32 Index : LightOrder)
			{
				if (Index == PreviewLightIndex) SortedPreviewIndex = SortedLights.Num();
				SortedLights.Add(VisibleLights[Index]);
				SortedProxies.Add(Index < VisibleLightProxies.Num() ? VisibleLightProxies[Index] : nullptr);
			}
			VisibleLights = std::move(SortedLights);
			VisibleLightProxies = std::move(SortedProxies);
			PreviewLightIndex = SortedPreviewIndex;

			for (FLightRenderParameters& Light : VisibleLights) Light.ShadowMapChannel = FLightRenderParameters::InvalidShadowMapChannel;

			int32 Offset = 0;
			int32 Count = 1;
			for (int32 i = 1; i < VisibleLights.Num(); i++)
			{
				const FLightRenderParameters& LightParams = VisibleLights[i];
				const FLightRenderParameters& PrevLightParams = VisibleLights[i - 1];

				if (LightParams.Type != PrevLightParams.Type)
				{
					int32 Slot = TypeToSlot[PrevLightParams.Type];
					LightParameterOffsets[Slot] = Offset;
					LightParameterCounts[Slot] = Count;

					Offset += Count;
					Count = 1;
				}
				else
				{
					Count++;
				}
			}

			const FLightRenderParameters& LightParams = VisibleLights.Last();
			int32 Slot = TypeToSlot[LightParams.Type];
			LightParameterOffsets[Slot] = Offset;
			LightParameterCounts[Slot] = Count;
		}

		bLightUploadSucceeded = ViewLightResources.Init();
		if (bLightUploadSucceeded)
			bLightUploadSucceeded &= ViewLightResources.UploadViewLights(VisibleLights);

		RenderStats.DirectionalCount = GetDirectionalLightCount();
		RenderStats.PointCount = GetPointLightCount();
		RenderStats.SpotCount = GetSpotLightCount();
	}

	inline bool HasDirectionalLight() { return LightParameterCounts[DirectionalLightSlotIndex]; }
	inline uint32 GetDirectionalLightOffset() const { return LightParameterOffsets[DirectionalLightSlotIndex]; }
	inline uint32 GetDirectionalLightCount() const { return LightParameterCounts[DirectionalLightSlotIndex]; }

	inline bool HasPointLights() { return LightParameterCounts[PointLightSlotIndex]; }
	inline uint32 GetPointLightOffset() const { return LightParameterOffsets[PointLightSlotIndex]; }
	inline uint32 GetPointLightCount() const { return LightParameterCounts[PointLightSlotIndex]; }

	inline bool HasSpotLights() { return LightParameterCounts[SpotLightSlotIndex]; }
	inline uint32 GetSpotLightOffset() const { return LightParameterOffsets[SpotLightSlotIndex]; }
	inline uint32 GetSpotLightCount() const { return LightParameterCounts[SpotLightSlotIndex]; }

	inline bool HasRectLights() { return LightParameterCounts[RectLightSlotIndex]; }
	inline bool HasLights() { return HasDirectionalLight() || HasPointLights() || HasSpotLights() || HasRectLights(); }
};
