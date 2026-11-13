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
#include "Tasks/Tasks.h"
#include "Core/Stats/LightweightStats.h"
#include "Core/StatOverlay.h"
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

	// 월드 프리미티브 수집 및 절두체 컬링
	GatherRenderPackets(World->GetScene(), RenderData, CachedViewContext, &View.Frustum, Renderer);
	RenderData.AmbientLight = World->GetScene().GetSkyAmbientLight(View.bGameView);

	// 모든 프록시가 같은 FLightRenderParameters를 채우고, 종류는 Type으로만 구분한다.
	auto AddVisibleLight = [this](const FLightSceneProxy* Light)
		{
			FLightRenderParameters Parameters{};
			Light->GetLightShaderParameters(Parameters);
			const int32 Index = RenderData.VisibleLights.Add(Parameters);
			RenderData.VisibleLightProxies.Add(Light);
			if (Light->GetLightComponent() == RenderData.PreviewLight) RenderData.PreviewLightIndex = Index;
		};

	const FScene& Scene = World->GetScene();
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

bool FSceneRenderer::BindDeferredLighting(FRenderer* Renderer, uint32 Width, uint32 Height, int32 SingleLightIndex, const FSceneRenderOptions& Options)
{
	FSceneLightResources& LightResources = RenderData.ViewLightResources;

	FConstantBuffer* DefferedConstants = LightResources.GetDeferredConstants();

	if (!bLightUploadSucceeded || !DefferedConstants)
	{
		return false;
	}

	FDeferredLightConstants Constants{};
	Constants.bSingleLightPass = SingleLightIndex >= 0 ? 1u : 0u;
	Constants.ReflectionModel = static_cast<uint32>(Options.ReflectionModel);
	Constants.bOverrideMaterialLighting = Options.bOverrideMaterialLighting ? 1u : 0u;
	Constants.SingleLightIndex = SingleLightIndex >= 0 ? static_cast<uint32>(SingleLightIndex) : 0u;
	Constants.InverseViewProjection = View.ViewProjectionMatrix.Inverse();
	Constants.CameraPosition = FVector4(View.ViewLocation.X, View.ViewLocation.Y, View.ViewLocation.Z, View.bIsPerspective ? 1.0f : 0.0f);
	Constants.CameraForward = FVector4(View.ViewForward.X, View.ViewForward.Y, View.ViewForward.Z, 0.0f);

	const FSkyLightEnvironment* Sky = RenderData.bHasSkyLight ? World->GetScene().FindSkyLightEnvironment(RenderData.SkyLight.SourcePath) : nullptr;
	Constants.Ambient = Sky ? FAmbientLightRenderData{ FVector4(0.0f) } : RenderData.AmbientLight; // 지금 환경광 계산이 ambient + skylight로 되어있어서 sky 잇으면 ambient 0으로 해줘야됨
	Constants.SkyColorIntensity = Sky ? RenderData.SkyLight.ColorIntensity : FVector4(0.f);

	std::memcpy(Constants.LightCounts, RenderData.LightParameterCounts, sizeof(RenderData.LightParameterCounts));
	std::memcpy(Constants.LightOffsets, RenderData.LightParameterOffsets, sizeof(RenderData.LightParameterOffsets)); // LightOffset은 각 조명별로 시작하는 위치

	Constants.ViewportSize = FVector4(static_cast<float>(Width), static_cast<float>(Height), 0.0f, 0.0f);
	RenderCommand::UpdateBufferData(DefferedConstants, &Constants, sizeof(Constants));
	RenderCommand::BindConstantBuffer(0, DefferedConstants, EShaderBindFlagBits::Pixel);
	// t0~t3은 패스의 ReadTextures(BaseColor, Normal, Depth, ScreenShadowMask)가 묶는다.
	RenderCommand::BindShaderResource(6, RenderData.HasLights() ? LightResources.GetSceneLights() : nullptr, EShaderBindFlagBits::Pixel);

	return true;
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

void FSceneRenderer::DrawBasePassMeshes(FRenderer* Renderer, const TArray<FMeshDrawBatch>& Batches)
{
	if (!FSkinningStats::IsCollecting())
	{
		Renderer->DrawMeshes(Batches);
		return;
	}

	auto& Timing = RenderData.BaseSkeletalDrawGpu;
	if (!Timing) Timing = std::make_shared<FGpuTimingHistory>();
	Renderer->DrawMeshes(Batches, Timing, RenderData.SkeletalDrawGpuMode);
}

bool FSceneRenderer::PrepareDecalPasses(FRenderGraphBuilder& Graph, FRenderer* Renderer, FSceneViewTargets& Targets,
	const FSceneRenderOptions& Options, const TArray<FMeshDrawBatch>& SourceBatches,
	uint32 Width, uint32 Height, bool bAlwaysCreate, FDecalPassSetup& Out)
{
	Out = {};

	// Receiver Decal 수집
	DecalReceiverBatches.Reset();
	DecalReceiverBatches.Reserve(SourceBatches.Num());

	for (const FMeshDrawBatch& SourceBatch : SourceBatches)
	{
		bool bHasReceiver = false;

		for (const FMeshDrawBatch::FItem& Item : SourceBatch.Items)
		{
			if (Item.bReceiveDecal)
			{
				bHasReceiver = true;
				break;
			}
		}

		if (!bHasReceiver)
			continue;

		FMeshDrawBatch ReceiverBatch = SourceBatch;

		ReceiverBatch.Items.Reset();
		ReceiverBatch.Items.Reserve(SourceBatch.Items.Num());

		for (const FMeshDrawBatch::FItem& Item : SourceBatch.Items)
		{
			if (Item.bReceiveDecal)
			{
				ReceiverBatch.Items.Add(Item);
			}
		}
		DecalReceiverBatches.Add(ReceiverBatch);
	}

	// 데칼 패스 확인
	FDecalStats& DecalStats = RenderData.DecalStats;
	if (DecalStats.bCollecting)
	{
		for (const FMeshDrawBatch& Batch : DecalReceiverBatches)
		{
			if (Batch.Items.IsEmpty())
				continue;

			++DecalStats.Frame.ReceiverBatchCount;
			DecalStats.Frame.ReceiverItemCount +=
				static_cast<uint32>(Batch.Items.Num());
		}
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
		!DecalReceiverBatches.IsEmpty() &&
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
			* (4ull + 4ull + 8ull); // 4채널 8비트 * 2장 + 4채널 16비트 * 1장
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
	const TArray<FMeshDrawBatch>* Batches, bool bWriteReceiverStencil)
{
	FPipelineState* DepthPipeline = FRenderResourceManager::GetPSO(EPSOType::StaticMesh_DepthOnly);
	if (!DepthPipeline || !DepthPipeline->Shader || !DepthPipeline->Shader->PixelShader) return false;

	// 본 패스가 이 깊이를 다시 쓰므로 PrepassReadOnly로 같은 픽셀만 그린다.
	FRenderGraphRasterPassDesc Prepass{};
	Prepass.Name = "DepthPrepass";
	Prepass.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
	return Graph.AddRasterPass(std::move(Prepass), [this, Renderer, Batches, DepthPipeline, bWriteReceiverStencil]
		{
			if (!bCorePassSucceeded || !Batches || Batches->IsEmpty()) return;
			// GBuffer 배치는 뷰 상수를 스스로 묶지 않으므로 먼저 올린다 (Forward에도 같은 값이라 무해).
			Renderer->UploadViewMatrix(View.ViewProjectionMatrix);

			for (const FMeshDrawBatch& SourceBatch : *Batches)
			{
				if (SourceBatch.Items.IsEmpty())
					continue;

				if (!SourceBatch.Pipeline.Shader ||
					!SourceBatch.Pipeline.Shader->VertexShader)
				{
					bCorePassSucceeded = false;
					return;
				}

				// 각 배치의 원래 VS를 사용한다.
				FShaderProgram DepthProgram(
					SourceBatch.Pipeline.Shader->VertexShader,
					DepthPipeline->Shader->PixelShader);

				FMeshDrawBatch Batch = SourceBatch;

				Batch.Pipeline = *DepthPipeline;
				Batch.Pipeline.Shader = &DepthProgram;

				// 원래 표면과 같은 래스터 규칙 유지
				Batch.Pipeline.RasterizerState =
					SourceBatch.Pipeline.RasterizerState;

				Batch.Pipeline.StencilRef = 0;

				if (bWriteReceiverStencil)
				{
					Batch.Pipeline.DepthStencilState =
						EDepthStencilState::DecalReceiverWrite;
				}

				for (FMeshDrawBatch::FItem& Item : Batch.Items)
				{
					Item.bOverrideStencilRef =
						bWriteReceiverStencil;

					Item.StencilRef =
						Item.bReceiveDecal
						? StencilBits::ReceiveDecal
						: 0;
				}

				// DepthProgram의 수명이 살아 있는 동안 바로 제출한다.
				Renderer->DrawMeshes(std::span<const FMeshDrawBatch>(&Batch, 1));
			}
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
								DecalReceiverBatches,
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
	return Graph.AddRasterPass(std::move(Pass), [this, Renderer, bDrawPrimitives = Options.bDrawPrimitives]
		{
			if (!bDrawPrimitives) return;
			FMeshPassProcessor Processor;
			TArray<FMeshDrawBatch> Batches;
			bCorePassSucceeded = Processor.Build(View, RenderData.RenderQueue,
				&RenderData, EMeshPassType::WorldNormal, Batches);
			if (bCorePassSucceeded)
				Renderer->DrawMeshes(Batches);
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
	if (RenderData.SkeletalDrawGpuPath != Options.RenderingPath || RenderData.SkeletalDrawGpuMode != SkinningMode)
	{
		// Pending GPU queries still target the old history.
		RenderData.BaseSkeletalDrawGpu.reset();
		RenderData.ShadowSkeletalDrawGpu.reset();
		RenderData.SkeletalDrawGpuPath = Options.RenderingPath;
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

	PrepareDrawData(Renderer, RenderData);

	// HDR 파노라마를 물체보다 먼저 그려 두고, 최종 ToneMap에서 함께 인코딩한다.
	FRenderGraphRasterPassDesc Sky{};
	Sky.Name = "Skybox";
	Sky.ColorAttachments = {
		{ LightingHDR, ERenderGraphLoadAction::Clear,{ 0.0f, 0.0f, 0.0f, 1.0f } }
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

	if (Options.RenderingPath == ERenderingPath::Deferred) // Deferred rendering
	{
		const FRenderGraphTextureDesc GBufferDesc{ Width, Height, ETextureFormat::R8G8B8A8_UNorm };
		const FRenderGraphTextureRef BaseColor = Graph.CreateTexture(GBufferDesc, "BaseColorMetallic"); // 임시 텍스쳐 생성
		const FRenderGraphTextureRef Normal = Graph.CreateTexture(HDRDesc, "NormalRoughness");
		const FRenderGraphTextureRef Emissive = Graph.CreateTexture(HDRDesc, "EmissiveModel");
		const FRenderGraphTextureRef Specular = Graph.CreateTexture(HDRDesc, "SpecularShininess");
		if (!BaseColor || !Normal || !Emissive || !Specular)
			return false;

		if (Options.bDrawPrimitives)
		{
			FRenderGraphRasterPassDesc GBuffer{};
			GBuffer.Name = "GBuffer";
			GBuffer.ColorAttachments = {
				{ BaseColor, ERenderGraphLoadAction::Clear,{ 0.0f, 0.0f, 0.0f, 0.0f } },
				{ Normal, ERenderGraphLoadAction::Clear,{ 0.0f, 0.0f, 0.0f, 0.0f } },
				{ Emissive, ERenderGraphLoadAction::Clear,{ 0.0f, 0.0f, 0.0f, 0.0f } },
				{ Specular, ERenderGraphLoadAction::Clear,{ 0.0f, 0.0f, 0.0f, 1.0f } }
			}; // GBuffer Pass 에는 BaseColor 와 Normal Texture가 Clear된 상태로 필요하다

			// 프리패스와 GBuffer 패스가 같은 배치를 쓰므로 미리 만든다.
			GBufferBatches.Reset();
			{
				FMeshPassProcessor Processor;
				if (!Processor.Build(View, RenderData.RenderQueue,
					&RenderData, EMeshPassType::GBuffer, GBufferBatches)) return false;
			}

			// 언리얼 DBuffer 데칼: 깊이 프리패스 → DBuffer → GBuffer(BasePass)가 DBuffer를 읽어 재질에 섞는다.
			FDecalPassSetup Decal{};
			if (!PrepareDecalPasses(Graph, Renderer, Targets, Options, GBufferBatches, Width, Height, false, Decal))
				return false;
			const bool bUseDepthPrepass = Decal.bNeedsDBuffer;
			if (bUseDepthPrepass)
			{
				if (!AddDepthPrepass(Graph, Renderer, Depth, &GBufferBatches, Decal.bUseReceiverStencil)) return false;
				if (!AddDBufferPasses(Graph, Renderer, Depth, Options, Decal, Width, Height)) return false;
				GBuffer.ReadTextures = { Decal.Lit, Decal.Unlit, Decal.Normal };
				GBuffer.ReadTextureStartSlot = 14;
			}

			GBuffer.DepthAttachment = { Depth,
				bUseDepthPrepass ? ERenderGraphLoadAction::Load : ERenderGraphLoadAction::Clear, 1.0f, 0 };

			if (!Graph.AddRasterPass(std::move(GBuffer), [this, Renderer, bUseDepthPrepass]
				{
					if (!bCorePassSucceeded) return;
					if (bUseDepthPrepass)
					{
						for (FMeshDrawBatch& Batch : GBufferBatches)
							Batch.Pipeline.DepthStencilState = EDepthStencilState::PrepassReadOnly;
					}
					Renderer->UploadViewMatrix(View.ViewProjectionMatrix);
					DrawBasePassMeshes(Renderer, GBufferBatches);
					if (bUseDepthPrepass)
						UnbindForwardDecal(Renderer);
				})) return false;

			// 언리얼 RenderLights: 그림자 없는 광원은 한 번에 그리고, 그림자 광원은 광원마다
			// 마스크를 지우고 → 그 광원의 그림자를 Projection하고 → 그 광원만 가산 블렌드로 더한다. 마스크 한 장을 계속 재사용한다.
			const FRenderGraphTextureRef ShadowMask = ShadowAtlasRef && !ShadowedLights.IsEmpty()
				? Graph.CreateTexture({ Width, Height, ETextureFormat::R8G8B8A8_UNorm }, "ScreenShadowMask") : FRenderGraphTextureRef{};
			auto AddDeferredLightPass = [&](const char* Name, int32 SingleLightIndex) -> bool
				{
					FRenderGraphRasterPassDesc LightingPass{};
					LightingPass.Name = Name;
					LightingPass.ReadTextures = { BaseColor, Normal, Depth, Emissive, Specular };
					if (SingleLightIndex >= 0) LightingPass.ReadTextures.push_back(ShadowMask);   // t3
					LightingPass.ColorAttachments = { { LightingHDR, ERenderGraphLoadAction::Load } };
					return Graph.AddRasterPass(std::move(LightingPass), [this, Renderer, Width, Height, SingleLightIndex, Options]
						{
							if (!bCorePassSucceeded) return;
							if (!BindDeferredLighting(Renderer, Width, Height, SingleLightIndex, Options))
							{
								bCorePassSucceeded = false;
								return;
							}

							// 단일 광원 패스는 같은 셰이더에 가산 블렌드 상태를 쓴다.
							FPipelineState* Pipeline = FRenderResourceManager::GetPSO(SingleLightIndex >= 0 ? EPSOType::DeferredPointLighting : EPSOType::DeferredLighting);
							if (Pipeline && Pipeline->Shader && Pipeline->Shader->VertexShader && Pipeline->Shader->PixelShader)
							{
								RenderCommand::BindPipelineState(*Pipeline);
								RenderCommand::BindVertexBuffer(nullptr);
								RenderCommand::Draw(3);
							}
							else
							{
								bCorePassSucceeded = false;
							}
							RenderCommand::UnbindShaderResources(3, 4, EShaderBindFlagBits::Pixel);
						});
				};
			if (!AddDeferredLightPass("DeferredLighting", -1)) return false;

			if (ShadowMask)
			{
				for (const FShadowedLight& Light : ShadowedLights)
				{
					FRenderGraphRasterPassDesc Projection{};
					Projection.Name = "ShadowProjection";
					Projection.ReadTextures = { Depth, ShadowAtlasRef };   // t0 장면 깊이, t1 깊이 아틀라스
					Projection.ColorAttachments = { { ShadowMask, ERenderGraphLoadAction::Clear,{ 1.0f, 1.0f, 1.0f, 1.0f } } };
					if (!Graph.AddRasterPass(std::move(Projection), [this, Width, Height, LightId = Light.LightId]
						{
							if (!bCorePassSucceeded) return;
							RenderShadowProjectionsForLight(RenderData.VisibleLightInfos[LightId], 0, Width, Height);
							RenderCommand::BindConstantBuffer(0, nullptr, EShaderBindFlagBits::Pixel);
						})) return false;

					if (!AddDeferredLightPass("DeferredShadowedLight", Light.VisibleLightIndex)) return false;
				}
			}

			// Shadow Mask 뷰 모드: 그림자 광원을 최대 4개까지 채널 하나씩 그려 조명 대신 보여 준다.
			if (Options.bShowShadowMask && ShadowAtlasRef)
			{
				const FRenderGraphTextureRef ShadowMask = Graph.CreateTexture({ Width, Height, ETextureFormat::R8G8B8A8_UNorm }, "ShadowMaskVisualization");
				if (!ShadowMask) return false;

				FRenderGraphRasterPassDesc Projection{};
				Projection.Name = "ShadowProjection";
				Projection.ReadTextures = { Depth, ShadowAtlasRef };   // t0 장면 깊이, t1 깊이 아틀라스
				Projection.ColorAttachments = { { ShadowMask, ERenderGraphLoadAction::Clear,{ 1.0f, 1.0f, 1.0f, 1.0f } } };
				if (!Graph.AddRasterPass(std::move(Projection), [this, Width, Height]
					{
						RenderShadowMaskVisualizationProjections(Width, Height);
					})) return false;

				FRenderGraphRasterPassDesc Visualize{};
				Visualize.Name = "ShadowMaskVisualization";
				Visualize.ReadTextures = { ShadowMask };
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
		}
		else
		{
			FRenderGraphRasterPassDesc ClearDepth{};
			ClearDepth.Name = "ClearSceneDepth";
			ClearDepth.DepthAttachment = { Depth, ERenderGraphLoadAction::Clear, 1.0f, 0 };
			if (!Graph.AddRasterPass(std::move(ClearDepth), [] {})) return false;
		}
	}
	else // Forward Rendering
	{
		ForwardBatches.Reset();
		if (Options.bDrawPrimitives)
		{
			FMeshPassProcessor Processor;
			if (!Processor.Build(View, RenderData.RenderQueue, &RenderData,
				EMeshPassType::Forward, ForwardBatches, Options)) return false;
		}
		
		FDecalPassSetup Decal{};
		if (!PrepareDecalPasses(Graph, Renderer, Targets, Options, ForwardBatches, Width, Height, true, Decal))
			return false;
		const bool bNeedsDBuffer = Decal.bNeedsDBuffer;
		const bool bUseDecalReceiverStencil = Decal.bUseReceiverStencil;


		// 현재 업로드된 광원 정보를 기존 그리드에 전달한다.
		FForwardLightTileGrid& Tiles = RenderData.ViewLightResources.GetTiles();
		FStructuredBuffer* SceneLights = RenderData.ViewLightResources.GetSceneLights();
		FForwardLightingConstants GridLighting{};
		std::memcpy(GridLighting.LightCounts, RenderData.LightParameterCounts, sizeof(RenderData.LightParameterCounts));
		std::memcpy(GridLighting.LightOffsets, RenderData.LightParameterOffsets, sizeof(RenderData.LightParameterOffsets));

		// 픽셀 조명 경로에서만 목록을 사용한다.
		const ELightCullingMode CullingMode = !View.bIsWireframe && Options.ShadingModel != EShadingModel::Gouraud &&
			!Options.bUnlit ? Options.LightCullingMode : ELightCullingMode::Off;

		// Off도 이번 프레임의 화면 정보와 입력 광원 개수를 갱신한다.
		bool bUseDepthPrepass = false;
		const bool bGridPrepared = Options.bDrawPrimitives && bLightUploadSucceeded &&
			Tiles.PrepareBuild(View, GridLighting, SceneLights, Targets.Depth.get(), CullingMode);

		const bool bNeedsLightCulling = bGridPrepared && CullingMode != ELightCullingMode::Off && (RenderData.HasSpotLights() || RenderData.HasPointLights());
		// 그림자 마스크는 장면 깊이로 Projection하므로 프리패스가 필요하다. Gouraud는 FinalizeShadowLights가 채널을 주지 않는다.
		const bool bForwardShadows = Options.bDrawPrimitives && ShadowAtlasRef && !ShadowedLights.IsEmpty();
		if (bNeedsLightCulling || bForwardShadows || bNeedsDBuffer)
		{
			bUseDepthPrepass = true;
			if (!AddDepthPrepass(Graph, Renderer, Depth, &ForwardBatches, bUseDecalReceiverStencil)) return false;
		}

		// 그림자 광원마다 배정된 채널(R~A)에 Projection한다. 한 픽셀에서 겹치는 그림자 광원은 서로 다른 채널을 받는다.
		FRenderGraphTextureRef ForwardShadowMask{};
		if (bForwardShadows)
		{
			ForwardShadowMask = Graph.CreateTexture({ Width, Height, ETextureFormat::R8G8B8A8_UNorm }, "ScreenShadowMask");
			if (!ForwardShadowMask) return false;

			FRenderGraphRasterPassDesc Projection{};
			Projection.Name = "ShadowProjection";
			Projection.ReadTextures = { Depth, ShadowAtlasRef };   // t0 장면 깊이, t1 깊이 아틀라스
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
			// 선택한 알고리즘에 맞는 컴퓨트 패스를 등록한다.
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

		// 데칼 패스 (그릴 것이 없어도 빈 DBuffer로 지운다)
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
				if (bUseDepthPrepass)
				{
					for (FMeshDrawBatch& Batch : ForwardBatches)
					{
						Batch.Pipeline.DepthStencilState = EDepthStencilState::PrepassReadOnly;
					}
				}
				DrawBasePassMeshes(Renderer, ForwardBatches);

				UnbindForwardDecal(Renderer);
				UnbindForwardLighting(Renderer);
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
		CoreOptions.RenderingPath = ERenderingPath::Forward;
		CoreOptions.ReflectionModel = EReflectionModel::Lambert;
		CoreOptions.ShadingModel = EShadingModel::Phong;
		CoreOptions.bUnlit = false;
	}
	else if (Options.bUnlit)
	{
		// Deferred 조명 패스는 Unlit 변형이 없다. 조명이 필요 없으므로 Uber Shader의 Unlit 변형으로 바로 그린다.
		CoreOptions.RenderingPath = ERenderingPath::Forward;
	}
	if (!AddCorePasses(Graph, Renderer, Targets, CoreOptions, Skybox)) // 기본적으로 그려져야 되는 Core Pass 들
		return false;

	const FRenderGraphTextureRef SceneColor = Graph.ImportTexture(Targets.SceneColor.get(), "SceneColor");
	const FRenderGraphTextureRef Depth = Graph.ImportTexture(Targets.Depth.get(), "SceneDepth");
	if (!SceneColor || !Depth) return false;
	bHasFog = false;
	bFXAAPassSucceeded = true;

	if (!Options.bShowWorldNormal && Options.bDrawPrimitives && Options.bEnableFog && Callbacks.HeightFog)
	{
		FRenderGraphRasterPassDesc Pass = MakeViewportPass("HeightFog", SceneColor);
		Pass.ReadTextures = { Depth };
		if (!Graph.AddRasterPass(std::move(Pass), [this, DrawFog = Callbacks.HeightFog]
			{
				bHasFog = DrawFog();
			})) return false;
	}
	if (!Options.bShowWorldNormal && Options.bDrawPrimitives)
	{
		if (!Graph.AddRasterPass(MakeViewportPass("Translucent", SceneColor, Depth),
			[this, Renderer, GetFog = Callbacks.TranslucentFogConstants]
			{
				RenderTranslucent(Renderer, GetFog ? GetFog(bHasFog) : nullptr);
			})) return false;
	}
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
	Callbacks.HeightFog = [this, Renderer, DepthTarget, ColorTarget]
		{
			return RenderHeightFog(Renderer, DepthTarget, ColorTarget);
		};
	Callbacks.TranslucentFogConstants = [Renderer](bool bHasFog)
		{
			FHeightFogRenderer* Fog = Renderer->GetHeightFog();
			return Fog ? Fog->GetTranslucentFogConstants(bHasFog) : nullptr;
		};
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
	if (!Renderer)
	{
		return;
	}

	RenderCommand::BindConstantBuffer(3, FogConstants, EShaderBindFlagBits::Pixel);
	PrepareDrawData(Renderer, RenderData);
	TArray<FMeshDrawBatch> Batches;
	FMeshPassProcessor Processor;
	if (Processor.Build(View, RenderData.RenderQueue, &RenderData,
		EMeshPassType::Translucent, Batches))
	{
		Renderer->UploadViewMatrix(View.ViewProjectionMatrix);
		Renderer->DrawMeshes(Batches);
	}
	RenderCommand::BindConstantBuffer(3, nullptr, EShaderBindFlagBits::Pixel);
}

void FSceneRenderer::GatherRenderPackets(FScene& Scene, FSceneRenderData& Data,
	const FViewContext& View, const FFrustumPlanes* Frustum, FRenderer* Renderer) //여기서 꼭 renderer를 매개변수로 받아야 할까?
{
	Data.ResetFrame();
	FRenderQueue& RenderQueue = Data.RenderQueue;
	FRenderStats& RenderStats = Data.RenderStats;
	auto& VisibleProxies = Data.VisibleProxies;
	auto& LODInputs = Data.LODInputs;
	auto& SelectedLODs = Data.SelectedLODs;
	auto& GatherChunks = Data.GatherChunks;
	using FGatherChunk = FSceneRenderData::FGatherChunk;

	{
		SCOPE_CYCLE_COUNTER(STAT_FrustumCull);
		// 표시 상태는 프록시 캐시로 판정해 오클루전과 드로우 수집 전에 제외한다.
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
		if (USkeletalMeshComponent* Component = Cast<USkeletalMeshComponent>(Proxy->GetComponent()))
			Component->MarkVisibleForAnimationTick();
	}

	RenderStats.TotalPrimitives = Scene.PrimitiveSceneProxies.Num();

	// GPU 오클루전: 프러스텀을 통과한 물체를 GPU에서 가림 판정하고 결과를 이번 프레임에 받아 온다.
	// Cull이 켜져 있으면 가려진 물체를 목록에서 빼서 이후 Gather·정렬·드로우를 모두 건너뛴다.
	// 꺼져 있으면(검증 모드) 목록은 그대로 두고 패킷에 판정만 표시한다.
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

	constexpr uint32 ExtraSlots = 4096;
	const uint32 VisibleCount = VisibleProxies.Num();
	const uint32 MaxSlots = VisibleCount + ExtraSlots;
	if (Renderer) Data.ObjectConstants.SetNum(MaxSlots);
	const bool bPrepareObjectSlots = Renderer != nullptr;
	uint32 NextExtraSlot = VisibleCount;

	// 조각 수 = 스레드 수 × 4. 잘게 나눠야 먼저 끝난 스레드가 남은 조각을 가져가서 부하가 고르게 된다.
	const uint32 NumThreads = Tasks::FTaskScheduler::Get().GetNumWorkers() + 1;
	const uint32 ChunkCount = FMath::Clamp(VisibleCount, 1u, NumThreads * 4);

	if (GatherChunks.Num() < ChunkCount)
		GatherChunks.SetNum(ChunkCount);     // 늘릴 때만. 줄이지 않아야 배열 용량이 계속 재사용된다.

	// Renderer가 있으면 불투명 스태틱 메시는 패킷 대신 조각별 묶음에 작은 항목으로 넣는다.
	// 동일 바인딩의 개별 드로우를 묶어 패킷 복사와 항목별 정렬을 줄인다.
	const bool bStaticGroups = Renderer != nullptr;

	{
		SCOPE_CYCLE_COUNTER(STAT_GatherElements);

		LODInputs.Reset();
		LODInputs.Reserve(VisibleProxies.Num());
		for (const FPrimitiveSceneProxy* Proxy : VisibleProxies)
			LODInputs.Add({ Proxy->GetLODSphere(), Proxy->GetRenderState() });
		SelectLODs(LODInputs, View, SelectedLODs);
		Tasks::ParallelForChunks(VisibleCount, ChunkCount, [&](uint32 Begin, uint32 End, uint32 ChunkIndex)
			{
				FGatherChunk& Out = GatherChunks[ChunkIndex];      // 이 조각 전용. 다른 스레드는 절대 안 건드림
				// (머티리얼, 메시, LOD) 묶음 찾기. 조합이 몇 개뿐이라 선형 탐색이면 충분하고, 바로 전 묶음을 먼저 본다.
				const auto FindGroup = [&Out](UMaterial* Material, UStaticMesh* Mesh, uint8 LOD) -> FStaticDrawGroup&
					{
						if (Out.LastGroup < Out.Groups.size())
						{
							FStaticDrawGroup& Last = Out.Groups[Out.LastGroup];
							if (Last.Material == Material && Last.Mesh == Mesh && Last.LODIndex == LOD)
								return Last;
						}
						for (uint32 g = 0; g < Out.Groups.size(); ++g)
						{
							FStaticDrawGroup& Group = Out.Groups[g];
							if (Group.Material == Material && Group.Mesh == Mesh && Group.LODIndex == LOD)
							{
								Out.LastGroup = g;
								return Group;
							}
						}
						Out.LastGroup = static_cast<uint32>(Out.Groups.size());
						FStaticDrawGroup& Group = Out.Groups.emplace_back();
						Group.Material = Material;
						Group.Mesh = Mesh;
						Group.LODIndex = LOD;
						return Group;
					};

				for (uint32 VisibleIndex = Begin; VisibleIndex < End; ++VisibleIndex)
				{
					FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];   // 읽기만
					if (!Proxy->IsVisible()) continue;

					UStaticMesh* Mesh = Proxy->GetMesh();
					if (!Mesh)
					{
						Out.SlowPathIndices.Add(VisibleIndex);     // 컴포넌트를 건드리는 경로는 메인이 나중에
						continue;
					}

					const uint32 LOD = SelectedLODs[VisibleIndex];
					const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);
					++Out.LODCounts[LOD];                          // RenderStats 대신 조각 전용 통계

					uint32 Slot = InvalidObjectSlot;
					if (bPrepareObjectSlots)
					{
						// 칸 VisibleIndex는 이 반복만 쓴다 → 스레드끼리 겹치지 않음
						const FPerObjectConstants Constants =
							MakePerObjectConstants(Proxy->GetLocalToWorld(), 
								Proxy->bReceiveShadow, Proxy->bReceiveDecal);

						Data.ObjectConstants[VisibleIndex] = Constants;

						Slot = VisibleIndex;
					}

					const bool bOccludedByGpu = OccludedMask && OccludedMask[VisibleIndex];
					for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
					{
						const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
						Out.LODTriangles[LOD] += Section.IndexCount / 3;

						if (bStaticGroups && Section.Material && Section.Material->PSOType == EPSOType::StaticMesh_Opaque)
						{
							FStaticDrawGroup& Group = FindGroup(Section.Material, Mesh, static_cast<uint8>(LOD));
							Group.Items.push_back({ Proxy, Slot, Section.StartIndex, Section.IndexCount, bOccludedByGpu ? 1u : 0u });
							++Out.StaticDrawCount;
							continue;
						}

						FRenderPacket& Packet = Out.Packets.AddDefaulted_GetRef();   // 조각 전용 배열에 추가
						Packet.Proxy = Proxy;
						Packet.Mesh = Mesh;
						Packet.Material = Section.Material;
						Packet.StartIndex = Section.StartIndex;
						Packet.IndexCount = Section.IndexCount;
						Packet.LODIndex = static_cast<uint8>(LOD);
						Packet.Slot = Slot;
						Packet.bOccludedByGpu = bOccludedByGpu;
					}
				}
			});

		// 이 뷰의 스태틱 드로우 수를 세고, 실제 렌더링할 때에만 해당 묶음을 바인딩한다.
		uint32 StaticDraws = 0;
		if (Renderer)
		{
			for (uint32 c = 0; c < ChunkCount; ++c)
			{
				StaticDraws += GatherChunks[c].StaticDrawCount;
			}
		}

		uint32 TotalPackets = 0;
		for (uint32 c = 0; c < ChunkCount; ++c)
			TotalPackets += GatherChunks[c].Packets.Num();

		RenderQueue.Reserve(TotalPackets + 256);             // 느린 경로 몫 약간 여유
		for (uint32 c = 0; c < ChunkCount; ++c)
			if (GatherChunks[c].Packets.Num() > 0)
				RenderQueue.Append(GatherChunks[c].Packets);  // 조각 순서대로 이어 붙이기

		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			for (uint32 VisibleIndex : GatherChunks[c].SlowPathIndices)
			{
				FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
				UPrimitiveComponent* Primitive = Proxy->GetComponent();
				if (!Primitive || !Primitive->IsVisible())
					continue;

				const uint32 FirstNew = RenderQueue.Num();

				// 프록시 캐시가 없는 스태틱 메시는 기존처럼 LOD를 골라 제출하고, 그 외는 컴포넌트에 맡긴다.
				UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Primitive);
				if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
				{
					const uint32 LOD = SelectedLODs[VisibleIndex];
					StaticMeshComponent->SubmitToRenderQueue(RenderQueue, LOD);
				}
				else
				{
					Primitive->SubmitToRenderQueue(RenderQueue, View);
				}

				// continue 없이 항상 여기까지 와서 새 패킷에 여유 칸을 배정한다.
				for (uint32 p = FirstNew; p < RenderQueue.Num(); ++p)
				{
					if (!bPrepareObjectSlots || NextExtraSlot >= MaxSlots) break;
					FRenderPacket& Packet = RenderQueue[p];
					const FMatrix& Model = Packet.Proxy ? Packet.Proxy->GetLocalToWorld() : Packet.Model ? *Packet.Model : FMatrix::Identity;
					Data.ObjectConstants[NextExtraSlot] = 
						MakePerObjectConstants(
							Model, 
							!Packet.Proxy || Packet.Proxy->bReceiveShadow, 
							!Packet.Proxy || Packet.Proxy->bReceiveDecal);
					Packet.Slot = NextExtraSlot++;
				}
			}
		}

		for (uint32 c = 0; c < ChunkCount; ++c)
			for (uint32 L = 0; L < 4; ++L)
			{
				RenderStats.LODCounts[L] += GatherChunks[c].LODCounts[L];
				RenderStats.LODTriangles[L] += GatherChunks[c].LODTriangles[L];
			}

		for (const FParticleSceneProxy* Proxy : Scene.GetParticleProxies())
		{
			if (!Proxy->HasParticles() || !Proxy->IsShown(View.bGameView)) continue;
			if (Frustum)
			{
				uint32 PlaneMask = FrustumAllPlanesMask;
				if (ClassifyBoxInFrustum(Proxy->GetBounds(), *Frustum, PlaneMask) == EFrustumTest::Outside) continue;
			}
			Proxy->SubmitToRenderQueue(RenderQueue, View);
		}

		if (bPrepareObjectSlots) Data.ObjectConstants.SetNum(NextExtraSlot);
		RenderStats.DrawCalls = RenderQueue.Num() + StaticDraws;
		for (uint64 T : RenderStats.LODTriangles) RenderStats.Triangles += T;
	}
}


void FSceneRenderer::PrepareDrawData(FRenderer* Renderer, const FSceneRenderData& Data)
{
	if (!Renderer) return;
	if (uint8* Destination = Renderer->BeginObjectConstants(static_cast<uint32>(Data.ObjectConstants.Num())))
	{
		for (int32 Index = 0; Index < Data.ObjectConstants.Num(); ++Index)
			std::memcpy(Destination + static_cast<size_t>(Index) * ObjectSlotBytes,
				&Data.ObjectConstants[Index], sizeof(FPerObjectConstants));
		Renderer->EndObjectConstants();
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
		DecalReceiverBatches.IsEmpty())
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
				Proxy->GetScene() != &Scene ||
				!Proxy->GetComponent())
			{
				return false;
			}

			const int32 Index = Proxy->PackedIndex;

			return
				Index >= 0 &&
				Index < Scene.PrimitiveSceneProxies.Num() &&
				Scene.PrimitiveSceneProxies[Index] == Proxy;
		};

	// 뷰당 한 번: Proxy -> 현재 뷰의 수신 Item 목록
	for (uint32 BatchIndex = 0; BatchIndex < DecalReceiverBatches.Num(); ++BatchIndex)
	{
		const FMeshDrawBatch& Batch = DecalReceiverBatches[BatchIndex];

		for (uint32 ItemIndex = 0; ItemIndex < Batch.Items.Num(); ++ItemIndex)
		{
			const FMeshDrawBatch::FItem& Item = Batch.Items[ItemIndex];

			if (!Item.bReceiveDecal)
				continue;

			const FDecalReceiverItemRef Ref{
				BatchIndex, ItemIndex
			};

			if (IsIndexedProxy(Item.Proxy))
			{
				ReceiverLookup[Item.Proxy].Add(Ref);
			}
			else
			{
				FallbackItems.Add(Ref);
			}
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
