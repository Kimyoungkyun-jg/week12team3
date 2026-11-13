#include "EnginePCH.h"
#include "Render/PostProcess/FXAARenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

static_assert(sizeof(FFXAAData) == 16);

// Shader와 상수 버퍼를 준비한다.
void FFXAARenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/FxaaShader.hlsl");

	// depth buffer를 사용하지 않고, 별도의 shader를 통해 렌더링을 처리한다.
	FXAAPipelineState.Shader = Shader;
	FXAAPipelineState.Topology = EPrimitiveTopology::TriangleList;
	FXAAPipelineState.RasterizerState = ERasterizerState::SolidNone;
	FXAAPipelineState.BlendState = EBlendState::Opaque;
	FXAAPipelineState.DepthStencilState = EDepthStencilState::Disabled;

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FFXAAData));


	assert(Shader && Shader->VertexShader && Shader->PixelShader &&
		ConstantBuffer && ConstantBuffer->GetBuffer());
}

// Color Buffer에 있는 값을 바탕으로 FXAA를 처리한다.
bool FFXAARenderer::PrepareOutput(FTexture2D* ColorTarget)
{
	if (!ColorTarget) return false;
	const uint32 Width = ColorTarget->GetWidth();
	const uint32 Height = ColorTarget->GetHeight();

	// 같은 크기·포맷 캐시를 찾아 재사용하고, 없으면 가장 오래 안 쓴 칸에 새로 만든다.
	// 생성에 실패하면 원래 화면을 유지한다.
	++UseCounter;
	uint32 OldestIndex = 0;
	for (uint32 Index = 0; Index < MaxCachedBuffers; ++Index)
	{
		FCachedBuffer& Entry = CachedColorBuffers[Index];
		if (Entry.Buffer && Entry.Buffer->GetWidth() == Width &&
			Entry.Buffer->GetHeight() == Height &&
			Entry.Buffer->GetFormat() == ColorTarget->GetFormat())
		{
			Entry.LastUse = UseCounter;
			ActiveIndex = Index;
			return true;
		}
		// 빈 칸(나이 0으로 취급)이 있으면 그 칸을, 없으면 가장 오래 안 쓴 칸을 고른다.
		const uint64 EntryAge = Entry.Buffer ? Entry.LastUse : 0;
		const FCachedBuffer& Oldest = CachedColorBuffers[OldestIndex];
		if (EntryAge < (Oldest.Buffer ? Oldest.LastUse : 0))
			OldestIndex = Index;
	}

	FTextureDesc Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.Format = ColorTarget->GetTextureFormat();
	Desc.BindFlags = ETextureBindFlags::RenderTarget | ETextureBindFlags::ShaderResource;
	auto NewTarget = RenderCommand::CreateTexture2D(Desc);
	if (!NewTarget || !NewTarget->GetRawPtr() || !NewTarget->GetRTV() || !NewTarget->GetSRV())
		return false;
	CachedColorBuffers[OldestIndex].Buffer = std::move(NewTarget);
	CachedColorBuffers[OldestIndex].LastUse = UseCounter;
	ActiveIndex = OldestIndex;
	return true;
}

bool FFXAARenderer::OnRender(FTexture2D* ColorTarget)
{
	if (!ColorTarget || !GetOutputTarget() || !ConstantBuffer) return false;
	const uint32 Width = ColorTarget->GetWidth();
	const uint32 Height = ColorTarget->GetHeight();
	FFXAAData Data{};
	Data.InvWidth = 1.0f / Width;
	Data.InvHeight = 1.0f / Height;

	RenderCommand::BindPipelineState(FXAAPipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, ColorTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(3);
	RenderCommand::BindShaderResource(0, static_cast<FTexture2D*>(nullptr), EShaderBindFlagBits::Pixel);
	return true;
}
