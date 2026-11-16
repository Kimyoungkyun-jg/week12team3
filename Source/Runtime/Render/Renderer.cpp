#include "EnginePCH.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include "Render/GpuProfiler.h"
#include <algorithm>
#include <chrono>
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
	constexpr uint32 PerObjectSlotConstants = 16;
	constexpr uint32 MinPerObjectSlots = 1024;
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

	bool HasStructuredBuffer(const TArray<FMeshDrawShaderResource>& Resources)
	{
		for (const FMeshDrawShaderResource& Entry : Resources)
		{
			if (Entry.StructuredBuffer != nullptr)
				return true;
		}
		return false;
	}
}

FRenderer::FRenderer() = default;
FRenderer::~FRenderer() = default;

bool FRenderer::Init()
{
	bUsePerObjectSlots = RenderCommand::SupportsConstantBufferOffsets();
	for (uint32 i = 0; i < MaxCommandSlotCBs; ++i)
	{
		CommandSlotCBs[i] = RenderCommand::CreateConstantBuffer(CommandSlotBufferBytes);
		if (!CommandSlotCBs[i])
			return false;
	}
	ViewCB = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));
	if (!ViewCB)
		return false;
	GPUOcclusion.Init();
	return true;
}

void FRenderer::UploadViewMatrix(const FMatrix& ViewProjection)
{
	RenderCommand::UpdateBufferData(ViewCB.get(), &ViewProjection, sizeof(ViewProjection));
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

void FRenderer::BindCommand(const FMeshDrawCommand& Command, TArray<FMeshDrawShaderResource>& InOutBoundResources)
{
	RenderCommand::BindPipelineState(Command.Pipeline);
	if (Command.bForceWireframe)
		RenderCommand::SetRasterizerState(ERasterizerState::Wireframe);
	if (Command.bOverrideStencilRef)
		RenderCommand::SetDepthStencilState(Command.Pipeline.DepthStencilState, Command.StencilRef);

	RenderCommand::BindVertexBuffer(Command.VertexBuffer);
	RenderCommand::BindIndexBuffer(Command.IndexBuffer);
	RenderCommand::BindSamplerState(0, Command.Sampler, Command.SamplerStages);

	UnbindUnusedResources(Command.ShaderResources, InOutBoundResources);
	for (const FMeshDrawShaderResource& Res : Command.ShaderResources)
	{
		if (Res.StructuredBuffer)
			RenderCommand::BindShaderResource(Res.Slot, Res.StructuredBuffer, Res.Stages);
		else
			RenderCommand::BindShaderResource(Res.Slot, Res.Texture, Res.Stages);
	}
	InOutBoundResources = Command.ShaderResources;

	for (const FMeshDrawConstantBuffer& CB : Command.ConstantBuffers)
	{
		if (CB.bBindViewMatrix)
		{
			RenderCommand::BindConstantBuffer(CB.Slot, ViewCB.get(), CB.Stages);
		}
		else if (Command.bUseObjectSlots && bUsePerObjectSlots && PerObjectSlotCB && CB.ObjectSlot != InvalidObjectSlot)
		{
			RenderCommand::BindConstantBufferRange(CB.Slot, PerObjectSlotCB.get(),
				CB.ObjectSlot * PerObjectSlotConstants, PerObjectSlotConstants, CB.Stages);
		}
		else if (CB.DataSize > 0 && CB.Slot < MaxCommandSlotCBs && CommandSlotCBs[CB.Slot])
		{
			FConstantBuffer* TargetCB = CommandSlotCBs[CB.Slot].get();
			RenderCommand::UpdateBufferData(TargetCB, CB.Data.data(), CB.DataSize);
			RenderCommand::BindConstantBuffer(CB.Slot, TargetCB, CB.Stages);
		}
	}
}

void FRenderer::SubmitDrawCommands(std::span<const FMeshDrawCommand> Commands,
	const std::shared_ptr<FGpuTimingHistory>& SkeletalTiming, ESkinningMode MeasuredMode)
{
	using EScopeKind = FGpuProfiler::EScopeKind;
	FGpuProfiler* Profiler = SkeletalTiming ? &FGpuProfiler::Get() : nullptr;
	EScopeKind ActiveKind = EScopeKind::Pass;
	int32 TimingHandle = -1;

	TArray<FMeshDrawShaderResource> BoundResources;
	for (const FMeshDrawCommand& Cmd : Commands)
	{
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

		BindCommand(Cmd, BoundResources);
		if (Cmd.InstanceCount > 0)
		{
			RenderCommand::DrawIndexedInstanced(Cmd.IndexCount, Cmd.InstanceCount,
				Cmd.FirstIndex, Cmd.BaseVertexLocation, Cmd.StartInstanceLocation);
		}
		else
		{
			RenderCommand::DrawIndexed(Cmd.IndexCount, Cmd.FirstIndex, Cmd.BaseVertexLocation);
		}
	}

	const TArray<FMeshDrawShaderResource> EmptyResources{};
	UnbindUnusedResources(EmptyResources, BoundResources);

	if (Profiler)
		Profiler->EndScope(TimingHandle);
}

void FRenderer::EnsurePerObjectSlotCapacity(uint32 SlotCount)
{
	if (SlotCount <= PerObjectSlotCapacity) return;
	uint32 NewCapacity = std::max(PerObjectSlotCapacity * 2, MinPerObjectSlots);
	while (NewCapacity < SlotCount) NewCapacity *= 2;
	PerObjectSlotCB = RenderCommand::CreateConstantBuffer(NewCapacity * ObjectSlotBytes);
	if (!PerObjectSlotCB || !PerObjectSlotCB->GetBuffer())
	{
		HTR_LOG(Warning, "Per-object constant buffer creation failed; using per-draw updates.");
		PerObjectSlotCB.reset();
		PerObjectSlotCapacity = 0;
		return;
	}
	PerObjectSlotCapacity = NewCapacity;
}

uint8* FRenderer::BeginObjectConstants(uint32 MaxSlots)
{
	if (!bUsePerObjectSlots || MaxSlots == 0) return nullptr;
	EnsurePerObjectSlotCapacity(MaxSlots);
	if (!PerObjectSlotCB) { bUsePerObjectSlots = false; return nullptr; }
	uint8* Destination = static_cast<uint8*>(RenderCommand::MapWriteDiscard(PerObjectSlotCB.get()));
	if (!Destination) bUsePerObjectSlots = false;
	return Destination;
}

void FRenderer::EndObjectConstants()
{
	if (PerObjectSlotCB) RenderCommand::Unmap(PerObjectSlotCB.get());
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

FOcclusionMeasureResult FRenderer::MeasureDrawVisibility(std::span<const FMeshDrawCommand> Commands)
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
	TArray<FMeshDrawShaderResource> BoundResources;
	uint32 QueryIndex = 0;
	for (const FMeshDrawCommand& Cmd : Commands)
	{
		BindCommand(Cmd, BoundResources);
		RenderCommand::SetBlendState(EBlendState::NoColorWrite);
		Context->OMSetDepthStencilState(DepthLessEqualReadOnly.Get(), 0);
		ID3D11Query* Query = OcclusionQueries[QueryIndex++].Get();
		Context->Begin(Query);
		RenderCommand::DrawIndexed(Cmd.IndexCount, Cmd.FirstIndex, Cmd.BaseVertexLocation);
		Context->End(Query);
	}
	std::unordered_map<const void*, bool> ObjectVisible;
	ObjectVisible.reserve(Count);
	QueryIndex = 0;
	for (const FMeshDrawCommand& Cmd : Commands)
	{
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
		if (Cmd.bOccludedByGpu)
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
	Result.ElapsedMs = std::chrono::duration<double, std::milli>(
		std::chrono::high_resolution_clock::now() - StartTime).count();
	Result.bValid = true;
	return Result;
}
