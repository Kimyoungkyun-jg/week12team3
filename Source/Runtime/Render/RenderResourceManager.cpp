#include "EnginePCH.h"
#include "RenderResourceManager.h"

#include "RenderCommand.h"

#include "RenderUtil.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

namespace
{
	// 파일 본문과 #include "..."로 끌어온 파일들을 이어 붙인다. 진입점이 include한 파일에만 있는 셰이더
	// (예: EditorArrowShader.hlsl → UberLit.hlsl)도 VS/PS 셰이더로 알아보려면 include까지 봐야 한다.
	void AppendShaderSource(const fs::path& Path, std::set<fs::path>& Visited, std::string& Out)
	{
		std::error_code Ec;
		const fs::path Canonical = fs::weakly_canonical(Path, Ec);
		if (Ec || !Visited.insert(Canonical).second) return;
		std::ifstream File(Path, std::ios::binary);
		if (!File) return;
		const std::string Source(std::istreambuf_iterator<char>{ File }, std::istreambuf_iterator<char>{});
		Out += Source;
		for (size_t Pos = Source.find("#include"); Pos != std::string::npos; Pos = Source.find("#include", Pos + 8))
		{
			const size_t Open = Source.find('"', Pos);
			const size_t LineEnd = Source.find('\n', Pos);
			if (Open == std::string::npos || (LineEnd != std::string::npos && Open > LineEnd)) continue;
			const size_t Close = Source.find('"', Open + 1);
			if (Close == std::string::npos) continue;
			AppendShaderSource(Path.parent_path() / Source.substr(Open + 1, Close - Open - 1), Visited, Out);
		}
	}
}


struct FPipelineTableEntry
{
	EPSOType Type;
	const char* ShaderPath;
	EPrimitiveTopology Topology;
	ERasterizerState RasterizerState;
	EBlendState BlendState;
	EDepthStencilState DepthStencilState;
};

constexpr FPipelineTableEntry PipelineTable[] =
{
	{ EPSOType::StaticMesh_Opaque,       "Resources/Shader/StaticMeshShader.hlsl",            EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::StaticMesh_Translucent,  "Resources/Shader/StaticMeshTranslucentShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::StaticMesh_Wireframe,    "Resources/Shader/StaticMeshShader.hlsl",    EPrimitiveTopology::TriangleList, ERasterizerState::Wireframe, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::Particle_AlphaBlend,     "Resources/Shader/ParticleSubUVShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_Additive,       "Resources/Shader/ParticleSubUVShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Additive,     EDepthStencilState::ReadOnly },
	{ EPSOType::Skybox,                  "Resources/Shader/SkyboxShader.hlsl",        EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::ReadOnly },
	{ EPSOType::Grid,                    "Resources/Shader/GridShader.hlsl",          EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::AlphaBlend,   EDepthStencilState::Default },
	{ EPSOType::Outline_Mask,            "Resources/Shader/OutlineShader.hlsl",       EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::NoColorWrite, EDepthStencilState::StencilMask },
	{ EPSOType::Outline_Draw,            "Resources/Shader/OutlineShader.hlsl",       EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::StencilOutline },
	{ EPSOType::StaticMesh_GBuffer,		 "Resources/Shader/StaticMeshGBufferShader.hlsl",	EPrimitiveTopology::TriangleList,	ERasterizerState::SolidBack,	EBlendState::Opaque,  EDepthStencilState::Default },
	{ EPSOType::DeferredLighting,       "Resources/Shader/DeferredLightingShader.hlsl",    EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Disabled },
	{ EPSOType::DeferredPointLighting,  "Resources/Shader/DeferredLightingShader.hlsl",    EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Additive,     EDepthStencilState::Disabled },
	{ EPSOType::ToneMap,                "Resources/Shader/ToneMapShader.hlsl",              EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Disabled },
	{ EPSOType::Reserved_13,            nullptr,                                            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque,       EDepthStencilState::Default },
	{ EPSOType::Billboard,              "Resources/Shader/BillboardShader.hlsl",            EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend,   EDepthStencilState::ReadOnly },
	{ EPSOType::StaticMesh_Forward,		"Resources/Shader/UberLit.hlsl",			EPrimitiveTopology::TriangleList,			ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_WorldNormal, "Resources/Shader/UberLit.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_DepthOnly, "Resources/Shader/DepthPrepass.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::StaticMesh_LightShadow, "Resources/Shader/DepthPrepass.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::LightShadow, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::LightCullingDebug, "Resources/Shader/LightCullingDebug.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::Disabled },
	{ EPSOType::ShadowTileClear, "Resources/Shader/ShadowTileClear.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::NoColorWrite, EDepthStencilState::AlwaysWrite },
	{ EPSOType::ShadowDepth, "Resources/Shader/ShadowDepth.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::LightShadow, EBlendState::NoColorWrite, EDepthStencilState::Default },
	{ EPSOType::ShadowProjection, "Resources/Shader/ShadowProjection.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::ShadowMaskR, EDepthStencilState::Disabled },
	{ EPSOType::ShadowMaskVisualize, "Resources/Shader/ShadowMaskVisualize.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Disabled },
	{ EPSOType::StaticMesh_DBufferDecal, "Resources/Shader/ProjectedDecal.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidBack, EBlendState::DecalPremultiplied, EDepthStencilState::DecalEqualReadOnly },
	{ EPSOType::DBufferDecal_DepthReconstruction, "Resources/Shader/ProjectedDecalDepth.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::DecalPremultiplied, EDepthStencilState::Disabled },
	{ EPSOType::Particle_Instanced, "Resources/Shader/ParticleInstancedShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly },
	{ EPSOType::Particle_GpuInstanced, "Resources/Shader/ParticleGpuInstancedShader.hlsl", EPrimitiveTopology::TriangleList, ERasterizerState::SolidNone, EBlendState::AlphaBlend, EDepthStencilState::ReadOnly }
};

static_assert(sizeof(PipelineTable) / sizeof(PipelineTable[0]) == static_cast<size_t>(EPSOType::Count));

void FRenderResourceManager::InitPipelineStates()
{
	for (size_t i = 0; i < static_cast<size_t>(EPSOType::Count); ++i)
	{
		const auto& Entry = PipelineTable[i];
		const EPSOType Type = static_cast<EPSOType>(i);
		PipelineStateMap[Type] = MakeUnique<FPipelineState>();
		FPipelineState* State = PipelineStateMap[Type].get();
		// World Normal은 전용 매크로 변형이 성공했을 때만 연결한다.
		// 셰이더가 없는 항목은 번호만 남긴 예약 자리다.
		State->Shader = Type == EPSOType::StaticMesh_WorldNormal || !Entry.ShaderPath ? nullptr : GetShaderProgram(Entry.ShaderPath);
		State->Topology = Entry.Topology;
		State->RasterizerState = Entry.RasterizerState;
		State->BlendState = Entry.BlendState;
		State->DepthStencilState = Entry.DepthStencilState;
		State->StencilRef =
			Type == EPSOType::Outline_Mask ? StencilBits::Outline : 0;
	}
	// Shadow 배치가 참조하는 VS 전용 프로그램은 리소스 매니저가 소유한다.
	FShaderProgram* DepthProgram = GetShaderProgram("Resources/Shader/DepthPrepass.hlsl");
	if (DepthProgram && DepthProgram->VertexShader)
		ShaderProgramMap["ShadowDepth"] = MakeUnique<FShaderProgram>(DepthProgram->VertexShader, nullptr);
	const FString ShadowPath = "Resources/Shader/ShadowDepth.hlsl";
	if (auto* Base = ShaderProgramMap.Find(ShadowPath))
	{
		const D3D_SHADER_MACRO Defines[] = { { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
		FString VSPath;
		FShaderByteCode Code = RenderUtil::GetOrCompile(ShadowPath, "mainVS", EShaderType::Vertex, VSPath, Defines);
		if (Code.IsValid())
		{
			auto VS = RenderCommand::CreateVertexShader(Code);
			if (VS && VS->IsValid())
			{
				FVertexShader* VSRaw = VS.get();
				VertexShaderMap[VSPath] = std::move(VS);
				ShaderProgramMap[ShadowPath + "#GPU"] = MakeUnique<FShaderProgram>(VSRaw, (*Base)->PixelShader);
			}
		}
	}

	struct FForwardVariant { const char* Name; const char* Shading; const char* Reflection; };
	const FForwardVariant Variants[] = {
		{ "GouraudPhong", "SHADING_MODEL_GOURAUD", "REFLECTION_MODEL_PHONG" },
		{ "GouraudBlinnPhong", "SHADING_MODEL_GOURAUD", "REFLECTION_MODEL_BLINN_PHONG" },
		{ "GouraudLambert", "SHADING_MODEL_GOURAUD", "REFLECTION_MODEL_LAMBERT" },
		{ "PhongPhong", "SHADING_MODEL_PHONG", "REFLECTION_MODEL_PHONG" },
		{ "PhongBlinnPhong", "SHADING_MODEL_PHONG", "REFLECTION_MODEL_BLINN_PHONG" },
		{ "PhongLambert", "SHADING_MODEL_PHONG", "REFLECTION_MODEL_LAMBERT" },
		{ "VIEW_MODE_UNLIT", "VIEW_MODE_UNLIT", "REFLECTION_MODEL_LAMBERT" },
		{ "VIEW_MODE_WORLD_NORMAL", "VIEW_MODE_WORLD_NORMAL", "REFLECTION_MODEL_LAMBERT" }
	};
	for (const auto& Variant : Variants)
	{
		const char* Name = Variant.Name;
		const D3D_SHADER_MACRO Defines[] = { { Variant.Shading, "1" }, { Variant.Reflection, "1" }, { nullptr, nullptr } };
		const FString Path = "Resources/Shader/UberLit.hlsl";
		// 변형도 .cso 캐시를 쓴다(매크로 이름이 캐시 파일 이름에 들어간다). 매번 컴파일하면 Release에서 시작이 크게 늦어진다.
		FString VSCSOPath;
		FString PSCSOPath;
		FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath, Defines);
		FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath, Defines);
		if (!VSCode.IsValid() || !PSCode.IsValid())
		{
			HTR_LOG(Error, "[Shader] UberShader variant compile failed: {}", Name);
			continue;
		}
		auto VS = RenderCommand::CreateVertexShader(VSCode);
		auto PS = RenderCommand::CreatePixelShader(PSCode);
		if (!VS || !VS->IsValid() || !PS || !PS->IsValid())
		{
			continue;
		}
		FVertexShader* VSRaw = VS.get();
		FPixelShader* PSRaw = PS.get();
		const FString Key = Path + "#" + Name;
		VertexShaderMap[Key + "#VS"] = std::move(VS);
		PixelShaderMap[Key + "#PS"] = std::move(PS);
		ShaderProgramMap[Key] = MakeUnique<FShaderProgram>(VSRaw, PSRaw);
		if (std::strcmp(Name, "VIEW_MODE_WORLD_NORMAL") == 0)
			PipelineStateMap[EPSOType::StaticMesh_WorldNormal]->Shader = ShaderProgramMap[Key].get();

		const D3D_SHADER_MACRO SkinningDefines[] = { { Variant.Shading, "1" }, { Variant.Reflection, "1" }, { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
		FString SkinningVSPath;
		FShaderByteCode SkinningCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, SkinningVSPath, SkinningDefines);
		if (!SkinningCode.IsValid()) continue;
		auto SkinningVS = RenderCommand::CreateVertexShader(SkinningCode);
		if (!SkinningVS || !SkinningVS->IsValid()) continue;
		FVertexShader* SkinningVSRaw = SkinningVS.get();
		VertexShaderMap[SkinningVSPath] = std::move(SkinningVS);
		ShaderProgramMap[Key + "#GPU"] = MakeUnique<FShaderProgram>(SkinningVSRaw, PSRaw);
	}
	const FString TranslucentPath = "Resources/Shader/StaticMeshTranslucentShader.hlsl";
	if (auto* Base = ShaderProgramMap.Find(TranslucentPath))
	{
		const D3D_SHADER_MACRO Defines[] = { { "GPU_SKINNING", "1" }, { nullptr, nullptr } };
		FString VSPath;
		FShaderByteCode Code = RenderUtil::GetOrCompile(TranslucentPath, "mainVS", EShaderType::Vertex, VSPath, Defines);
		if (Code.IsValid())
		{
			auto VS = RenderCommand::CreateVertexShader(Code);
			if (VS && VS->IsValid())
			{
				FVertexShader* VSRaw = VS.get();
				VertexShaderMap[VSPath] = std::move(VS);
				ShaderProgramMap[TranslucentPath + "#GPU"] = MakeUnique<FShaderProgram>(VSRaw, (*Base)->PixelShader);
			}
		}
	}
}

FShaderProgram* FRenderResourceManager::GetShadowDepthShaderProgram()
{
	if (auto* Found = Get().ShaderProgramMap.Find("ShadowDepth")) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetGPUSkinningShadowDepthShaderProgram()
{
	if (auto* Found = Get().ShaderProgramMap.Find("Resources/Shader/ShadowDepth.hlsl#GPU")) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetForwardShaderProgram(EReflectionModel Reflection, EShadingModel Shading, bool bGPUSkinning, bool bUnlit)
{
	const char* ReflectionName = Reflection == EReflectionModel::Phong ? "Phong" :
		Reflection == EReflectionModel::Lambert ? "Lambert" : "BlinnPhong";
	const FString Name = bUnlit ? FString("VIEW_MODE_UNLIT") :
		FString(Shading == EShadingModel::Gouraud ? "Gouraud" : "Phong") + ReflectionName;
	const FString Key = FString("Resources/Shader/UberLit.hlsl#") + Name + (bGPUSkinning ? "#GPU" : "");
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetWorldNormalShaderProgram(bool bGPUSkinning)
{
	const FString Key = FString("Resources/Shader/UberLit.hlsl#VIEW_MODE_WORLD_NORMAL") + (bGPUSkinning ? "#GPU" : "");
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FShaderProgram* FRenderResourceManager::GetTranslucentShaderProgram(bool bGPUSkinning)
{
	const FString Key = FString("Resources/Shader/StaticMeshTranslucentShader.hlsl") + (bGPUSkinning ? "#GPU" : "");
	if (auto* Found = Get().ShaderProgramMap.Find(Key)) return Found->get();
	return nullptr;
}

FPipelineState* FRenderResourceManager::GetPSO(const EPSOType& InType)
{
	if (auto* Found = Get().PipelineStateMap.Find(InType))
	{
		return Found->get();
	}
	return nullptr;
}

void FRenderResourceManager::ScanShaders(const fs::path& ShaderRoot)
{
	std::error_code ErrorCode;

	if (!fs::exists(ShaderRoot, ErrorCode))
	{
		HTR_LOG(Error, "[Shader] Scan Root Not Found : {}", ShaderRoot.generic_string());
		return;
	}

	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(ShaderRoot))
	{
		if (!Entry.is_regular_file()) continue;
		if (Entry.path().extension() != ".hlsl") continue;
		// 컴퓨트 전용 .hlsl에는 mainVS/mainPS가 없다. VS/PS로 컴파일하지 않는다.
		std::string Source;
		std::set<fs::path> Visited;
		AppendShaderSource(Entry.path(), Visited, Source);
		if (Source.find("mainVS(") == std::string::npos ||
			Source.find("mainPS(") == std::string::npos) continue;

		FString Path = Entry.path().generic_string();
		LoadOrCompileShader(Path);
	}
}

FComputeShader* FRenderResourceManager::GetComputeSkinningShader()
{
	auto& Resources = Get();
	if (!Resources.bComputeSkinningShaderAttempted)
	{
		Resources.bComputeSkinningShaderAttempted = true;
		Resources.ComputeSkinningShader = RenderCommand::CreateComputeShader(
			RenderUtil::CompileShader("Resources/Shader/ComputeSkinningCS.hlsli", "mainCS", EShaderType::Compute));
		if (Resources.ComputeSkinningShader && !Resources.ComputeSkinningShader->IsValid())
			Resources.ComputeSkinningShader.reset();
	}
	return Resources.ComputeSkinningShader.get();
}

FComputeShader* FRenderResourceManager::GetGpuParticleInjectShader()
{
	auto& Resources = Get();
	if (!Resources.bGpuParticleInjectAttempted)
	{
		Resources.bGpuParticleInjectAttempted = true;
		Resources.GpuParticleInjectShader = RenderCommand::CreateComputeShader(
			RenderUtil::CompileShader("Resources/Shader/GpuParticleSimulate.hlsl", "CSInjectSpawns", EShaderType::Compute));
		if (Resources.GpuParticleInjectShader && !Resources.GpuParticleInjectShader->IsValid())
			Resources.GpuParticleInjectShader.reset();
	}
	return Resources.GpuParticleInjectShader.get();
}

FComputeShader* FRenderResourceManager::GetGpuParticleSimulateShader()
{
	auto& Resources = Get();
	if (!Resources.bGpuParticleSimulateAttempted)
	{
		Resources.bGpuParticleSimulateAttempted = true;
		Resources.GpuParticleSimulateShader = RenderCommand::CreateComputeShader(
			RenderUtil::CompileShader("Resources/Shader/GpuParticleSimulate.hlsl", "CSSimulate", EShaderType::Compute));
		if (Resources.GpuParticleSimulateShader && !Resources.GpuParticleSimulateShader->IsValid())
			Resources.GpuParticleSimulateShader.reset();
	}
	return Resources.GpuParticleSimulateShader.get();
}

void FRenderResourceManager::Shutdown()
{
	Get().ComputeSkinningShader.reset();
	Get().bComputeSkinningShaderAttempted = false;
	Get().GpuParticleInjectShader.reset();
	Get().bGpuParticleInjectAttempted = false;
	Get().GpuParticleSimulateShader.reset();
	Get().bGpuParticleSimulateAttempted = false;
	Get().VertexShaderMap.Empty();
	Get().PixelShaderMap.Empty();
	Get().ShaderProgramMap.Empty();
}

FShaderProgram* FRenderResourceManager::GetShaderProgram(const FString& InPath)
{
	if (TUniquePtr<FShaderProgram>* Found = Get().ShaderProgramMap.Find(InPath))
		return Found->get();

	HTR_LOG(Error, "[Shader] not found: {}", InPath);

	if (TUniquePtr<FShaderProgram>* Fallback =
		Get().ShaderProgramMap.Find("Resources/Shader/DefaultShader.hlsl"))
		return Fallback->get();

	return nullptr;
}



void FRenderResourceManager::LoadOrCompileShader(const FString& Path)
{ 
	FString VSCSOPath;
	FString PSCSOPath;
	FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath);
	FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath);

	if (!VSCode.IsValid() || !PSCode.IsValid())
	{
		HTR_LOG(Error, "[Shader] compile failed: {}", Path);
		return;
	}

	TUniquePtr<FVertexShader> Vs = RenderCommand::CreateVertexShader(VSCode);
	TUniquePtr<FPixelShader>  Ps = RenderCommand::CreatePixelShader(PSCode);

	if (!Vs || !Vs->IsValid() || !Ps || !Ps->IsValid())
	{
		HTR_LOG(Error, "[Shader] device create failed: {}", Path);
		return;
	}

	FVertexShader* VsRaw = Vs.get();
	FPixelShader* PsRaw = Ps.get();

	VertexShaderMap[VSCSOPath] = std::move(Vs);
	PixelShaderMap[PSCSOPath] = std::move(Ps);
	ShaderProgramMap[Path] = MakeUnique<FShaderProgram>(VsRaw, PsRaw);

	HTR_LOG(Info, "[Shader] loaded: {}", Path);

}
