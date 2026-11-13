#pragma once

#include "Render/Renderer.h"
#include "Render/RenderingInfo.h"
#include "Render/PipelineState.h"

// FXAA 처리를 하기 위해 필요한 값을 넘긴다.
struct alignas(16) FFXAAData
{
	float InvWidth;
	float InvHeight;
	float Padding[2];
};

class FFXAARenderer
{
public:
	// Shader와 상수 버퍼를 준비한다.
	void Init(FRenderer* InRenderer);
	// Color Buffer에 있는 값을 바탕으로 FXAA를 처리한다.
	bool PrepareOutput(FTexture2D* ColorTarget);
	bool OnRender(FTexture2D* ColorTarget);
	// 직전 PrepareOutput이 고른 출력 버퍼. 렌더 후 SceneColor와 swap된다.
	TUniquePtr<FTexture2D>& GetOutputTarget() { return CachedColorBuffers[ActiveIndex].Buffer; }

private:
	// 크기가 다른 뷰포트(멀티 뷰포트, 에디터 프리뷰)가 번갈아 그려도
	// 매번 다시 만들지 않도록 크기별로 몇 장을 보관한다.
	static constexpr uint32 MaxCachedBuffers = 8;
	struct FCachedBuffer
	{
		TUniquePtr<FTexture2D> Buffer;
		uint64 LastUse = 0;
	};

	FRenderer* Renderer = nullptr;

	FShaderProgram* Shader = nullptr;
	FPipelineState FXAAPipelineState;

	TUniquePtr<FConstantBuffer> ConstantBuffer;
	FCachedBuffer CachedColorBuffers[MaxCachedBuffers];
	uint32 ActiveIndex = 0;
	uint64 UseCounter = 0;
};
