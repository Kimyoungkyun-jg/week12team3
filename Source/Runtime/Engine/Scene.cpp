#include "EnginePCH.h"
#include "Scene.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Engine/ParticleSceneProxy.h"
#include <algorithm>

namespace
{
	class FSceneStaticPDI : public FStaticPrimitiveDrawInterface
	{
	public:
		TArray<FStaticMeshBatch> Batches;

		void DrawMesh(const FStaticMeshBatch& Batch) override
		{
			Batches.Add(Batch);
		}
	};
}

void FScene::AddPrimitive(UPrimitiveComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = Component->CreateSceneProxy();
	if (!Proxy) return;

	const uint32 Index = static_cast<uint32>(PrimitiveSceneProxies.Num());
	Proxy->Scene = this;
	Proxy->PackedIndex = static_cast<int32>(Index);
	Component->SceneProxy = Proxy;
	PrimitiveProxyComponents.Add(Proxy, Component);
	MarkDirty(Proxy);
	MarkRenderStateDirty(Proxy);

	PrimitiveSceneProxies.Add(Proxy);
	PrimitiveBounds.Add(FAABB{});
	PrimitiveFlags.Add(0);
	ProxyCachedCommands.AddDefaulted_GetRef();
	GPUScenePrimitives.AddDefaulted_GetRef();
	GPUSceneDirtyMask.Add(0);
	UpdateGPUScenePrimitiveEntry(Index, Proxy);
	if (Proxy->HasStaticRelevance())
		MarkStaticCacheDirty(Proxy);

	if (UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Component))
		ParticleComponents.Add(ParticleComp);

	bElementListChanged = true;
}

void FScene::RemovePrimitive(UPrimitiveComponent* Component)
{
	if (!Component || !Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy) return;

	const uint32 Index = static_cast<uint32>(Proxy->PackedIndex);

	// 사라진 Caster 의 그림자가 캐시에 남지 않도록 마지막 위치를 기록한다.
	if (Proxy->bCastsShadow) RecordShadowInvalidation(Proxy->GetBounds());

	ClearProxyStaticCache(Index);

	if (Proxy->bStaticCacheQueued)
	{
		const int32 Found = StaticCacheDirtyProxies.Find(Proxy);
		if (Found != INDEX_NONE) StaticCacheDirtyProxies.RemoveAtSwap(Found);
		Proxy->bStaticCacheQueued = false;
	}

	PrimitiveSceneProxies.RemoveAtSwap(Index);
	PrimitiveBounds.RemoveAtSwap(Index);
	PrimitiveFlags.RemoveAtSwap(Index);
	ProxyCachedCommands.RemoveAtSwap(Index);
	GPUScenePrimitives.RemoveAtSwap(Index);
	GPUSceneDirtyMask.RemoveAtSwap(Index);

	if (Index < static_cast<uint32>(PrimitiveSceneProxies.Num()))
	{
		FPrimitiveSceneProxy* SwappedProxy = PrimitiveSceneProxies[Index];
		SwappedProxy->PackedIndex = static_cast<int32>(Index);
		for (const FCachedCommandSlotRef& Ref : ProxyCachedCommands[Index])
		{
			TArray<FCachedMeshDrawEntry>& List = CachedDrawLists[static_cast<size_t>(Ref.Pass)];
			if (Ref.CommandIndex < static_cast<uint32>(List.Num()))
			{
				List[Ref.CommandIndex].PrimitiveIndex = Index;
				List[Ref.CommandIndex].Command.PrimitiveId = Index;
			}
		}
		MarkGPUSceneDirty(Index);
	}

	if (Proxy->bQueuedForUpdate)
	{
		const int32 Found = DirtyProxies.Find(Proxy);
		if (Found != INDEX_NONE) DirtyProxies.RemoveAtSwap(Found);
	}

	if (Proxy->bRenderStateQueued)
	{
		const int32 Found = RenderStateDirtyProxies.Find(Proxy);
		if (Found != INDEX_NONE) RenderStateDirtyProxies.RemoveAtSwap(Found);
	}

	if (UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Component))
	{
		const int32 ParticleIndex = ParticleComponents.Find(ParticleComp);
		if (ParticleIndex != INDEX_NONE)
			ParticleComponents.RemoveAtSwap(ParticleIndex);
	}
	PrimitiveProxyComponents.Remove(Proxy);
	Component->SceneProxy = nullptr;
	delete Proxy;

	bElementListChanged = true;
}

void FScene::RemoveAllPrimitives()
{
	// 하나씩 RemovePrimitive 하면 대기열 검색이 반복되므로 통째로 비운다.
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (UPrimitiveComponent** Found = PrimitiveProxyComponents.Find(Proxy); Found && *Found)
			(*Found)->SceneProxy = nullptr;
		delete Proxy;
	}
	PrimitiveProxyComponents.Empty();
	PrimitiveSceneProxies.Reset();
	PrimitiveBounds.Reset();
	PrimitiveFlags.Reset();
	ProxyCachedCommands.Reset();
	GPUScenePrimitives.Reset();
	GPUSceneDirtyIndices.Reset();
	GPUSceneDirtyMask.Reset();
	for (TArray<FCachedMeshDrawEntry>& List : CachedDrawLists)
		List.Reset();
	StaticCacheDirtyProxies.Reset();
	ParticleComponents.Reset();
	DirtyProxies.Reset();
	RenderStateDirtyProxies.Reset();
	BVH.Clear();
	bElementListChanged = true;
	InvalidateAllShadows();
}

void FScene::UpdateParticleLODs(const FVector& CameraPosition)
{
	for (UParticleSystemComponent* Component : ParticleComponents)
	{
		if (Component)
		{
			const float Distance = (Component->GetWorldLocation() - CameraPosition).Length();
			Component->UpdateLOD(Distance);
		}
	}
}

void FScene::AddLight(ULightComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FLightSceneProxy* Proxy = Component->CreateSceneProxy();
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
			if (SkyLights.Find(Sky) == INDEX_NONE) SkyLights.Add(Sky);
		return;
	}

	Proxy->Scene = this;
	Component->SceneProxy = Proxy;
	LightProxyComponents.Add(Proxy, Component);

	if (Component->GetLightType() == ELightType::Directional)
	{
		Proxy->PackedIndex = DirectionalLightProxies.Num();
		DirectionalLightProxies.Add(static_cast<FDirectionalLightSceneProxy*>(Proxy));
	}
	else
	{
		Proxy->PackedIndex = LocalLightProxies.Num();
		LocalLightProxies.Add(static_cast<FLocalLightSceneProxy*>(Proxy));
	}

	// PackedIndex 는 스왑 삭제마다 바뀌므로 Id 는 빈 칸을 재사용해 고정한다.
	FLightSceneInfo* Info = new FLightSceneInfo();
	Info->Proxy = Proxy;
	Info->Scene = this;
	Info->Id = Lights.Find(nullptr);
	if (Info->Id == INDEX_NONE)
	{
		Info->Id = Lights.Num();
		Lights.Add(Info);
	}
	else
	{
		Lights[Info->Id] = Info;
	}
	Proxy->LightSceneInfo = Info;
}

void FScene::RemoveLight(ULightComponent* Component)
{
	if (!Component) return;

	FLightSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
		{
			const int32 Found = SkyLights.Find(Sky);
			if (Found != INDEX_NONE) SkyLights.RemoveAtSwap(Found);
		}
		return;
	}

	const int32 Index = Proxy->PackedIndex;
	if (Component->GetLightType() == ELightType::Directional)
	{
		DirectionalLightProxies.RemoveAtSwap(Index);
		if (Index < DirectionalLightProxies.Num())
			DirectionalLightProxies[Index]->PackedIndex = Index;
	}
	else
	{
		LocalLightProxies.RemoveAtSwap(Index);
		if (Index < LocalLightProxies.Num())
			LocalLightProxies[Index]->PackedIndex = Index;
	}

	if (FLightSceneInfo* Info = Proxy->LightSceneInfo)
	{
		if (Info->Id >= 0 && Info->Id < Lights.Num()) Lights[Info->Id] = nullptr;
		delete Info;
	}

	Component->SceneProxy = nullptr;
	LightProxyComponents.Remove(Proxy);
	delete Proxy;
}

void FScene::RemoveAllLights()
{
	for (FLightSceneInfo* Info : Lights) delete Info;
	Lights.Reset();
	for (FLightSceneProxy* Proxy : DirectionalLightProxies)
	{
		if (ULightComponent** Found = LightProxyComponents.Find(Proxy))
		{
			if (*Found) (*Found)->SceneProxy = nullptr;
		}
		delete Proxy;
	}
	for (FLightSceneProxy* Proxy : LocalLightProxies)
	{
		if (ULightComponent** Found = LightProxyComponents.Find(Proxy))
		{
			if (*Found) (*Found)->SceneProxy = nullptr;
		}
		delete Proxy;
	}
	DirectionalLightProxies.Reset();
	LocalLightProxies.Reset();
	LightProxyComponents.Empty();
	SkyLights.Reset();
	SkyLightEnvironments.clear();
}

FAmbientLightRenderData FScene::GetSkyAmbientLight(bool bGameView) const
{
	for (const USkyLightComponent* Sky : SkyLights)
	{
		if (!Sky || !Sky->IsShown(bGameView)) continue;
		const FLinearColor Color(Sky->GetLightColor());
		return { FVector4(Color.R, Color.G, Color.B, Sky->GetIntensity()) };
	}
	return {};
}

void FScene::PrepareSkyLight(const FSkyLightRenderData& SkyLight)
{
	if (SkyLight.SourcePath.empty() || SkyLightEnvironments.contains(SkyLight.SourcePath)) return;
	auto Environment = MakeUnique<FSkyLightEnvironment>();
	Environment->Prepare(SkyLight.SourcePath);
	SkyLightEnvironments.emplace(SkyLight.SourcePath, std::move(Environment));
}

FSkyLightEnvironment* FScene::FindPendingSkyLightEnvironment(const FString& SourcePath)
{
	const auto It = SkyLightEnvironments.find(SourcePath);
	return It != SkyLightEnvironments.end() && It->second->IsPrepared() && !It->second->IsValid()
		? It->second.get() : nullptr;
}

const FSkyLightEnvironment* FScene::FindSkyLightEnvironment(const FString& SourcePath) const
{
	const auto It = SkyLightEnvironments.find(SourcePath);
	return It != SkyLightEnvironments.end() && It->second->IsValid() ? It->second.get() : nullptr;
}

void FScene::ClearProxyStaticCache(uint32 PrimitiveIndex)
{
	if (PrimitiveIndex >= static_cast<uint32>(ProxyCachedCommands.Num()))
		return;

	TArray<FCachedCommandSlotRef>& Refs = ProxyCachedCommands[PrimitiveIndex];
	for (int32 r = Refs.Num() - 1; r >= 0; --r)
	{
		const FCachedCommandSlotRef Ref = Refs[r];
		TArray<FCachedMeshDrawEntry>& List = CachedDrawLists[static_cast<size_t>(Ref.Pass)];
		if (Ref.CommandIndex >= static_cast<uint32>(List.Num()))
			continue;

		const uint32 LastIdx = static_cast<uint32>(List.Num() - 1);
		if (Ref.CommandIndex != LastIdx)
		{
			List[Ref.CommandIndex] = std::move(List[LastIdx]);
			const FCachedMeshDrawEntry& Moved = List[Ref.CommandIndex];
			if (Moved.PrimitiveIndex < static_cast<uint32>(ProxyCachedCommands.Num()) &&
				Moved.OwnerEntryIndex < static_cast<uint32>(ProxyCachedCommands[Moved.PrimitiveIndex].Num()))
			{
				ProxyCachedCommands[Moved.PrimitiveIndex][Moved.OwnerEntryIndex].CommandIndex = Ref.CommandIndex;
			}
		}
		List.RemoveLast();
	}
	Refs.Reset();
}

void FScene::RebuildProxyStaticCache(FPrimitiveSceneProxy* Proxy, const FSceneRenderOptions& Options)
{
	if (!Proxy || Proxy->PackedIndex < 0)
		return;

	const uint32 PrimIdx = static_cast<uint32>(Proxy->PackedIndex);
	ClearProxyStaticCache(PrimIdx);
	if (!Proxy->HasStaticRelevance() || !Proxy->IsVisible())
		return;

	FSceneStaticPDI PDI;
	Proxy->DrawStaticElements(&PDI);
	if (PDI.Batches.IsEmpty())
		return;

	const FBasePassMeshProcessor BaseProcessor;
	const FDepthPassMeshProcessor DepthProcessor;
	const FWorldNormalPassMeshProcessor WorldNormalProcessor;
	const FTranslucencyPassMeshProcessor TranslucencyProcessor;
	const FShadowDepthMeshProcessor ShadowProcessor;

	FSceneRenderOptions BuildOptions = Options;
	BuildOptions.bUnlit = false;

	auto CacheForPass = [&](EMeshPass Pass, const FMeshPassProcessor& Processor, const FStaticMeshBatch& Batch)
	{
		TArray<FMeshDrawCommand> Built;
		if (!Processor.AddMeshBatch(Batch, BuildOptions, nullptr, Built))
			return;
		TArray<FCachedMeshDrawEntry>& List = CachedDrawLists[static_cast<size_t>(Pass)];
		for (FMeshDrawCommand& Cmd : Built)
		{
			Cmd.PrimitiveId = PrimIdx;
			const uint32 OwnerSlot = static_cast<uint32>(ProxyCachedCommands[PrimIdx].Num());
			const uint32 EntryIdx = static_cast<uint32>(List.Num());
			List.Add({ std::move(Cmd), PrimIdx, OwnerSlot, Batch.LODIndex });
			ProxyCachedCommands[PrimIdx].Add({ Pass, EntryIdx });
		}
	};

	for (const FStaticMeshBatch& Batch : PDI.Batches)
	{
		CacheForPass(EMeshPass::BasePass, BaseProcessor, Batch);
		CacheForPass(EMeshPass::DepthPass, DepthProcessor, Batch);
		CacheForPass(EMeshPass::WorldNormal, WorldNormalProcessor, Batch);
		CacheForPass(EMeshPass::Translucency, TranslucencyProcessor, Batch);
		CacheForPass(EMeshPass::ShadowDepth, ShadowProcessor, Batch);
	}
}

void FScene::UpdateGPUScenePrimitiveEntry(uint32 PrimitiveIndex, const FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || PrimitiveIndex >= static_cast<uint32>(GPUScenePrimitives.Num()))
		return;
	GPUScenePrimitives[PrimitiveIndex] = MakePrimitiveSceneData(
		Proxy->GetLocalToWorld(), Proxy->bReceiveShadow, Proxy->bReceiveDecal);
	MarkGPUSceneDirty(PrimitiveIndex);
}

void FScene::FlushStaticCaches(const FSceneRenderOptions& Options)
{
	const bool bOptionsChanged = !bHasCachedOptions ||
		CachedReflectionModel != Options.ReflectionModel ||
		CachedShadingModel != Options.ShadingModel ||
		bCachedOverrideMaterialLighting != Options.bOverrideMaterialLighting;

	if (bOptionsChanged)
	{
		bHasCachedOptions = true;
		CachedReflectionModel = Options.ReflectionModel;
		CachedShadingModel = Options.ShadingModel;
		bCachedOverrideMaterialLighting = Options.bOverrideMaterialLighting;

		for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
		{
			if (Proxy && Proxy->HasStaticRelevance())
				MarkStaticCacheDirty(Proxy);
		}
	}

	if (StaticCacheDirtyProxies.IsEmpty())
		return;

	for (FPrimitiveSceneProxy* Proxy : StaticCacheDirtyProxies)
	{
		if (!Proxy)
			continue;
		Proxy->bStaticCacheQueued = false;
		RebuildProxyStaticCache(Proxy, Options);
	}
	StaticCacheDirtyProxies.Reset();
}

const FStructuredBuffer* FScene::SyncGPUScene(std::span<const FPrimitiveSceneData> DynamicPrimitives)
{
	const uint32 StaticCount = static_cast<uint32>(GPUScenePrimitives.Num());
	const uint32 DynamicCount = static_cast<uint32>(DynamicPrimitives.size());
	const uint32 TotalNeeded = StaticCount + DynamicCount;
	if (TotalNeeded == 0)
		return nullptr;

	constexpr uint32 ElemStride = sizeof(FPrimitiveSceneData);
	if (!GPUSceneBuffer || GPUSceneBufferCapacity < TotalNeeded)
	{
		uint32 NewCapacity = std::max(GPUSceneBufferCapacity * 2u, 256u);
		while (NewCapacity < TotalNeeded)
			NewCapacity *= 2u;
		GPUSceneBuffer = RenderCommand::CreateDefaultStructuredBuffer(NewCapacity, ElemStride);
		GPUSceneBufferCapacity = GPUSceneBuffer ? NewCapacity : 0;
		if (!GPUSceneBuffer)
			return nullptr;
		if (StaticCount > 0)
		{
			RenderCommand::UpdateStructuredBufferRange(GPUSceneBuffer.get(), 0, StaticCount, ElemStride, GPUScenePrimitives.GetData());
		}
		GPUSceneDirtyIndices.Reset();
		std::fill(GPUSceneDirtyMask.begin(), GPUSceneDirtyMask.end(), uint8(0));
	}
	else if (!GPUSceneDirtyIndices.IsEmpty())
	{
		// 대량 변경 시 전체를 한 번에 업로드하고 소량이면 연속 구간만 부분 갱신한다.
		const uint32 DirtyCount = static_cast<uint32>(GPUSceneDirtyIndices.Num());
		if (StaticCount > 0 && DirtyCount * 3 >= StaticCount)
		{
			RenderCommand::UpdateStructuredBufferRange(GPUSceneBuffer.get(), 0, StaticCount, ElemStride, GPUScenePrimitives.GetData());
		}
		else
		{
			std::sort(GPUSceneDirtyIndices.begin(), GPUSceneDirtyIndices.end());
			const uint32 NumDirty = static_cast<uint32>(GPUSceneDirtyIndices.Num());
			uint32 i = 0;
			while (i < NumDirty)
			{
				const uint32 RunStart = GPUSceneDirtyIndices[i];
				if (RunStart >= StaticCount)
					break;
				uint32 RunEnd = RunStart;
				while (i + 1 < NumDirty && GPUSceneDirtyIndices[i + 1] <= RunEnd + 1 && GPUSceneDirtyIndices[i + 1] < StaticCount)
				{
					++i;
					RunEnd = GPUSceneDirtyIndices[i];
				}
				const uint32 RunCount = RunEnd - RunStart + 1;
				RenderCommand::UpdateStructuredBufferRange(GPUSceneBuffer.get(), RunStart, RunCount, ElemStride,
					GPUScenePrimitives.GetData() + RunStart);
				++i;
			}
		}
		GPUSceneDirtyIndices.Reset();
		std::fill(GPUSceneDirtyMask.begin(), GPUSceneDirtyMask.end(), uint8(0));
	}

	if (DynamicCount > 0)
	{
		RenderCommand::UpdateStructuredBufferRange(GPUSceneBuffer.get(), StaticCount, DynamicCount, ElemStride, DynamicPrimitives.data());
	}
	return GPUSceneBuffer.get();
}

void FScene::UpdateAllTransforms()
{
	const bool bAnyRenderStateChanged = RenderStateDirtyProxies.Num() > 0;
	for (FPrimitiveSceneProxy* Proxy : RenderStateDirtyProxies)
	{
		// 그림자를 안 드리우는 동적 프록시는 매 틱 그림자 무효화를 건너뛴다
		if (!Proxy->AsParticleSceneProxy() || Proxy->bCastsShadow)
			RecordShadowInvalidation(Proxy->GetBounds());
		if (UPrimitiveComponent* Component = FindPrimitiveComponent(Proxy))
			Component->PushRenderStateToProxy();
		const uint32 Index = static_cast<uint32>(Proxy->PackedIndex);
		PrimitiveBounds[Index] = Proxy->GetBounds();
		PrimitiveFlags[Index] = Proxy->IsVisible() ? 1 : 0;
		UpdateGPUScenePrimitiveEntry(Index, Proxy);
		if (Proxy->HasStaticRelevance())
			MarkStaticCacheDirty(Proxy);
		Proxy->bRenderStateQueued = false;
	}
	RenderStateDirtyProxies.Reset();

	for (FPrimitiveSceneProxy* Proxy : DirtyProxies)
	{
		const FAABB OldBounds = Proxy->GetBounds();
		if (UPrimitiveComponent* Component = FindPrimitiveComponent(Proxy))
			Component->PushTransformToProxy();
		// 그림자가 사라진 자리와 새로 생긴 자리를 모두 무효화한다.
		if (Proxy->bCastsShadow)
		{
			RecordShadowInvalidation(OldBounds);
			RecordShadowInvalidation(Proxy->GetBounds());
		}
		const uint32 Index = static_cast<uint32>(Proxy->PackedIndex);
		PrimitiveBounds[Index] = Proxy->GetBounds();
		PrimitiveFlags[Index] = Proxy->IsVisible() ? 1 : 0;
		UpdateGPUScenePrimitiveEntry(Index, Proxy);
		Proxy->bQueuedForUpdate = false;
	}

	const bool bAnyMoved = DirtyProxies.Num() > 0 || bAnyRenderStateChanged;
	DirtyProxies.Reset();

	if (bElementListChanged)
	{
		BuildBVH();
		bElementListChanged = false;
	}
	else if (bAnyMoved)
	{
		BVH.Refit();
	}
}

void FScene::BuildBVH()
{
	BVH.Clear();

	TArray<FPrimitiveSceneProxy*> Elements;
	Elements.Reserve(PrimitiveSceneProxies.Num());
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (Proxy && FindPrimitiveComponent(Proxy))
			Elements.Add(Proxy);
	}

	BVH.Build(std::span(Elements.GetData(), Elements.Num()));
}

UPrimitiveComponent* FScene::FindPrimitiveComponent(const FPrimitiveSceneProxy* Proxy) const
{
	if (!Proxy)
		return nullptr;
	UPrimitiveComponent* const* Found = PrimitiveProxyComponents.Find(const_cast<FPrimitiveSceneProxy*>(Proxy));
	return Found ? *Found : nullptr;
}

void FScene::RecordShadowInvalidation(const FAABB& Bounds)
{
	if (ShadowInvalidations.Num() >= MaxShadowInvalidations)
	{
		// 오래된 절반을 버려 처리하지 않은 뷰가 전체 다시 그리기를 하도록 한다.
		const int32 DropCount = ShadowInvalidations.Num() / 2;
		ShadowOldestVersion = ShadowInvalidations[DropCount - 1].Version;
		ShadowInvalidations.RemoveAt(0, DropCount);
	}
	ShadowInvalidations.Add({ ++ShadowVersion, Bounds });
}

void FScene::InvalidateAllShadows()
{
	ShadowInvalidations.Reset();
	ShadowOldestVersion = ++ShadowVersion;
}

void FScene::MarkDirty(FPrimitiveSceneProxy* Proxy)
{
	if (Proxy->bQueuedForUpdate) return;
	Proxy->bQueuedForUpdate = true;
	DirtyProxies.Add(Proxy);
}

void FScene::MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bRenderStateQueued) return;
	Proxy->bRenderStateQueued = true;
	RenderStateDirtyProxies.Add(Proxy);
}

void FScene::MarkStaticCacheDirty(FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bStaticCacheQueued) return;
	Proxy->bStaticCacheQueued = true;
	StaticCacheDirtyProxies.Add(Proxy);
}

void FScene::MarkGPUSceneDirty(uint32 PrimitiveIndex)
{
	if (PrimitiveIndex >= static_cast<uint32>(GPUSceneDirtyMask.Num()))
		return;
	if (GPUSceneDirtyMask[PrimitiveIndex] != 0)
		return;
	GPUSceneDirtyMask[PrimitiveIndex] = 1;
	GPUSceneDirtyIndices.Add(PrimitiveIndex);
}
