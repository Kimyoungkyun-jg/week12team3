#include "EnginePCH.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include "Render/GpuProfiler.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_map>

#include "Render/SkyboxRenderer.h"
#include "Render/PostProcess/HeightFogRenderer.h"
#include "Render/PostProcess/FXAARenderer.h"
#include "Render/PostProcess/ToneMapData.h"
#include "Render/PostProcess/CameraOverlayRenderer.h"
#include "Render/Decal/ProjectedDecalRenderer.h"
#include "Text/TextRenderer.h"

namespace
{
	constexpr uint32 MinInstanceIdCapacity = 256;
	constexpr uint32 CommandSlotBufferBytes = 512;

	bool HasResourceSlot(const TArray<FMeshDrawShaderResource>& Resources, uint32 Slot, EShaderBindFlagBits Stages)
	{
		for (const FMeshDrawShaderResource& Entry : Resources)
		{
			if (Entry.Slot == Slot && Entry.Stages == Stages)
				return true;
		}
		return false;
	}

	bool HasSameBoundResource(const TArray<FMeshDrawShaderResource>& Bound, const FMeshDrawShaderResource& Next)
	{
		for (const FMeshDrawShaderResource& Entry : Bound)
		{
			if (Entry.Slot == Next.Slot && Entry.Stages == Next.Stages &&
				Entry.Texture == Next.Texture && Entry.StructuredBuffer == Next.StructuredBuffer)
				return true;
		}
		return false;
	}

	bool HasStructuredBuffer(const TArray<FMeshDrawShaderResource>& Resources)
	{
		for (const FMeshDrawShaderResource& Entry : Resources)
		{
			if (Entry.StructuredBuffer != nullptr)
				return true;
		}
		return false;
	}

	bool HasSameConstantBufferBytes(const FMeshDrawCommand* Prev, const FMeshDrawCommand& Next)
	{
		if (!Prev || Prev->ConstantBuffers.Num() != Next.ConstantBuffers.Num())
			return false;
		for (int32 i = 0; i < Next.ConstantBuffers.Num(); ++i)
		{
			const FMeshDrawConstantBuffer& A = Prev->ConstantBuffers[i];
			const FMeshDrawConstantBuffer& B = Next.ConstantBuffers[i];
			if (A.Slot != B.Slot || A.Stages != B.Stages || A.DataSize != B.DataSize)
				return false;
			if (A.DataSize > 0 && std::memcmp(A.Data.data(), B.Data.data(), A.DataSize) != 0)
				return false;
		}
		return true;
	}
}

FRenderer::FRenderer() = default;
FRenderer::~FRenderer() = default;

bool FRenderer::Init()
{
	for (uint32 i = 0; i < MaxCommandSlotCBs; ++i)
	{
		CommandSlotCBs[i] = RenderCommand::CreateConstantBuffer(CommandSlotBufferBytes);
		if (!CommandSlotCBs[i])
			return false;
	}
	ViewCB = RenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	DrawBatchCB = RenderCommand::CreateConstantBuffer(sizeof(FDrawBatchConstants));
	if (!ViewCB || !DrawBatchCB)
		return false;
	GPUOcclusion.Init();
	return true;
}

void FRenderer::UploadViewConstants(const FMatrix& ViewProjection, const FMatrix& ViewMatrix,
	const FMatrix& ProjectionMatrix, float TotalTime)
{
	FViewConstants Constants{};
	Constants.VP = ViewProjection;
	Constants.View = ViewMatrix;
	Constants.Projection = ProjectionMatrix;
	Constants.ViewTimeParams = FVector4(TotalTime, 0.0f, 0.0f, 0.0f);
	RenderCommand::UpdateBufferData(ViewCB.get(), &Constants, sizeof(Constants));
}

void FRenderer::EnsureInstanceIdCapacity(uint32 Count)
{
	if (Count <= InstanceIdCapacity)
		return;
	uint32 NewCap = std::max(InstanceIdCapacity * 2u, MinInstanceIdCapacity);
	while (NewCap < Count)
		NewCap *= 2u;
	InstanceIdBuffer = RenderCommand::CreateDynamicStructuredBuffer(NewCap, sizeof(uint32));
	InstanceIdCapacity = InstanceIdBuffer ? NewCap : 0;
}

void FRenderer::UnbindUnusedResources(const TArray<FMeshDrawShaderResource>& NextResources,
	TArray<FMeshDrawShaderResource>& InOutBoundResources)
{
	for (const FMeshDrawShaderResource& Prev : InOutBoundResources)
	{
		if (!HasResourceSlot(NextResources, Prev.Slot, Prev.Stages))
			RenderCommand::UnbindShaderResources(Prev.Slot, 1, Prev.Stages);
	}
}

void FRenderer::SubmitVisibleCommands(std::span<const FVisibleMeshDrawCommand> Commands,
	const FStructuredBuffer* GPUSceneBuffer, uint32* OutDrawCalls,
	const std::shared_ptr<FGpuTimingHistory>& SkeletalTiming, ESkinningMode MeasuredMode)
{
	if (Commands.empty())
		return;

	ScratchInstanceIds.Reset();
	ScratchDrawRuns.Reset();
	ScratchDrawRuns.Reserve(static_cast<int32>(Commands.size()));

	const uint32 TotalCount = static_cast<uint32>(Commands.size());
	for (uint32 i = 0; i < TotalCount; ++i)
	{
		const FVisibleMeshDrawCommand& Vis = Commands[i];
		if (!Vis.Command)
			continue;

		const FMeshDrawCommand& Cmd = *Vis.Command;
		if (Cmd.bUsesGPUScene && Cmd.InstanceCount == 0 && Vis.PrimitiveId != InvalidPrimitiveId)
		{
			const uint32 StartInstance = static_cast<uint32>(ScratchInstanceIds.Num());
			ScratchInstanceIds.Add(Vis.PrimitiveId);
			uint32 RunCount = 1;
			while (i + RunCount < TotalCount && AreVisibleCommandsInstanceCompatible(Vis, Commands[i + RunCount])) // 바로 옆에 나란히 붙어 있는 완전히 같은 오브젝트들을 몇 개까지 하나의 DrawIndexedInstanced로 합칠 수 있는지
			{
				ScratchInstanceIds.Add(Commands[i + RunCount].PrimitiveId);
				++RunCount;
			}
			ScratchDrawRuns.Add({ &Vis, StartInstance, RunCount, true });
			i += RunCount - 1;
		}
		else
		{
			ScratchDrawRuns.Add({ &Vis, Cmd.StartInstanceLocation, Cmd.InstanceCount, false });
		}
	}

	if (ScratchDrawRuns.IsEmpty())
		return;

	bool bBoundGPUScene = false;
	if (!ScratchInstanceIds.IsEmpty())
	{
		EnsureInstanceIdCapacity(static_cast<uint32>(ScratchInstanceIds.Num()));
		if (InstanceIdBuffer)
		{
			if (void* Mapped = RenderCommand::MapWriteDiscard(InstanceIdBuffer.get()))
			{
				std::memcpy(Mapped, ScratchInstanceIds.GetData(), ScratchInstanceIds.Num() * sizeof(uint32));
				RenderCommand::Unmap(InstanceIdBuffer.get());
			}
			if (GPUSceneBuffer)
				RenderCommand::BindShaderResource(17, GPUSceneBuffer, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
			RenderCommand::BindShaderResource(18, InstanceIdBuffer.get(), EShaderBindFlagBits::Vertex);
			bBoundGPUScene = true;
		}
	}

	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	using EScopeKind = FGpuProfiler::EScopeKind;
	FGpuProfiler* Profiler = SkeletalTiming ? &FGpuProfiler::Get() : nullptr;
	EScopeKind ActiveKind = EScopeKind::Pass;
	int32 TimingHandle = -1;

	FShaderProgram* PrevShader = nullptr;
	EPrimitiveTopology PrevTopology = static_cast<EPrimitiveTopology>(255);
	ERasterizerState PrevRaster = static_cast<ERasterizerState>(255);
	EBlendState PrevBlend = static_cast<EBlendState>(255);
	EDepthStencilState PrevDepth = static_cast<EDepthStencilState>(255);
	uint32 PrevStencil = 0xFFFFFFFFu;
	FVertexBuffer* PrevVB = nullptr;
	FIndexBuffer* PrevIB = nullptr;
	ESamplerState PrevSampler = static_cast<ESamplerState>(255);
	EShaderBindFlagBits PrevSamplerStages = EShaderBindFlagBits::None;
	const FMeshDrawCommand* PrevCBCommand = nullptr;
	TArray<FMeshDrawShaderResource> BoundResources;

	for (const FDrawRun& Run : ScratchDrawRuns)
	{
		const FVisibleMeshDrawCommand& Vis = *Run.Visible;
		const FMeshDrawCommand& Cmd = *Vis.Command;

		if (Profiler)
		{
			EScopeKind Kind = EScopeKind::Pass;
			const bool bGpuSkinned = Cmd.bComputeSkinned || (Cmd.bSkeletalMesh && HasStructuredBuffer(Cmd.ShaderResources));
			if (Cmd.bSkeletalMesh && bGpuSkinned == (MeasuredMode != ESkinningMode::CPU))
				Kind = bGpuSkinned ? EScopeKind::GpuSkinnedDraw : EScopeKind::SkeletalDraw;

			if (Kind != ActiveKind)
			{
				Profiler->EndScope(TimingHandle);
				TimingHandle = Kind == EScopeKind::Pass ? -1 : Profiler->BeginScope(SkeletalTiming, Kind);
				ActiveKind = Kind;
			}
		}

		FShaderProgram* Shader = Vis.ShaderOverride ? Vis.ShaderOverride : Cmd.Pipeline.Shader;
		if (Shader != PrevShader)
		{
			RenderCommand::BindShaderProgram(Shader);
			PrevShader = Shader;
		}
		if (Cmd.Pipeline.Topology != PrevTopology)
		{
			RenderCommand::SetPrimitiveTopology(Cmd.Pipeline.Topology);
			PrevTopology = Cmd.Pipeline.Topology;
		}
		const ERasterizerState Raster = (Vis.bForceWireframe || Cmd.bForceWireframe)
			? ERasterizerState::Wireframe : Cmd.Pipeline.RasterizerState;
		if (Raster != PrevRaster)
		{
			RenderCommand::SetRasterizerState(Raster);
			PrevRaster = Raster;
		}
		if (Cmd.Pipeline.BlendState != PrevBlend)
		{
			RenderCommand::SetBlendState(Cmd.Pipeline.BlendState);
			PrevBlend = Cmd.Pipeline.BlendState;
		}
		const EDepthStencilState Depth = Vis.bHasDepthStencilOverride
			? Vis.DepthStencilOverride : Cmd.Pipeline.DepthStencilState;
		const uint32 Stencil = (Vis.bOverrideStencilRef || Cmd.bOverrideStencilRef)
			? (Vis.bOverrideStencilRef ? Vis.StencilRef : Cmd.StencilRef) : Cmd.Pipeline.StencilRef;
		if (Depth != PrevDepth || Stencil != PrevStencil)
		{
			RenderCommand::SetDepthStencilState(Depth, Stencil);
			PrevDepth = Depth;
			PrevStencil = Stencil;
		}

		if (Cmd.VertexBuffer != PrevVB)
		{
			RenderCommand::BindVertexBuffer(Cmd.VertexBuffer);
			PrevVB = Cmd.VertexBuffer;
		}
		if (Cmd.IndexBuffer != PrevIB)
		{
			RenderCommand::BindIndexBuffer(Cmd.IndexBuffer);
			PrevIB = Cmd.IndexBuffer;
		}
		if (Cmd.Sampler != PrevSampler || Cmd.SamplerStages != PrevSamplerStages)
		{
			RenderCommand::BindSamplerState(0, Cmd.Sampler, Cmd.SamplerStages);
			PrevSampler = Cmd.Sampler;
			PrevSamplerStages = Cmd.SamplerStages;
		}

		UnbindUnusedResources(Cmd.ShaderResources, BoundResources);
		for (const FMeshDrawShaderResource& Res : Cmd.ShaderResources)
		{
			if (HasSameBoundResource(BoundResources, Res))
				continue;
			if (Res.StructuredBuffer)
				RenderCommand::BindShaderResource(Res.Slot, Res.StructuredBuffer, Res.Stages);
			else
				RenderCommand::BindShaderResource(Res.Slot, Res.Texture, Res.Stages);
		}
		BoundResources = Cmd.ShaderResources;

		if (!HasSameConstantBufferBytes(PrevCBCommand, Cmd))
		{
			for (const FMeshDrawConstantBuffer& CB : Cmd.ConstantBuffers)
			{
				if (CB.DataSize > 0 && CB.Slot < MaxCommandSlotCBs && CommandSlotCBs[CB.Slot])
				{
					FConstantBuffer* TargetCB = CommandSlotCBs[CB.Slot].get();
					RenderCommand::UpdateBufferData(TargetCB, CB.Data.data(), CB.DataSize);
					RenderCommand::BindConstantBuffer(CB.Slot, TargetCB, CB.Stages);
				}
			}
			PrevCBCommand = &Cmd;
		}

		if (Run.bGPUSceneInstanced)
		{
			FDrawBatchConstants BatchConstants{};
			BatchConstants.StartInstance = Run.StartInstance;
			RenderCommand::UpdateBufferData(DrawBatchCB.get(), &BatchConstants, sizeof(BatchConstants));
			RenderCommand::BindConstantBuffer(5, DrawBatchCB.get(), EShaderBindFlagBits::Vertex);
			RenderCommand::DrawIndexedInstanced(Cmd.IndexCount, Run.InstanceCount,
				Cmd.FirstIndex, Cmd.BaseVertexLocation, 0);
		}
		else if (Run.InstanceCount > 0)
		{
			RenderCommand::DrawIndexedInstanced(Cmd.IndexCount, Run.InstanceCount,
				Cmd.FirstIndex, Cmd.BaseVertexLocation, Run.StartInstance);
		}
		else
		{
			RenderCommand::DrawIndexed(Cmd.IndexCount, Cmd.FirstIndex, Cmd.BaseVertexLocation);
		}
	}

	if (OutDrawCalls)
		*OutDrawCalls += static_cast<uint32>(ScratchDrawRuns.Num());

	const TArray<FMeshDrawShaderResource> EmptyResources{};
	UnbindUnusedResources(EmptyResources, BoundResources);
	RenderCommand::BindConstantBuffer(4, nullptr, EShaderBindFlagBits::Pixel);
	if (bBoundGPUScene)
	{
		RenderCommand::UnbindShaderResources(17, 1, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
		RenderCommand::UnbindShaderResources(18, 1, EShaderBindFlagBits::Vertex);
		RenderCommand::BindConstantBuffer(5, nullptr, EShaderBindFlagBits::Vertex);
	}

	if (Profiler)
		Profiler->EndScope(TimingHandle);
}

void FRenderer::InitScenePasses(const FString& SkyPanoramaPath)
{
	if (!SkyPanoramaPath.empty())
	{
		Skybox = MakeUnique<FSkyboxRenderer>();
		if (!Skybox->Init(SkyPanoramaPath))
		{
			HTR_LOG(Warning, "Skybox panorama not loaded: {}", SkyPanoramaPath);
			Skybox.reset();
		}
	}
	HeightFog = MakeUnique<FHeightFogRenderer>();
	HeightFog->Init(this);
	FXAA = MakeUnique<FFXAARenderer>();
	FXAA->Init(this);
	CameraOverlay = MakeUnique<FCameraOverlayRenderer>();
	CameraOverlay->Init(this);
	ToneMapConstants = RenderCommand::CreateConstantBuffer(sizeof(FToneMapData));
	SceneText = MakeUnique<FTextRenderer>();
	SceneText->Init();
	ProjectedDecal = MakeUnique<FProjectedDecalRenderer>();

	if (!ProjectedDecal->Init(this))
	{
		HTR_LOG(Error, "Projected decal renderer init failed");
		ProjectedDecal.reset();
	}
}

FOcclusionMeasureResult FRenderer::MeasureDrawVisibility(std::span<const FVisibleMeshDrawCommand> Commands,
	const FStructuredBuffer* GPUSceneBuffer)
{
	FOcclusionMeasureResult Result;
	const uint32 Count = static_cast<uint32>(Commands.size());
	if (!Count) return Result;
	ID3D11Device* Device = FRenderDevice::Get()->GetDevice();
	ID3D11DeviceContext* Context = FRenderDevice::Get()->GetContext();
	if (!Device || !Context) return Result;
	const auto StartTime = std::chrono::high_resolution_clock::now();
	if (!DepthLessEqualReadOnly)
	{
		D3D11_DEPTH_STENCIL_DESC Desc{};
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		if (FAILED(Device->CreateDepthStencilState(&Desc, DepthLessEqualReadOnly.GetAddressOf()))) return Result;
	}
	while (OcclusionQueries.Num() < Count)
	{
		const D3D11_QUERY_DESC Desc{ D3D11_QUERY_OCCLUSION, 0 };
		ComPtr<ID3D11Query> Query;
		if (FAILED(Device->CreateQuery(&Desc, Query.GetAddressOf()))) return Result;
		OcclusionQueries.Add(std::move(Query));
	}

	ScratchInstanceIds.Reset();
	ScratchInstanceIds.Reserve(Count);
	for (const FVisibleMeshDrawCommand& Vis : Commands)
		ScratchInstanceIds.Add(Vis.PrimitiveId);
	EnsureInstanceIdCapacity(Count);
	if (InstanceIdBuffer)
	{
		if (void* Mapped = RenderCommand::MapWriteDiscard(InstanceIdBuffer.get()))
		{
			std::memcpy(Mapped, ScratchInstanceIds.GetData(), Count * sizeof(uint32));
			RenderCommand::Unmap(InstanceIdBuffer.get());
		}
		if (GPUSceneBuffer)
			RenderCommand::BindShaderResource(17, GPUSceneBuffer, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
		RenderCommand::BindShaderResource(18, InstanceIdBuffer.get(), EShaderBindFlagBits::Vertex);
	}
	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	TArray<FMeshDrawShaderResource> BoundResources;
	uint32 QueryIndex = 0;
	for (uint32 i = 0; i < Count; ++i)
	{
		const FVisibleMeshDrawCommand& Vis = Commands[i];
		if (!Vis.Command)
			continue;
		const FMeshDrawCommand& Cmd = *Vis.Command;
		FPipelineState Pipe = Cmd.Pipeline;
		if (Vis.ShaderOverride)
			Pipe.Shader = Vis.ShaderOverride;
		RenderCommand::BindPipelineState(Pipe);
		RenderCommand::BindVertexBuffer(Cmd.VertexBuffer);
		RenderCommand::BindIndexBuffer(Cmd.IndexBuffer);
		UnbindUnusedResources(Cmd.ShaderResources, BoundResources);
		for (const FMeshDrawShaderResource& Res : Cmd.ShaderResources)
		{
			if (Res.StructuredBuffer)
				RenderCommand::BindShaderResource(Res.Slot, Res.StructuredBuffer, Res.Stages);
			else
				RenderCommand::BindShaderResource(Res.Slot, Res.Texture, Res.Stages);
		}
		BoundResources = Cmd.ShaderResources;
		FDrawBatchConstants Batch{ i, {} };
		RenderCommand::UpdateBufferData(DrawBatchCB.get(), &Batch, sizeof(Batch));
		RenderCommand::BindConstantBuffer(5, DrawBatchCB.get(), EShaderBindFlagBits::Vertex);
		RenderCommand::SetBlendState(EBlendState::NoColorWrite);
		Context->OMSetDepthStencilState(DepthLessEqualReadOnly.Get(), 0);
		ID3D11Query* Query = OcclusionQueries[QueryIndex++].Get();
		Context->Begin(Query);
		RenderCommand::DrawIndexedInstanced(Cmd.IndexCount, 1, Cmd.FirstIndex, Cmd.BaseVertexLocation, 0);
		Context->End(Query);
	}
	std::unordered_map<const void*, bool> ObjectVisible;
	ObjectVisible.reserve(Count);
	QueryIndex = 0;
	for (const FVisibleMeshDrawCommand& Vis : Commands)
	{
		if (!Vis.Command)
			continue;
		const FMeshDrawCommand& Cmd = *Vis.Command;
		UINT64 Samples = 0;
		while (Context->GetData(OcclusionQueries[QueryIndex++].Get(), &Samples, sizeof(Samples), 0) == S_FALSE) {}
		const bool bVisible = Samples > 0;
		++Result.TotalDraws;
		Result.TotalTriangles += Cmd.IndexCount / 3;
		if (bVisible)
		{
			++Result.VisibleDraws;
			Result.VisibleTriangles += Cmd.IndexCount / 3;
		}
		if (Vis.bOccludedByGpu)
		{
			++Result.GPUOccludedDraws;
			if (bVisible) ++Result.FalseCulls;
		}
		bool& bObjectVisible = ObjectVisible[Cmd.ObjectKey];
		bObjectVisible = bObjectVisible || bVisible;
	}
	Result.TotalObjects = static_cast<uint32>(ObjectVisible.size());
	for (const auto& [Key, bVisible] : ObjectVisible)
		Result.VisibleObjects += bVisible ? 1 : 0;
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
	const TArray<FMeshDrawShaderResource> EmptyResources{};
	UnbindUnusedResources(EmptyResources, BoundResources);
	RenderCommand::BindConstantBuffer(4, nullptr, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResources(17, 1, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResources(18, 1, EShaderBindFlagBits::Vertex);
	RenderCommand::BindConstantBuffer(5, nullptr, EShaderBindFlagBits::Vertex);
	Result.ElapsedMs = std::chrono::duration<double, std::milli>(
		std::chrono::high_resolution_clock::now() - StartTime).count();
	Result.bValid = true;
	return Result;
}
