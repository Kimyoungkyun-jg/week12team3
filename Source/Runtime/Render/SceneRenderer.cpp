#include "EnginePCH.h"
#include "Render/SceneRenderer.h"
#include "Render/Renderer.h"
#include "Render/SceneLightResources.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/SceneViewTargets.h"
#include "Render/SkyboxRenderer.h"
#include "Render/RenderResourceManager.h"
#include "Engine/World.h"
#include "Engine/ParticleSceneProxy.h"
#include "Components/ParticleSystemComponent.h"
#include "Tasks/Tasks.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/StatOverlay.h"
#include "Core/EngineTimer.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Render/SkyLightEnvironment.h"
#include "Engine/SceneManagement.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/DecalComponent.h"
#include "Render/PostProcess/HeightFogRenderer.h"
#include "Render/PostProcess/CameraOverlayRenderer.h"
#include "Render/PostProcess/FXAARenderer.h"
#include "Render/PostProcess/ToneMapData.h"
#include "Render/Decal/ProjectedDecalRenderer.h"
#include "Render/Decal/DecalStats.h"
#include "Render/Decal/DecalBounds.h"
#include "Render/SkinningStats.h"
#include "Components/SkeletalMeshComponent.h"
#include "Text/TextRenderer.h"
#include "UObject/UObjectIterator.h"

#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

DECLARE_CYCLE_STAT("Frustum Cull", STAT_FrustumCull);
DECLARE_CYCLE_STAT("Gather Elements", STAT_GatherElements);

namespace
{
	FRenderGraphRasterPassDesc MakeViewportPass(const std::string& Name, FRenderGraphTextureRef Color,
		FRenderGraphTextureRef DepthTarget = {},
		ERenderGraphLoadAction DepthLoad = ERenderGraphLoadAction::Load)
	{
		FRenderGraphRasterPassDesc Pass{};
		Pass.Name = Name;
		Pass.ColorAttachments = { { Color, ERenderGraphLoadAction::Load } };
		if (DepthTarget)
		{
			Pass.DepthAttachment = { DepthTarget, DepthLoad, 1.0f, 0 };
		}
		return Pass;
	}

	bool AddOverlayPass(FRenderGraphBuilder& Graph, const char* Name,
		FRenderGraphTextureRef Color, FRenderGraphTextureRef Depth,
		const std::function<void()>& Draw,
		ERenderGraphLoadAction DepthLoad = ERenderGraphLoadAction::Load)
	{
		return !Draw || Graph.AddRasterPass(MakeViewportPass(Name, Color, Depth, DepthLoad), Draw);
	}
}

// 생성자
FSceneRenderer::FSceneRenderer(UWorld* InWorld, const FSceneView& InView, FSceneRenderData& InRenderData)
	: World(InWorld), View(InView), RenderData(InRenderData)
{
	CachedViewContext = View.ToViewContext();
}

// 구체와 절두체 평면 교차 판별
bool FSceneRenderer::IntersectSphereFrustum(const FVector4& PositionRadius, const FPlane(&Planes)[6]) const
{
	const float Radius = PositionRadius.W;
	for (const FPlane& Plane : Planes)
	{
		const float Distance = Plane.Normal.X * PositionRadius.X +
			Plane.Normal.Y * PositionRadius.Y +
			Plane.Normal.Z * PositionRadius.Z + Plane.Distance;

		if (Distance < -Radius)
		{
			return false;
		}
	}
	return true;
}

// 아래 두 함수는 UEditorEngine의 같은 이름 함수 본문이다. TargetWorld·SceneView 대신 멤버 World·View를 쓴다.
bool FSceneRenderer::RenderHeightFog(FRenderer* Renderer, FTexture2D* DepthTarget, FTexture2D* ColorTarget)
{
	FHeightFogRenderer* FogRenderer = Renderer->GetHeightFog();
	if (!World || !FogRenderer || !DepthTarget || !ColorTarget || !View.bIsPerspective)
		return false;

	for (TObjectIterator<UExponentialHeightFogComponent> Fog; Fog; ++Fog)
	{
		if (!Fog || !Fog->IsRegistered() || !Fog->IsShown(View.bGameView) || !Fog->GetOwner() ||
			Fog->GetOwner()->GetWorld() != World)
			continue;

		FHeightFogSetting Setting;
		Setting.FogDensity = Fog->GetFogDensity();
		Setting.FogHeightFalloff = Fog->GetFogHeightFalloff();
		Setting.FogHeight = Fog->GetFogHeight();
		Setting.FogColor = Fog->GetFogColor();
		Setting.StartDistance = Fog->GetStartDistance();
		Setting.FogMaxOpacity = Fog->GetFogMaxOpacity();
		Setting.FogCutoffDistance = Fog->GetFogCutoffDistance();
		if (FogRenderer->OnRender(View, DepthTarget, ColorTarget, Setting))
			return true;
	}
	return false;
}

void FSceneRenderer::RenderSceneText(FRenderer* Renderer)
{
	FTextRenderer* TextRenderer = Renderer->GetSceneText();
	if (!World || !TextRenderer) return;
	for (TObjectIterator<UTextRenderComponent> Text; Text; ++Text)
	{
		if (!Text || !Text->IsRegistered() || !Text->GetFont() || !Text->IsShown(View.bGameView) ||
			!Text->GetOwner() || Text->GetOwner()->GetWorld() != World)
			continue;
		TextRenderer->OnRender(Text->GetText(), Text->GetWorldMatrix(), Text->GetTextSize(),
			*Text->GetFont(), View.ViewProjectionMatrix);
	}
}

// 가시성 검사 및 렌더 큐 수집
void FSceneRenderer::InitViews(FRenderer* Renderer, const FSceneRenderOptions& Options)
{
	bLightUploadSucceeded = false;
	if (!World)
	{
		RenderData.ResetFrame();
		return;
	}

	// Show 플래그는 옵션으로 들어오므로, 수집과 그림자 단계가 읽는 뷰에 옮겨 둔다
	View.bShowSkeletalMeshes = Options.bDrawSkeletalMeshes;
	CachedViewContext.bShowSkeletalMeshes = Options.bDrawSkeletalMeshes;

	FScene& Scene = World->GetScene();
	Scene.FlushStaticCaches(Options);

	// 월드 프리미티브 가시성 검사 및 동적 요소 수집
	GatherViewVisibilityAndDynamicElements(Scene, RenderData, CachedViewContext, Options, &View.Frustum, Renderer);
	RenderData.AmbientLight = Scene.GetSkyAmbientLight(View.bGameView);

	// 모든 프록시가 같은 FLightRenderParameters를 채우고, 종류는 Type으로만 구분한다.
	auto AddVisibleLight = [this](const FLightSceneProxy* Light)
		{
			FLightRenderParameters Parameters{};
			Light->GetLightShaderParameters(Parameters);
			const int32 Index = RenderData.VisibleLights.Add(Parameters);
			RenderData.VisibleLightProxies.Add(Light);
			if (Light->GetComponentKey() == reinterpret_cast<uintptr_t>(RenderData.PreviewLight)) RenderData.PreviewLightIndex = Index;
		};

	for (const FDirectionalLightSceneProxy* Light : Scene.GetDirectionalLights())
	{
		if (!Light->IsEnabled() || !Light->IsShown(View.bGameView)) continue;
		AddVisibleLight(Light);
	}

	for (const FLocalLightSceneProxy* Light : Scene.GetLocalLights())
	{
		if (!Light->IsEnabled() || !Light->IsShown(View.bGameView)) continue;
		AddVisibleLight(Light);
	}

	RenderData.Commit(bLightUploadSucceeded);

	InitDynamicShadows(Options);
	RenderData.ViewLightResources.CreateShadowDepthAtlasIfNeed(RenderData.ShadowAtlasSliceCount);
	// 그림자 맵이 생긴 광원에 마스크 채널을 주고 광원 데이터를 다시 올린다.
	FinalizeShadowLights(Options);
}

bool FSceneRenderer::BindForwardLighting(FRenderer* Renderer, const FSceneRenderOptions& Options, FTexture2D* ShadowMask)
{
	FSceneLightResources& LightResources = RenderData.ViewLightResources;

	FConstantBuffer* ForwardConstants = LightResources.GetForwardConstants();

	if (!bLightUploadSucceeded || !ForwardConstants)
	{
		return false;
	}

	FPipelineState* BasePSO = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_Forward);
	FShaderProgram* Program = FRenderResourceManager::GetForwardShaderProgram(Options.ReflectionModel, Options.ShadingModel, false, Options.bUnlit);
	if (!BasePSO || !Program)
	{
		return false;
	}

	FForwardLightingConstants Constants{};

	const FSkyLightEnvironment* Sky = RenderData.bHasSkyLight ? World->GetScene().FindSkyLightEnvironment(RenderData.SkyLight.SourcePath) : nullptr;
	Constants.Ambient = Sky ? FAmbientLightRenderData{ FVector4(0.0f) } : RenderData.AmbientLight;
	Constants.SkyColorIntensity = Sky ? RenderData.SkyLight.ColorIntensity : FVector4(0.f);
	Constants.CameraPosition = FVector4(View.ViewLocation.X, View.ViewLocation.Y, View.ViewLocation.Z, 1.0f);

	std::memcpy(Constants.LightCounts, RenderData.LightParameterCounts, sizeof(RenderData.LightParameterCounts));
	std::memcpy(Constants.LightOffsets, RenderData.LightParameterOffsets, sizeof(RenderData.LightParameterOffsets)); // LightOffset은 각 조명별로 시작하는 위치

	RenderCommand::UpdateBufferData(ForwardConstants, &Constants, sizeof(Constants));
	RenderCommand::BindConstantBuffer(1, ForwardConstants, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(11, Sky ? Sky->GetIrradianceCube() : nullptr, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	// 광원 채널(ShadowMapChannel)로 읽는 그림자 마스크. 없으면 셰이더가 그림자 없음(1)으로 본다.
	RenderCommand::BindShaderResource(13, ShadowMask, EShaderBindFlagBits::Pixel);

	FForwardLightTileGrid& Tiles = LightResources.GetTiles();
	Tiles.PreparePixelShader(Renderer, bTilesBuilt);
	RenderCommand::BindConstantBuffer(3, Tiles.GetPixelConstants(), EShaderBindFlagBits::Pixel);
	const std::array<FStructuredBuffer*, 3> TileBuffers = Tiles.GetPixelResources();
	RenderCommand::BindShaderResource(9, TileBuffers[0], EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(10, TileBuffers[1], EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(12, TileBuffers[2], EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(8, RenderData.HasLights() && LightResources.GetSceneLights() && LightResources.GetSceneLights()->GetSRV() ? LightResources.GetSceneLights() : nullptr, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	RenderCommand::BindSamplerState(1, ESamplerState::LinearClamp, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	return true;
}

void FSceneRenderer::UnbindForwardLighting(FRenderer* Renderer)
{
	RenderCommand::UnbindShaderResources(9, 2, EShaderBindFlagBits::Pixel);
	RenderCommand::BindConstantBuffer(3, nullptr, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResources(12, 2, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderData.ViewLightResources.GetTiles().ResetPixelState();
	RenderCommand::UnbindShaderResources(8, 1, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResources(11, 1, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
}

void FSceneRenderer::UnbindForwardDecal(FRenderer* Renderer)
{
	RenderCommand::UnbindShaderResources(14, 3, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
}

void FSceneRenderer::BuildVisiblePassCommands(const FScene& Scene, EMeshPass Pass, const FSceneRenderOptions& Options,
	bool bWireframe, TArray<FVisibleMeshDrawCommand>& OutCommands) const
{
	OutCommands.Reset();
	const TArray<FCachedMeshDrawEntry>& CachedList = Scene.GetCachedDrawList(Pass);
	const TArray<FMeshDrawCommand>& DynamicList = RenderData.DynamicPassCommands[static_cast<size_t>(Pass)];
	OutCommands.Reserve(CachedList.Num() + DynamicList.Num());

	const uint32 NumPrimitives = static_cast<uint32>(RenderData.PrimitiveVisibilityMap.Num());
	const bool bApplyWireframe = bWireframe && (Pass == EMeshPass::BasePass);
	const bool bApplyUnlit = Options.bUnlit && (Pass == EMeshPass::BasePass);

	for (const FCachedMeshDrawEntry& Entry : CachedList) // 캐시된 정적 데이터를 FVisibleMeshDrawCommand로 만듦
	{
		const uint32 PrimIdx = Entry.PrimitiveIndex;
		if (PrimIdx >= NumPrimitives || !RenderData.PrimitiveVisibilityMap[PrimIdx])
			continue;
		if (Pass != EMeshPass::ShadowDepth && RenderData.PrimitiveSelectedLODs[PrimIdx] != Entry.LODIndex)
			continue;

		FVisibleMeshDrawCommand Vis{};
		Vis.Command = &Entry.Command;
		Vis.PrimitiveId = Entry.Command.PrimitiveId;
		Vis.StateBucketId = Entry.Command.StateBucketId;
		Vis.SortKey = Entry.Command.SortKey;
		Vis.bForceWireframe = bApplyWireframe;
		Vis.bOccludedByGpu = (PrimIdx < static_cast<uint32>(RenderData.PrimitiveOccludedMap.Num()))
			&& (RenderData.PrimitiveOccludedMap[PrimIdx] != 0);

		if (bApplyUnlit)
			Vis.ShaderOverride = ResolveUnlitShaderOverride(Entry.Command, Options);
		if (Pass == EMeshPass::Translucency && Entry.Command.Proxy)
		{
			const FAABB& Bounds = Entry.Command.Proxy->GetBounds();
			const FVector Delta = Bounds.Center - View.ViewLocation;
			Vis.SortKey = MakeTranslucentSortKey(Delta.Dot(Delta));
		}
		OutCommands.Add(Vis);
	}

	for (const FMeshDrawCommand& DynCmd : DynamicList) // 이번 프레임에 저장된 동적 데이터를 FVisibleMeshDrawCommand로 만듦
	{
		FVisibleMeshDrawCommand Vis{};
		Vis.Command = &DynCmd;
		Vis.PrimitiveId = DynCmd.PrimitiveId;
		Vis.StateBucketId = DynCmd.StateBucketId;
		Vis.SortKey = DynCmd.SortKey;
		Vis.bForceWireframe = bApplyWireframe && !DynCmd.bIsParticle;
		Vis.bOccludedByGpu = (DynCmd.Proxy && DynCmd.Proxy->PackedIndex >= 0 &&
			static_cast<uint32>(DynCmd.Proxy->PackedIndex) < static_cast<uint32>(RenderData.PrimitiveOccludedMap.Num()))
			&& (RenderData.PrimitiveOccludedMap[static_cast<uint32>(DynCmd.Proxy->PackedIndex)] != 0);
		if (bApplyUnlit)
			Vis.ShaderOverride = ResolveUnlitShaderOverride(DynCmd, Options);
		OutCommands.Add(Vis);
	}

	std::stable_sort(OutCommands.begin(), OutCommands.end(),
		[](const FVisibleMeshDrawCommand& A, const FVisibleMeshDrawCommand& B) { return A.SortKey < B.SortKey; });  // 불투명 패스 (같은 material 끼리) , 반 투명 패스 (머터리얼
}

void FSceneRenderer::DrawBasePassMeshes(FRenderer* Renderer, const TArray<FVisibleMeshDrawCommand>& Commands)
{
	if (!FSkinningStats::IsCollecting())
	{
		Renderer->SubmitVisibleCommands(Commands, RenderData.GPUSceneBuffer, &RenderData.RenderStats.DrawCalls);
		return;
	}

	auto& Timing = RenderData.BaseSkeletalDrawGpu;
	if (!Timing) Timing = std::make_shared<FGpuTimingHistory>();
	Renderer->SubmitVisibleCommands(Commands, RenderData.GPUSceneBuffer, &RenderData.RenderStats.DrawCalls, Timing, RenderData.SkeletalDrawGpuMode);
}

bool FSceneRenderer::PrepareDecalPasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
	const FSceneRenderOptions& Options, const TArray<FVisibleMeshDrawCommand>& SourceCommands,
	uint32 Width, uint32 Height, bool bAlwaysCreate, FDecalPassSetup& Out)
{
	Out = {};

	DecalReceiverCommands.Reset();
	DecalReceiverCommands.Reserve(SourceCommands.Num());

	for (const FVisibleMeshDrawCommand& SourceVis : SourceCommands)
	{
		if (SourceVis.Command && SourceVis.Command->bReceiveDecal && !SourceVis.Command->bIsParticle)
			DecalReceiverCommands.Add(SourceVis);
	}

	FDecalStats& DecalStats = RenderData.DecalStats;
	if (DecalStats.bCollecting && !DecalReceiverCommands.IsEmpty())
	{
		DecalStats.Frame.ReceiverBatchCount = static_cast<uint32>(DecalReceiverCommands.Num());
		DecalStats.Frame.ReceiverItemCount = static_cast<uint32>(DecalReceiverCommands.Num());
	}
	if (Options.bDrawDecals)
	{
		GatherProjectedDecals();
	}
	else
	{
		VisibleDecals.Reset();
	}
	MeshDecalDrawLists.Reset();

	if (Options.DecalRenderMethod == EDecalRenderMethod::MeshRerendering)
	{
		BuildProjectedDecalMeshDrawLists();
	}
	FProjectedDecalRenderer* DecalRenderer = Renderer->GetProjectedDecal();

	Out.bNeedsDBuffer =
		Options.bDrawPrimitives &&
		!View.bIsWireframe &&
		!DecalReceiverCommands.IsEmpty() &&
		!VisibleDecals.IsEmpty();

	if (Out.bNeedsDBuffer &&
		(!DecalRenderer || !DecalRenderer->IsReady()))
	{
		return false;
	}

	if (!Out.bNeedsDBuffer && !bAlwaysCreate)
		return true;

	// 비활성 상태에서도 투명한 DBuffer를 본 패스에 전달한다.
	Out.Lit = Graph.CreateTexture({ Width, Height, ETextureFormat::R8G8B8A8_UNorm }, "DBufferLit");
	Out.Unlit = Graph.CreateTexture({ Width, Height, ETextureFormat::R8G8B8A8_UNorm }, "DBufferUnlit");
	Out.Normal = Graph.CreateTexture({ Width, Height, ETextureFormat::R16G16B16A16_Float }, "DBufferNormal");

	if (!Out.Lit || !Out.Unlit || !Out.Normal) return false;
	if (DecalStats.bCollecting)
	{
		DecalStats.Frame.bHasDBufferPasses = true;

		DecalStats.Frame.DBufferBytes =
			uint64(Width) * uint64(Height)
			* (4ull + 4ull + 8ull);
	}

	Out.bUseReceiverStencil =
		Options.bDecalReceiverStencil &&
		Options.DecalRenderMethod == EDecalRenderMethod::DepthReconstruction &&
		Out.bNeedsDBuffer &&
		Targets.Depth &&
		Targets.Depth->GetReadOnlyDSV() &&
		FRenderDevice::Get()->GetDepthStencilState(
			EDepthStencilState::DecalReceiverWrite) &&
		FRenderDevice::Get()->GetDepthStencilState(
			EDepthStencilState::DecalReceiverTest);
	return true;
}

bool FSceneRenderer::AddDepthPrepass(FRenderGraphBuilder& Graph, FRenderer* Renderer, FRenderGraphTextureRef Depth,
	const FSceneRenderOptions& Options, bool bWriteReceiverStencil)
{
	FPipelineState* DepthPipeline = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_DepthOnly);
	if (!DepthPipeline || !DepthPipeline->Shader || !DepthPipeline->Shader->PixelShader) return false;

	FRenderGraphRasterPassDesc Prepass{};
	Prepass.Name = "DepthPrepass";
	Prepass.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
	return Graph.AddRasterPass(std::move(Prepass), [this, Renderer, Options, bWriteReceiverStencil]
		{
			if (!bCorePassSucceeded || !World) return;
			BuildVisiblePassCommands(World->GetScene(), EMeshPass::DepthPass, Options, false, RenderData.VisibleDepthPassCommands);
			if (RenderData.VisibleDepthPassCommands.IsEmpty()) return;

			if (bWriteReceiverStencil)
			{
				for (FVisibleMeshDrawCommand& Vis : RenderData.VisibleDepthPassCommands)
				{
					Vis.bHasDepthStencilOverride = true;
					Vis.DepthStencilOverride = EDepthStencilState::DecalReceiverWrite;
					Vis.bOverrideStencilRef = true;
					Vis.StencilRef = (Vis.Command && Vis.Command->bReceiveDecal) ? StencilBits::ReceiveDecal : 0;
					Vis.SortKey = (static_cast<uint64>(Vis.StateBucketId) << 32) | static_cast<uint64>(Vis.StencilRef);
				}
				std::stable_sort(RenderData.VisibleDepthPassCommands.begin(), RenderData.VisibleDepthPassCommands.end(),
					[](const FVisibleMeshDrawCommand& A, const FVisibleMeshDrawCommand& B) { return A.SortKey < B.SortKey; });
			}

			Renderer->UploadViewConstants(View.ViewProjectionMatrix, View.ViewMatrix, View.ProjectionMatrix,
				static_cast<float>(EngineTimer::GetTotalTime()));
			Renderer->SubmitVisibleCommands(RenderData.VisibleDepthPassCommands, RenderData.GPUSceneBuffer);
		});
}

bool FSceneRenderer::AddDBufferPasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FRenderGraphTextureRef Depth,
	const FSceneRenderOptions& Options, const FDecalPassSetup& Setup, uint32 Width, uint32 Height)
{
	if (!Setup.Lit || !Setup.Unlit || !Setup.Normal)
		return false;

	FProjectedDecalRenderer* DecalRenderer = Renderer->GetProjectedDecal();
	const bool bNeedsDBuffer = Setup.bNeedsDBuffer;
	const bool bUseDecalReceiverStencil = Setup.bUseReceiverStencil;
	const FRenderGraphTextureRef DBufferNormal = Setup.Normal;
	const bool bUseDepthReconstruction =
		Options.DecalRenderMethod == EDecalRenderMethod::DepthReconstruction;
	auto AddDecalPass =
		[&](FRenderGraphTextureRef Output, bool bUnlitPass)
		{
			FRenderGraphRasterPassDesc DecalPass{};
			DecalPass.Name = bUnlitPass ? "DBufferUnlit" : "DBufferLit";
			DecalPass.ColorAttachments = {
				{
					Output,
					ERenderGraphLoadAction::Clear,
					{ 0.0f, 0.0f, 0.0f, 0.0f }
				},
				{
					DBufferNormal,
					bUnlitPass
						? ERenderGraphLoadAction::Load
						: ERenderGraphLoadAction::Clear,
					{ 0.0f, 0.0f, 0.0f, 0.0f }
				}
			};
			if (bNeedsDBuffer)
			{
				if (bUseDepthReconstruction)
				{
					DecalPass.ReadTextures = { Depth };
					DecalPass.ReadTextureStartSlot = 1;

					// 볼륨 경로의 GreaterEqual 검사를 위해 DSV가 항상 필요하다.
					DecalPass.DepthAttachment = {
						Depth,
						ERenderGraphLoadAction::Load,
						1.0f, 0
					};

					DecalPass.DepthAttachment.bReadOnlyDepthStencil = true;
				}
				else
				{
					// 메시 방식은 Depth Equal 검사에 사용한다.
					DecalPass.DepthAttachment = {
						Depth,
						ERenderGraphLoadAction::Load,
						1.0f, 0
					};
				}
			}

			FDecalStats& Stats = RenderData.DecalStats;
			if (Stats.bCollecting)
			{
				const auto Destination =
					bUnlitPass ? Stats.GpuUnlit : Stats.GpuLit;

				// 실행 전후 람다가 같은 핸들을 공유한다.
				const auto Handle = std::make_shared<int32>(-1);

				DecalPass.OnBeforeExecute =
					[this, Destination, Handle]
					{
						if (!bCorePassSucceeded)
							return;

						*Handle =
							FGpuProfiler::Get().BeginScope(Destination);
					};

				DecalPass.OnAfterExecute =
					[Handle]
					{
						FGpuProfiler::Get().EndScope(*Handle);
					};
			}

			return Graph.AddRasterPass(
				std::move(DecalPass),
				[
					this,
					DecalRenderer,
					bNeedsDBuffer,
					bUseDepthReconstruction,
					bUnlitPass,
					Width,
					Height,
					bUseDecalReceiverStencil
				]
				{
					if (!bCorePassSucceeded || !bNeedsDBuffer)
						return;

					FDecalStats& Stats = RenderData.DecalStats;
					FDecalCpuScope Timer(
						Stats.bCollecting,
						Stats.Frame.SubmitCycles);

					FDecalFrameStats* Counters =
						Stats.bCollecting ? &Stats.Frame : nullptr;

					if (bUseDepthReconstruction)
					{
						bCorePassSucceeded =
							DecalRenderer->OnRenderDepth(
								View,
								Width,
								Height,
								VisibleDecals,
								bUnlitPass,
								Counters,
								bUseDecalReceiverStencil);
					}
					else
					{
						bCorePassSucceeded =
							DecalRenderer->OnRenderMesh(
								DecalReceiverCommands,
								RenderData.GPUSceneBuffer,
								VisibleDecals,
								MeshDecalDrawLists,
								bUnlitPass,
								Counters);
					}
				});
		};

	return AddDecalPass(Setup.Lit, false) && AddDecalPass(Setup.Unlit, true);
}

bool FSceneRenderer::AddWorldNormalPass(FRenderGraphBuilder& Graph, FRenderer* Renderer,
	FSceneViewTargets& Targets, const FSceneRenderOptions& Options)
{
	const FRenderGraphTextureRef SceneColor = Graph.ImportTexture(Targets.SceneColor.get(), "SceneColor");
	const FRenderGraphTextureRef Depth = Graph.ImportTexture(Targets.Depth.get(), "SceneDepth");
	if (!SceneColor || !Depth) return false;

	FRenderGraphRasterPassDesc Pass{};
	Pass.Name = "WorldNormal";
	Pass.ColorAttachments = { { SceneColor, ERenderGraphLoadAction::Clear,{ 0.0f, 0.0f, 0.0f, 1.0f } } };
	Pass.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
	return Graph.AddRasterPass(std::move(Pass), [this, Renderer, bDrawPrimitives = Options.bDrawPrimitives, Options]
		{
			if (!bDrawPrimitives || !World) return;
			auto& DynList = RenderData.DynamicPassCommands[static_cast<size_t>(EMeshPass::WorldNormal)];
			DynList.Reset();
			FWorldNormalPassMeshProcessor Processor;
			for (const FMeshBatch& Batch : RenderData.Collector.GetMeshBatches())
			{
				if (!Processor.AddMeshBatch(Batch, Options, &RenderData.Collector, DynList))
				{
					bCorePassSucceeded = false;
					return;
				}
			}

			TArray<FVisibleMeshDrawCommand> Commands;
			BuildVisiblePassCommands(World->GetScene(), EMeshPass::WorldNormal, Options, false, Commands);
			Renderer->UploadViewConstants(View.ViewProjectionMatrix, View.ViewMatrix, View.ProjectionMatrix,
				static_cast<float>(EngineTimer::GetTotalTime()));
			Renderer->SubmitVisibleCommands(Commands, RenderData.GPUSceneBuffer);
		});
}

bool FSceneRenderer::AddCorePasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
	const FSceneRenderOptions& Options, FSkyboxRenderer* Skybox)
{
	if (!World || !Renderer || !Targets.SceneColor || !Targets.Depth)
		return false;
	const uint32 Width = Targets.SceneColor->GetWidth();
	const uint32 Height = Targets.SceneColor->GetHeight();
	if (!Width || !Height || !Targets.IsValidFor(Width, Height))
		return false;

	bCorePassSucceeded = true;
	bTilesBuilt = false;
	const ESkinningMode SkinningMode = USkeletalMeshComponent::GetSkinningMode();
	if (RenderData.SkeletalDrawGpuMode != SkinningMode)
	{
		// Pending GPU queries still target the old history.
		RenderData.BaseSkeletalDrawGpu.reset();
		RenderData.ShadowSkeletalDrawGpu.reset();
		RenderData.SkeletalDrawGpuMode = SkinningMode;
	}
	if (Options.bShowWorldNormal)
		return AddWorldNormalPass(Graph, Renderer, Targets, Options);

	const FRenderGraphTextureRef SceneColor = Graph.ImportTexture(Targets.SceneColor.get(), "SceneColor"); // Texture Handle 리턴 해줌
	const FRenderGraphTextureRef Depth = Graph.ImportTexture(Targets.Depth.get(), "SceneDepth");
	if (!SceneColor || !Depth)
		return false;

	const FRenderGraphTextureDesc HDRDesc{ Width, Height, ETextureFormat::R16G16B16A16_Float };
	const FRenderGraphTextureRef LightingHDR = Graph.CreateTexture(HDRDesc, "LightingHDR");
	if (!LightingHDR)
		return false;

	if (!RenderData.DynamicPrimitiveData.IsEmpty())
		RenderData.GPUSceneBuffer = World->GetScene().SyncGPUScene(RenderData.DynamicPrimitiveData);

	// HDR 파노라마를 물체보다 먼저 그려 두고, 최종 ToneMap에서 함께 인코딩한다.
	FRenderGraphRasterPassDesc Sky{};
	Sky.Name = "Skybox";
	Sky.ColorAttachments = {
		{ LightingHDR, ERenderGraphLoadAction::Clear, Options.BackgroundColor }
	};
	if (!Graph.AddRasterPass(std::move(Sky), [this, Skybox]
		{
			if (bCorePassSucceeded && Skybox && View.bIsPerspective)
				Skybox->OnRender(View.ViewProjectionMatrix, View.ViewLocation);
		})) return false;

	// 언리얼 RenderShadowDepthMaps: 할당된 그림자 맵마다 Caster 깊이를 아틀라스에 그린다.
	FTexture2D* ShadowDepthAtlas = RenderData.ViewLightResources.GetShadowDepthAtlas();
	const bool bAnyShadowAllocated = std::any_of(RenderData.ProjectedShadows.begin(), RenderData.ProjectedShadows.end(),
		[](const TUniquePtr<FProjectedShadowInfo>& Shadow) { return Shadow->bAllocated; });
	FRenderGraphTextureRef ShadowAtlasRef{};   // Projection 패스가 읽는다
	if (ShadowDepthAtlas && bAnyShadowAllocated)
	{
		const FRenderGraphTextureRef AtlasRef = Graph.ImportTexture(ShadowDepthAtlas, "ShadowDepthAtlas");
		if (!AtlasRef) return false;
		ShadowAtlasRef = AtlasRef;

		FRenderGraphRasterPassDesc ShadowDepths{};
		ShadowDepths.Name = "ShadowDepths";
		ShadowDepths.DepthAttachment = { AtlasRef, ERenderGraphLoadAction::Clear, 1.0f, 0 };   // 단계 8에서 Load로
		if (!Graph.AddRasterPass(std::move(ShadowDepths), [this, Renderer, &Graph, AtlasRef]
			{
				RenderShadowDepthMaps(Renderer, static_cast<FTexture2DArray*>(Graph.Resolve(AtlasRef)));
			})) return false;
	}

	RenderData.VisibleBasePassCommands.Reset();
	if (Options.bDrawPrimitives)
	{
		BuildVisiblePassCommands(World->GetScene(), EMeshPass::BasePass, Options, View.bIsWireframe, RenderData.VisibleBasePassCommands);
	}

	FDecalPassSetup Decal{};
	if (!PrepareDecalPasses(Graph, Renderer, Targets, Options, RenderData.VisibleBasePassCommands, Width, Height, true, Decal))
		return false;
	const bool bNeedsDBuffer = Decal.bNeedsDBuffer;
	const bool bUseDecalReceiverStencil = Decal.bUseReceiverStencil;

	FForwardLightTileGrid& Tiles = RenderData.ViewLightResources.GetTiles();
	FStructuredBuffer* SceneLights = RenderData.ViewLightResources.GetSceneLights();
	FForwardLightingConstants GridLighting{};
	std::memcpy(GridLighting.LightCounts, RenderData.LightParameterCounts, sizeof(RenderData.LightParameterCounts));
	std::memcpy(GridLighting.LightOffsets, RenderData.LightParameterOffsets, sizeof(RenderData.LightParameterOffsets));

	const ELightCullingMode CullingMode = !View.bIsWireframe && Options.ShadingModel != EShadingModel::Gouraud &&
		!Options.bUnlit ? Options.LightCullingMode : ELightCullingMode::Off;

	bool bUseDepthPrepass = false;
	const bool bGridPrepared = Options.bDrawPrimitives && bLightUploadSucceeded &&
		Tiles.PrepareBuild(View, GridLighting, SceneLights, Targets.Depth.get(), CullingMode);

	const bool bNeedsLightCulling = bGridPrepared && CullingMode != ELightCullingMode::Off && (RenderData.HasSpotLights() || RenderData.HasPointLights());
	const bool bForwardShadows = Options.bDrawPrimitives && ShadowAtlasRef && !ShadowedLights.IsEmpty();
	if (bNeedsLightCulling || bForwardShadows || bNeedsDBuffer)
	{
		bUseDepthPrepass = true;
		if (!AddDepthPrepass(Graph, Renderer, Depth, Options, bUseDecalReceiverStencil)) return false;
	}

	FRenderGraphTextureRef ForwardShadowMask{};
	if (bForwardShadows)
	{
		ForwardShadowMask = Graph.CreateTexture({ Width, Height, ETextureFormat::R8G8B8A8_UNorm }, "ScreenShadowMask");
		if (!ForwardShadowMask) return false;

		FRenderGraphRasterPassDesc Projection{};
		Projection.Name = "ShadowProjection";
		Projection.ReadTextures = { Depth, ShadowAtlasRef };
		Projection.ColorAttachments = { { ForwardShadowMask, ERenderGraphLoadAction::Clear,{ 1.0f, 1.0f, 1.0f, 1.0f } } };
		if (!Graph.AddRasterPass(std::move(Projection), [this, Width, Height]
			{
				if (!bCorePassSucceeded) return;
				for (const FShadowedLight& Light : ShadowedLights)
					RenderShadowProjectionsForLight(RenderData.VisibleLightInfos[Light.LightId], Light.Channel, Width, Height);
				RenderCommand::BindConstantBuffer(0, nullptr, EShaderBindFlagBits::Pixel);
			})) return false;
	}

	if (bNeedsLightCulling)
	{
		FRenderGraphComputePassDesc Culling{};
		Culling.Name = CullingMode == ELightCullingMode::Clustered
			? "ClusterLightCulling" : "TileLightCulling";
		Culling.ReadResources = { SceneLights->GetBuffer() };
		if (CullingMode == ELightCullingMode::Tiled)
			Culling.ReadResources.push_back(Targets.Depth->GetRawPtr());

		const std::array<ID3D11Resource*, 4> Writes = Tiles.GetWriteResources();
		Culling.WriteResources.assign(Writes.begin(), Writes.end());
		if (!Graph.AddComputePass(std::move(Culling),
			[this, Renderer, SceneLights, DepthTexture = Targets.Depth.get()]
			{
				if (bCorePassSucceeded)
					bTilesBuilt = RenderData.ViewLightResources.GetTiles().ExecuteBuild(Renderer, DepthTexture, SceneLights);
			}))
			return false;
	}

	if (!AddDBufferPasses(Graph, Renderer, Depth, Options, Decal, Width, Height))
		return false;

	FRenderGraphRasterPassDesc Forward{};
	Forward.Name = "ForwardOpaque";
	Forward.ReadTextures = { Decal.Lit, Decal.Unlit, Decal.Normal };
	Forward.ReadTextureStartSlot = 14;
	Forward.ColorAttachments = {
		{ LightingHDR, ERenderGraphLoadAction::Load }
	};
	Forward.DepthAttachment = { Depth, bUseDepthPrepass ? ERenderGraphLoadAction::Load : ERenderGraphLoadAction::Clear, 1.0f, 0 };
	if (!Graph.AddRasterPass(std::move(Forward), [this, Renderer, &Graph, LightingOptions = Options, bUseDepthPrepass, ForwardShadowMask]
		{
			if (!LightingOptions.bDrawPrimitives) return;
			bCorePassSucceeded = BindForwardLighting(Renderer, LightingOptions, ForwardShadowMask ? Graph.Resolve(ForwardShadowMask) : nullptr);
			if (!bCorePassSucceeded) return;
			Renderer->UploadViewConstants(View.ViewProjectionMatrix, View.ViewMatrix, View.ProjectionMatrix,
				static_cast<float>(EngineTimer::GetTotalTime()));
			if (bUseDepthPrepass)
			{
				for (FVisibleMeshDrawCommand& Vis : RenderData.VisibleBasePassCommands)
				{
					if (Vis.Command && !Vis.Command->bIsParticle)
					{
						Vis.bHasDepthStencilOverride = true;
						Vis.DepthStencilOverride = EDepthStencilState::PrepassReadOnly;
					}
				}
			}
			DrawBasePassMeshes(Renderer, RenderData.VisibleBasePassCommands);

			UnbindForwardDecal(Renderer);
			UnbindForwardLighting(Renderer);
		})) return false;

	bHasFog = false;
	if (Options.bDrawPrimitives && Options.bEnableFog)
	{
		FRenderGraphRasterPassDesc FogPass = MakeViewportPass("HeightFog", LightingHDR);
		FogPass.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(FogPass), [this, Renderer, DepthTarget = Targets.Depth.get(), ColorTarget = Targets.SceneColor.get()]
			{
				if (bCorePassSucceeded)
					bHasFog = RenderHeightFog(Renderer, DepthTarget, ColorTarget);
			})) return false;
	}

	if (Options.bDrawPrimitives)
	{
		const TArray<FGpuParticleBatch>& GpuBatches = RenderData.Collector.GetGpuParticleBatches();
		if (!GpuBatches.IsEmpty())
		{
			FRenderGraphComputePassDesc ComputeDesc{};
			ComputeDesc.Name = "GpuParticleSimulate";
			for (const FGpuParticleBatch& Batch : GpuBatches)
			{
				if (!Batch.GpuResources || Batch.MaxParticles == 0) continue;
				FGpuParticleEmitterResources& Res = *Batch.GpuResources;
				if (!Res.ParticleBuffer || Res.MaxParticles != Batch.MaxParticles)
				{
					Res.ParticleBuffer = RenderCommand::CreateRWStructuredBuffer(Batch.ParticleStride, Batch.MaxParticles);
					Res.MaxParticles = Batch.MaxParticles;
					if (Res.ParticleBuffer)
						RenderCommand::ClearUnorderedAccess(Res.ParticleBuffer.get(), 0);
				}
				else if (Batch.bResetRequested && Res.ParticleBuffer)
				{
					RenderCommand::ClearUnorderedAccess(Res.ParticleBuffer.get(), 0);
				}
				if (Res.ParticleBuffer && Res.ParticleBuffer->GetBuffer())
				{
					ID3D11Resource* RawRes = Res.ParticleBuffer->GetBuffer();
					if (std::find(ComputeDesc.WriteResources.begin(), ComputeDesc.WriteResources.end(), RawRes) == ComputeDesc.WriteResources.end())
						ComputeDesc.WriteResources.push_back(RawRes);
				}
			}

			if (!ComputeDesc.WriteResources.empty())
			{
				Graph.AddComputePass(std::move(ComputeDesc), [this]
					{
						FComputeShader* InjectCS = FRenderResourceManager::GetComputeShader("Resources/Shader/GpuParticleSimulate.hlsl", "CSInjectSpawns");
						FComputeShader* SimCS = FRenderResourceManager::GetComputeShader("Resources/Shader/GpuParticleSimulate.hlsl", "CSSimulate");
						if (!InjectCS || !SimCS) return;

						for (const FGpuParticleBatch& Batch : RenderData.Collector.GetGpuParticleBatches())
						{
							if (!Batch.GpuResources || Batch.MaxParticles == 0) continue;
							FGpuParticleEmitterResources& Res = *Batch.GpuResources;
							if (!Res.ParticleBuffer) continue;

							if (!Res.CurveLUTBuffer)
								Res.CurveLUTBuffer = RenderCommand::CreateDynamicStructuredBuffer(384, sizeof(FVector4));
							if (!Res.SimCB)
								Res.SimCB = RenderCommand::CreateConstantBuffer(sizeof(FGpuParticleSimParams));
							if (!Res.DrawCB)
								Res.DrawCB = RenderCommand::CreateConstantBuffer(sizeof(FGpuParticleDrawParams));
							if (!Res.CurveLUTBuffer || !Res.SimCB || !Res.DrawCB) continue;

							if (void* MappedLUT = RenderCommand::MapWriteDiscard(Res.CurveLUTBuffer.get()))
							{
								std::memcpy(MappedLUT, Batch.CurveLUT.GetData(), 384 * sizeof(FVector4));
								RenderCommand::Unmap(Res.CurveLUTBuffer.get());
							}
							RenderCommand::UpdateBufferData(Res.SimCB.get(), &Batch.SimParams, sizeof(FGpuParticleSimParams));

							if (Batch.SpawnCount > 0 && !Batch.PendingSpawnBytes.IsEmpty())
							{
								if (!Res.SpawnUploadBuffer || Res.SpawnCapacity < Batch.SpawnCount)
								{
									const uint32 NewCap = std::max(Batch.SpawnCount, 64u);
									Res.SpawnUploadBuffer = RenderCommand::CreateDynamicStructuredBuffer(NewCap, Batch.ParticleStride);
									Res.SpawnCapacity = Res.SpawnUploadBuffer ? NewCap : 0;
								}
								if (Res.SpawnUploadBuffer)
								{
									if (void* MappedSpawn = RenderCommand::MapWriteDiscard(Res.SpawnUploadBuffer.get()))
									{
										std::memcpy(MappedSpawn, Batch.PendingSpawnBytes.GetData(), Batch.PendingSpawnBytes.Num());
										RenderCommand::Unmap(Res.SpawnUploadBuffer.get());
									}
									RenderCommand::BindComputeShader(InjectCS);
									RenderCommand::BindConstantBuffer(0, Res.SimCB.get(), EShaderBindFlagBits::Compute);
									RenderCommand::BindShaderResource(0, Res.SpawnUploadBuffer.get(), EShaderBindFlagBits::Compute);
									RenderCommand::BindUnorderedAccess(0, Res.ParticleBuffer.get());
									RenderCommand::Dispatch((Batch.SpawnCount + 63u) / 64u, 1, 1);
									RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Compute);
								}
							}

							RenderCommand::BindComputeShader(SimCS);
							RenderCommand::BindConstantBuffer(0, Res.SimCB.get(), EShaderBindFlagBits::Compute);
							RenderCommand::BindShaderResource(1, Res.CurveLUTBuffer.get(), EShaderBindFlagBits::Compute);
							RenderCommand::BindUnorderedAccess(0, Res.ParticleBuffer.get());
							RenderCommand::Dispatch((Batch.MaxParticles + 255u) / 256u, 1, 1);

							RenderCommand::UnbindUnorderedAccess(0);
							RenderCommand::UnbindShaderResources(1, 1, EShaderBindFlagBits::Compute);
							RenderCommand::BindComputeShader(nullptr);
						}
					});
			}
		}

		if (!Graph.AddRasterPass(MakeViewportPass("Translucent", LightingHDR, Depth),
			[this, Renderer]
			{
				if (!bCorePassSucceeded)
					return;
				FHeightFogRenderer* Fog = Renderer->GetHeightFog();
				RenderTranslucent(Renderer, Fog ? Fog->GetTranslucentFogConstants(bHasFog) : nullptr);
			})) return false;
	}

	if (Options.bDrawPrimitives && Options.bShowShadowMask && ShadowAtlasRef)
	{
		const FRenderGraphTextureRef ShadowMaskVis = Graph.CreateTexture({ Width, Height, ETextureFormat::R8G8B8A8_UNorm }, "ShadowMaskVisualization");
		if (!ShadowMaskVis) return false;

		FRenderGraphRasterPassDesc Projection{};
		Projection.Name = "ShadowProjection";
		Projection.ReadTextures = { Depth, ShadowAtlasRef };
		Projection.ColorAttachments = { { ShadowMaskVis, ERenderGraphLoadAction::Clear,{ 1.0f, 1.0f, 1.0f, 1.0f } } };
		if (!Graph.AddRasterPass(std::move(Projection), [this, Width, Height]
			{
				RenderShadowMaskVisualizationProjections(Width, Height);
			})) return false;

		FRenderGraphRasterPassDesc Visualize{};
		Visualize.Name = "ShadowMaskVisualization";
		Visualize.ReadTextures = { ShadowMaskVis };
		Visualize.ColorAttachments = { { LightingHDR, ERenderGraphLoadAction::Load } };
		if (!Graph.AddRasterPass(std::move(Visualize), [this]
			{
				if (!bCorePassSucceeded) return;
				FPipelineState* Pipeline = FRenderResourceManager::GetPSO(EPSOType::ShadowMaskVisualize);
				if (!Pipeline || !Pipeline->Shader || !Pipeline->Shader->VertexShader || !Pipeline->Shader->PixelShader) return;
				RenderCommand::BindPipelineState(*Pipeline);
				RenderCommand::BindVertexBuffer(nullptr);
				RenderCommand::Draw(3);
			})) return false;
	}

	FRenderGraphRasterPassDesc ToneMap{};
	ToneMap.Name = "ToneMap";
	ToneMap.ReadTextures = { LightingHDR };
	ToneMap.ColorAttachments = { { SceneColor, ERenderGraphLoadAction::Clear,{ 0.0f, 0.0f, 0.0f, 1.0f } } };
	if (!Graph.AddRasterPass(std::move(ToneMap), [this, Renderer, &Graph, LightingHDR, bGamma = Options.bGammaCorrection, DisplayGamma = Options.DisplayGamma]
		{
			if (bCorePassSucceeded)
			{
				if (FPipelineState* Pipeline = FRenderResourceManager::GetPSO(EPSOType::ToneMap);
					Pipeline && Pipeline->Shader && Pipeline->Shader->VertexShader && Pipeline->Shader->PixelShader)
				{
					FToneMapData Data{};
					Data.InvGamma = 1.0f / std::max(DisplayGamma, 0.01f);
					Data.bGammaCorretion = bGamma ? 1.0f : 0.0f;
					FConstantBuffer* Constants = Renderer->GetToneMapConstants();
					RenderCommand::UpdateBufferData(Constants, &Data, sizeof(Data));

					RenderCommand::BindPipelineState(*Pipeline);
					RenderCommand::BindConstantBuffer(0, Constants, EShaderBindFlagBits::Pixel);
					RenderCommand::BindVertexBuffer(nullptr);
					RenderCommand::Draw(3);
				}
			}
		})) return false;

	return true;
}

bool FSceneRenderer::AddViewportPasses(FRenderGraphBuilder& Graph, FRenderer* Renderer,
	FSceneViewTargets& Targets, const FSceneRenderOptions& Options, FSkyboxRenderer* Skybox,
	const FScenePassCallbacks& Callbacks) // 뷰포트에 그려져야 되는 패스들을 옵션에 따라 정해진 순서로 Graph에 등록
{
	FSceneRenderOptions CoreOptions = Options;
	if (Options.bShowLightCulling)
	{
		// 디버그 보기에서 현재 뷰의 실제 타일 목록을 생성한다.
		CoreOptions.ReflectionModel = EReflectionModel::Lambert;
		CoreOptions.ShadingModel = EShadingModel::Phong;
		CoreOptions.bUnlit = false;
	}
	if (!AddCorePasses(Graph, Renderer, Targets, CoreOptions, Skybox)) // 기본적으로 그려져야 되는 Core Pass 들
		return false;

	const FRenderGraphTextureRef SceneColor = Graph.ImportTexture(Targets.SceneColor.get(), "SceneColor");
	const FRenderGraphTextureRef Depth = Graph.ImportTexture(Targets.Depth.get(), "SceneDepth");
	if (!SceneColor || !Depth) return false;
	bFXAAPassSucceeded = true;
	if (!Options.bShowWorldNormal && Callbacks.SceneText &&
		!Graph.AddRasterPass(MakeViewportPass("SceneText", SceneColor, Depth), Callbacks.SceneText))
		return false;

	if (!Options.bShowWorldNormal && Options.bShowSceneDepth && Callbacks.DepthVisualization)
	{
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("SceneDepthVisualization", SceneColor);
		Pass.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Pass), Callbacks.DepthVisualization)) return false;
	}

	if (!Options.bShowWorldNormal && !Options.bShowSceneDepth &&
		View.NeedsCameraOverlay(Options.bShowVignette, Options.bShowCameraOverlay))
	{
		const uint32 Width = Targets.SceneColor->GetWidth();
		const uint32 Height = Targets.SceneColor->GetHeight();
		if (!Graph.AddRasterPass(MakeViewportPass("CameraOverlay", SceneColor),
			[this, Renderer, Width, Height,
			bVignette = Options.bShowVignette, bOverlay = Options.bShowCameraOverlay]
			{
				if (FCameraOverlayRenderer* Overlay = Renderer->GetCameraOverlay())
					Overlay->OnRender(View, Width, Height, bVignette, bOverlay);
			})) return false;
	}

	FRenderGraphTextureRef FinalColor = SceneColor;
	if (!Options.bShowWorldNormal && Callbacks.FXAAOutput && Callbacks.FXAA)
	{
		FinalColor = Graph.ImportTexture(Callbacks.FXAAOutput, "FXAAOutput");
		if (!FinalColor) return false;
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("FXAA", FinalColor);
		Pass.ReadTextures = { SceneColor };
		if (!Graph.AddRasterPass(std::move(Pass), [this, ApplyFXAA = Callbacks.FXAA]
			{
				bFXAAPassSucceeded = ApplyFXAA();
			})) return false;
	}

	if (Options.bShowLightCulling && Options.bDrawPrimitives && !Options.bShowWorldNormal)
	{
		FRenderGraphRasterPassDesc Debug = MakeViewportPass("LightCullingDebug", FinalColor);
		Debug.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Debug), [this, Renderer, &Graph, Depth]
			{
				if (!bCorePassSucceeded) return;
				FPipelineState* Pipeline = FRenderResourceManager::GetPSO(EPSOType::LightCullingDebug);
				if (!Pipeline || !Pipeline->Shader || !Pipeline->Shader->VertexShader || !Pipeline->Shader->PixelShader)
				{
					bCorePassSucceeded = false;
					return;
				}
				FForwardLightTileGrid& Tiles = RenderData.ViewLightResources.GetTiles();
				Tiles.PreparePixelShader(Renderer, bTilesBuilt);
				RenderCommand::BindConstantBuffer(3, Tiles.GetPixelConstants(), EShaderBindFlagBits::Pixel);
				RenderCommand::BindShaderResource(9, Tiles.GetPixelResources()[0], EShaderBindFlagBits::Pixel);
				RenderCommand::BindPipelineState(*Pipeline);
				RenderCommand::BindVertexBuffer(nullptr);
				RenderCommand::Draw(3);
				RenderCommand::UnbindShaderResources(9, 1, EShaderBindFlagBits::Pixel);
				RenderCommand::BindConstantBuffer(3, nullptr, EShaderBindFlagBits::Pixel);
				Tiles.ResetPixelState();
			})) return false;
	}

	const FSceneOverlayCallbacks& Overlay = Callbacks.Overlays;
	if (Options.bShowCascade && Callbacks.CascadeVisualization)
	{
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("CascadeFloorVisualization", FinalColor);
		Pass.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Pass), Callbacks.CascadeVisualization)) return false;
	}
	if (!AddOverlayPass(Graph, "EditorLines", FinalColor, Depth, Overlay.EditorLines) ||
		!AddOverlayPass(Graph, "EditorGrid", FinalColor, Depth, Overlay.EditorGrid) ||
		!AddOverlayPass(Graph, "EditorOutline", FinalColor, Depth, Overlay.EditorOutline) ||
		!AddOverlayPass(Graph, "EditorGizmo", FinalColor, Depth, Overlay.EditorGizmo, ERenderGraphLoadAction::Clear) ||
		!AddOverlayPass(Graph, "SIEOutline", FinalColor, Depth, Overlay.SIEOutline) ||
		!AddOverlayPass(Graph, "SIEGizmo", FinalColor, Depth, Overlay.SIEGizmo, ERenderGraphLoadAction::Clear))
		return false;
	if (Overlay.bClearEditorDepth)
	{
		FRenderGraphRasterPassDesc ClearDepth{};
		ClearDepth.Name = "EditorClearDepth";
		ClearDepth.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
		if (!Graph.AddRasterPass(std::move(ClearDepth), [] {})) return false;
	}
	if (!AddOverlayPass(Graph, "EditorUUID", FinalColor, Depth, Overlay.EditorUUID)) return false;
	if (!AddOverlayPass(Graph, "GameUI", FinalColor, Depth, Overlay.GameUI)) return false;
	return true;
}


bool FSceneRenderer::Render(FRenderer* Renderer, FSceneViewTargets& Targets, const FSceneRenderOptions& Options, const std::function<void(FScenePassCallbacks&)>& ExtraPasses)
{
	RenderData.DecalStats.Frame.bValid = false;

	FTexture2D* ColorTarget = Targets.SceneColor.get();
	FTexture2D* DepthTarget = Targets.Depth.get();
	if (!World || !Renderer || !ColorTarget || !DepthTarget)
		return false;
	const uint32 Width = ColorTarget->GetWidth();
	const uint32 Height = ColorTarget->GetHeight();
	if (!Width || !Height || !Targets.IsValidFor(Width, Height))
		return false;

	FDecalStats& DecalStats = RenderData.DecalStats;
	DecalStats.BeginView(
		FStatOverlay::IsEnabled(EStatFlags::Decal),
		Options.DecalRenderMethod,
		Width,
		Height);
	FDecalViewStatsScope DecalStatsScope(DecalStats);

	FScenePassCallbacks Callbacks;
	Callbacks.SceneText = [this, Renderer] { RenderSceneText(Renderer); };

	FFXAARenderer* FXAA = Renderer->GetFXAA();
	const bool bUseFXAA = Options.bEnableFXAA && !Options.bShowSceneDepth &&
		!Options.bShowWorldNormal && FXAA && FXAA->PrepareOutput(ColorTarget);
	if (bUseFXAA)
	{
		Callbacks.FXAAOutput = FXAA->GetOutputTarget().get();
		Callbacks.FXAA = [FXAA, ColorTarget] { return FXAA->OnRender(ColorTarget); };
	}

	if (ExtraPasses) ExtraPasses(Callbacks);

	FRenderGraphBuilder Graph;
	FSkyboxRenderer* Skybox = Callbacks.SkyboxOverride ? Callbacks.SkyboxOverride : Renderer->GetSkybox();
	if (!AddViewportPasses(Graph, Renderer, Targets, Options, Skybox, Callbacks) ||
		!Graph.Execute() || !DidCorePassSucceed() || !DidFXAAPassSucceed())
		return false;
	if (bUseFXAA && !Targets.SwapSceneColorAndBind(FXAA->GetOutputTarget()))
		return false;

	RenderCommand::EndRenderPass();

	DecalStatsScope.MarkSuccess();
	return true;
}

// 반투명 렌더링
void FSceneRenderer::RenderTranslucent(FRenderer* Renderer, FConstantBuffer* FogConstants)
{
	if (!Renderer || !World)
		return;

	RenderCommand::BindConstantBuffer(3, FogConstants, EShaderBindFlagBits::Pixel);
	if (!RenderData.DynamicPrimitiveData.IsEmpty())
		RenderData.GPUSceneBuffer = World->GetScene().SyncGPUScene(RenderData.DynamicPrimitiveData);

	BuildVisiblePassCommands(World->GetScene(), EMeshPass::Translucency, FSceneRenderOptions{}, false, RenderData.VisibleTranslucencyCommands);
	if (!RenderData.VisibleTranslucencyCommands.IsEmpty())
	{
		Renderer->UploadViewConstants(View.ViewProjectionMatrix, View.ViewMatrix, View.ProjectionMatrix,
			static_cast<float>(EngineTimer::GetTotalTime()));
		Renderer->SubmitVisibleCommands(RenderData.VisibleTranslucencyCommands, RenderData.GPUSceneBuffer, &RenderData.RenderStats.DrawCalls);
	}

	RenderCommand::BindConstantBuffer(3, nullptr, EShaderBindFlagBits::Pixel);
}

void FSceneRenderer::GatherViewVisibilityAndDynamicElements(FScene& Scene, FSceneRenderData& Data,
	const FViewContext& View, const FSceneRenderOptions& Options,
	const FFrustumPlanes* Frustum, FRenderer* Renderer)
{
	Data.ResetFrame();
	FMeshElementCollector& Collector = Data.Collector;
	FRenderStats& RenderStats = Data.RenderStats;
	auto& VisibleProxies = Data.VisibleProxies;
	auto& LODInputs = Data.LODInputs;
	auto& SelectedLODs = Data.SelectedLODs;

	const uint32 NumPrimitives = Scene.GetNumPrimitives();
	Data.PrimitiveVisibilityMap.Init(uint8(0), NumPrimitives);
	Data.PrimitiveOccludedMap.Init(uint8(0), NumPrimitives);
	Data.PrimitiveSelectedLODs.Init(uint8(0), NumPrimitives);

	{
		SCOPE_CYCLE_COUNTER(STAT_FrustumCull);
		const auto Visit = [&](FPrimitiveSceneProxy* Proxy)
			{
				if (Proxy->IsShown(View.bGameView, View.bShowSkeletalMeshes)) VisibleProxies.Add(Proxy);
			};

		if (Frustum)
		{
			Scene.BVH.QueryCull(
				FrustumAllPlanesMask,
				[Frustum](const FBox& Bounds, uint32& Mask)
				{
					return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, *Frustum, Mask));
				},
				Visit);
		}
		else
		{
			Scene.BVH.QueryCull(0, [](const FBox&, uint32&) { return EBVHCullResult::Inside; }, Visit);
		}
	}

	for (FPrimitiveSceneProxy* Proxy : VisibleProxies)
	{
		if (Proxy)
			Proxy->MarkRendered();
	}

	RenderStats.TotalPrimitives = NumPrimitives;

	const uint8* OccludedMask = nullptr;
	if (Renderer && Renderer->GetGPUOcclusion().GetSettings().bEnabled)
	{
		FGPUOcclusion& Occlusion = Renderer->GetGPUOcclusion();
		if (Occlusion.Run(VisibleProxies.GetData(), VisibleProxies.Num(), View))
		{
			const auto& Occluded = Occlusion.GetOccluded();
			if (Occlusion.GetSettings().bCull)
			{
				uint32 Kept = 0;
				for (uint32 i = 0; i < VisibleProxies.Num(); ++i)
					if (!Occluded[i])
						VisibleProxies[Kept++] = VisibleProxies[i];
				VisibleProxies.SetNum(Kept);
			}
			else
			{
				OccludedMask = Occluded.data();
			}
		}
	}

	RenderStats.VisiblePrimitives = VisibleProxies.Num();

	{
		SCOPE_CYCLE_COUNTER(STAT_GatherElements);

		LODInputs.Reset();
		LODInputs.Reserve(VisibleProxies.Num());
		for (const FPrimitiveSceneProxy* Proxy : VisibleProxies)
			LODInputs.Add({ Proxy->GetLODSphere(), Proxy->GetRenderState() });
		SelectLODs(LODInputs, View, SelectedLODs);

		for (uint32 VisibleIndex = 0; VisibleIndex < VisibleProxies.Num(); ++VisibleIndex)
		{
			FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
			if (!Proxy || !Proxy->IsVisible())
				continue;

			const uint8 LOD = SelectedLODs[VisibleIndex];
			if (Proxy->PackedIndex >= 0 && static_cast<uint32>(Proxy->PackedIndex) < NumPrimitives)
			{
				const uint32 PrimIdx = static_cast<uint32>(Proxy->PackedIndex);
				Data.PrimitiveVisibilityMap[PrimIdx] = 1;
				Data.PrimitiveSelectedLODs[PrimIdx] = LOD;
				if (OccludedMask && OccludedMask[VisibleIndex])
					Data.PrimitiveOccludedMap[PrimIdx] = 1;
			}

			if (Proxy->GetRenderState() && Proxy->GetLODCount() > 0)
			{
				const uint32 ClampedLOD = std::min(static_cast<uint32>(LOD), Proxy->GetLODCount() - 1u);
				if (ClampedLOD < 4)
				{
					++RenderStats.LODCounts[ClampedLOD];
					const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(ClampedLOD);
					for (uint32 s = 0; s < CachedLOD.NumSections; ++s)
						RenderStats.LODTriangles[ClampedLOD] += Proxy->GetSection(CachedLOD.FirstSection + s).IndexCount / 3;
				}
			}
		}

		Scene.UpdateParticleLODs(View.CameraPosition);

		for (uint32 VisibleIndex = 0; VisibleIndex < VisibleProxies.Num(); ++VisibleIndex)
		{
			FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
			if (!Proxy || !Proxy->IsVisible() || !Proxy->HasDynamicRelevance())
				continue;

			const uint32 LOD = SelectedLODs[VisibleIndex];
			Proxy->GetDynamicMeshElements(View, Collector, LOD);
		}

		TArray<FMeshBatch>& DynamicBatches = Collector.GetMeshBatchesMutable();
		for (FMeshBatch& Batch : DynamicBatches)
		{
			if (Batch.bIsParticle)
				continue;
			const bool bUsesGPUSceneMaterial = !Batch.Material ||
				Batch.Material->GetPSOType() == EPSOType::StaticMesh_Opaque ||
				Batch.Material->GetPSOType() == EPSOType::StaticMesh_Translucent ||
				Batch.Material->GetPSOType() == EPSOType::StaticMesh_Wireframe;
			if (!bUsesGPUSceneMaterial)
				continue;

			for (FMeshBatchElement& Element : Batch.Elements)
			{
				if (Element.WorldMatrix == nullptr && Element.Proxy && Element.Proxy->PackedIndex >= 0)
				{
					Element.PrimitiveId = static_cast<uint32>(Element.Proxy->PackedIndex);
				}
				else
				{
					const FMatrix& Model = Element.WorldMatrix ? *Element.WorldMatrix
						: (Element.Proxy ? Element.Proxy->GetLocalToWorld() : FMatrix::Identity);
					const uint32 DynIdx = NumPrimitives + static_cast<uint32>(Data.DynamicPrimitiveData.Num());
					Data.DynamicPrimitiveData.Add(MakePrimitiveSceneData(Model, Element.bReceiveShadow, Element.bReceiveDecal));
					Element.PrimitiveId = DynIdx;
				}
			}
		}

		Collector.CommitGpuResources(); // CPU에 모은 동적 데이터를 GPU에 처음이자 한 번에 올리는 지점
		Data.GPUSceneBuffer = Scene.SyncGPUScene(Data.DynamicPrimitiveData); // CPU에 있는 모든 오브젝트의 변환/플래그 데이터(FPrimitiveSceneData)를 GPU의 t17 버퍼(GPUSceneBuffer)와 최신 상태로 맞춘 뒤, 그 GPU 버퍼 포인터를 렌더 데이터에 받아둔다

		const FBasePassMeshProcessor BaseProcessor;
		const FDepthPassMeshProcessor DepthProcessor;
		const FTranslucencyPassMeshProcessor TranslucencyProcessor;
		const FShadowDepthMeshProcessor ShadowProcessor;

		for (const FMeshBatch& Batch : Collector.GetMeshBatches()) // 이번 프레임 GetDynamicMeshElements에서 수집한 동적 요소들(스켈레탈 메시·파티클·빌보드)을 패스별 FMeshDrawCommand로 변환해서 한 프레임용 임시 배열(Data.DynamicPassCommands)에 모아둔다
		{
			BaseProcessor.AddMeshBatch(Batch, Options, &Collector, Data.DynamicPassCommands[static_cast<size_t>(EMeshPass::BasePass)]);
			DepthProcessor.AddMeshBatch(Batch, Options, &Collector, Data.DynamicPassCommands[static_cast<size_t>(EMeshPass::DepthPass)]);
			TranslucencyProcessor.AddMeshBatch(Batch, Options, &Collector, Data.DynamicPassCommands[static_cast<size_t>(EMeshPass::Translucency)]);
			ShadowProcessor.AddMeshBatch(Batch, Options, &Collector, Data.DynamicPassCommands[static_cast<size_t>(EMeshPass::ShadowDepth)]);
		}

		for (uint64 T : RenderStats.LODTriangles)
			RenderStats.Triangles += T;
	}
}

void FSceneRenderer::GatherProjectedDecals()
{
	VisibleDecals.Reset();

	FDecalStats& Stats = RenderData.DecalStats;
	FDecalFrameStats& Frame = Stats.Frame;

	if (!World)
		return;

	{
		FDecalCpuScope Timer(Stats.bCollecting, Frame.GatherCycles);

		for (TObjectIterator<UDecalComponent> It; It; ++It)
		{
			UDecalComponent* Component = *It;

			if (!Component || !Component->IsRegistered())
			{
				continue;
			}

			AActor* Owner = Component->GetOwner();

			if (!Owner || Owner->GetWorld() != World)
			{
				continue;
			}

			if (Stats.bCollecting)
				++Frame.WorldCount;

			if (!Component->IsShown(View.bGameView))
			{
				if (Stats.bCollecting)
					++Frame.HiddenCount;

				continue;
			}

			FProjectedDecalRenderData Data{};

			if (!Component->TryGetRenderData(Data))
			{
				if (Stats.bCollecting)
					++Frame.RenderDataRejected;

				continue;
			}

			const FBox Bounds = DecalBounds::GetWorldBox(Data);

			if (!DecalBounds::IsFinite(Bounds))
			{
				if (Stats.bCollecting)
					++Frame.RenderDataRejected;

				continue;
			}

			if (Stats.bCollecting)
				++Frame.EligibleCount;

			uint32 PlaneMask = FrustumAllPlanesMask;

			if (ClassifyBoxInFrustum(Bounds, View.Frustum, PlaneMask) ==
				EFrustumTest::Outside)
			{
				if (Stats.bCollecting)
					++Frame.ViewFrustumRejectedDecals;

				continue;
			}

			VisibleDecals.Add(Data);
		}
	}

	{
		FDecalCpuScope Timer(
			Stats.bCollecting,
			Frame.SortCycles);

		std::sort(
			VisibleDecals.begin(),
			VisibleDecals.end(),
			[](const auto& A, const auto& B)
			{
				if (A.SortOrder != B.SortOrder)
					return A.SortOrder < B.SortOrder;
				return A.ComponentID < B.ComponentID;
			});
	}
}

void FSceneRenderer::BuildProjectedDecalMeshDrawLists()
{
	MeshDecalDrawLists.Reset();
	MeshDecalDrawLists.SetNum(VisibleDecals.Num());

	if (!World ||
		VisibleDecals.IsEmpty() ||
		DecalReceiverCommands.IsEmpty())
	{
		return;
	}

	FDecalStats& Stats = RenderData.DecalStats;
	FDecalFrameStats& Frame = Stats.Frame;

	FDecalCpuScope Timer(
		Stats.bCollecting,
		Frame.ReceiverSelectionCycles);

	FScene& Scene = World->GetScene();

	using FItemRefs = TArray<FDecalReceiverItemRef>;

	TMap<const FPrimitiveSceneProxy*, FItemRefs> ReceiverLookup;

	FItemRefs FallbackItems;

	// BVH 갱신 대기가 남아있으면 오래된 Bounds로 메시가 누락되지 않도록 보수적으로 처리
	const bool bBVHReady =
		!Scene.bElementListChanged &&
		Scene.DirtyProxies.IsEmpty() &&
		Scene.RenderStateDirtyProxies.IsEmpty();

	auto IsIndexedProxy =
		[&](const FPrimitiveSceneProxy* Proxy)
		{
			if (!bBVHReady ||
				!Proxy ||
				Proxy->GetScene() != &Scene)
			{
				return false;
			}

			const int32 Index = Proxy->PackedIndex;

			return
				Index >= 0 &&
				Index < Scene.PrimitiveSceneProxies.Num() &&
				Scene.PrimitiveSceneProxies[Index] == Proxy;
		};

	// 뷰당 한 번: Proxy -> 현재 뷰의 수신 Command 목록
	for (uint32 CmdIndex = 0; CmdIndex < DecalReceiverCommands.Num(); ++CmdIndex)
	{
		const FVisibleMeshDrawCommand& Vis = DecalReceiverCommands[CmdIndex];

		if (!Vis.Command || !Vis.Command->bReceiveDecal)
			continue;

		const FDecalReceiverItemRef Ref{ CmdIndex };

		if (IsIndexedProxy(Vis.Command->Proxy))
		{
			ReceiverLookup[Vis.Command->Proxy].Add(Ref);
		}
		else
		{
			FallbackItems.Add(Ref);
		}
	}

	for (uint32 DecalIndex = 0; DecalIndex < VisibleDecals.Num(); ++DecalIndex)
	{
		const FProjectedDecalRenderData& Decal = VisibleDecals[DecalIndex];

		FItemRefs& Selected = MeshDecalDrawLists[DecalIndex].Items;

		const FBox DecalWorldBox = DecalBounds::GetWorldBox(Decal);

		const FBox QueryBox = DecalBounds::GetWorldBox(Decal);

		const FMatrix WorldToDecal = Decal.DecalToWorld.Inverse();

		if (bBVHReady && !ReceiverLookup.IsEmpty())
		{
			Scene.BVH.Query(
				[&](const FBox& Bounds)
				{
					if (Stats.bCollecting)
						++Frame.MeshBVHBoundsTests;

					return DecalBounds::Overlaps(
						QueryBox, Bounds);
				},
				[&](FPrimitiveSceneProxy* Proxy)
				{
					if (Stats.bCollecting)
						++Frame.MeshBVHProxyCandidates;

					const FItemRefs* Found = ReceiverLookup.Find(Proxy);

					// 월드에는 있지만 이번 뷰에서 실제로 그릴 수신 Item이 없는 경우
					if (!Found || Found->IsEmpty())
						return;

					const FBox ReceiverWorldBox = DecalBounds::GetProxyWorldBox(*Proxy);

					if (!DecalBounds::MayIntersectVolume(
						ReceiverWorldBox, WorldToDecal))
					{
						if (Stats.bCollecting)
							++Frame.MeshRefineRejectedProxies;

						return;
					}

					Selected.Append(*Found);
				}
			);
		}

		// Bounds를 확실히 알 수 없는 Item은 일단 포함한다.
		// 이 목록이 커지면 후속으로 명시적 Bounds를 추가한다.
		Selected.Append(FallbackItems);

		if (Stats.bCollecting)
		{
			Frame.MeshFallbackItems += FallbackItems.Num();
		}

		// BVH 방문 순서 대신 기존 Batch/Item 순서로 제출.
		std::sort(Selected.begin(), Selected.end());

		// 중복 제거. 같은 Batch/Item이 여러 번 선택될 수 있다.
		auto NewEnd = std::unique(Selected.begin(), Selected.end());
		Selected.SetNum(static_cast<int32>(NewEnd - Selected.begin()));

		if (Stats.bCollecting)
		{
			Frame.MeshMatchedItems += Selected.size();

			if (Selected.IsEmpty())
				++Frame.MeshNoReceiverDecals;
		}
	}
}
