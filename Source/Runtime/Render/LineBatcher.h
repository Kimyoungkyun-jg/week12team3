#pragma once

#include "Buffer.h"
#include "StaticMeshData.h"
#include "PipelineState.h"

class FRenderer;
class UWorld;

class FLineBatcher
{
public:
	FLineBatcher() = default;
	~FLineBatcher();
	bool Init(FRenderer* InRenderer, UWorld* InWorld);
	void BuildVertexBuffer();

	void BeginFrame();


	void AddLine(const FVector& A, const FVector& B, const FVector4& Color);
	void AddBox(FBox BoundingBox, FVector4 Color);

	// 로컬 AABB의 모서리를 각각 월드로 옮겨 그린다. 회전이 살아있는 OBB가 된다.
	void AddOrientedBox(const FBox& LocalBox, const FMatrix& WorldMatrix, const FVector4& Color);
	void AddPath(const TArray<FVector>& Points, const FVector4& Color);

	// 밑면 중심에서 꼭짓점으로 향하는 사각뿔의 모서리를 그립니다.
	void AddPyramid(const FVector& BaseCenter, const FVector& Tip, float HalfWidth, const FVector4& Color);
	// 서로 직교하는 세 원으로 와이어 구를 그립니다.
	void AddSphere(const FVector& Center, float Radius, const FVector4& Color, int32 Segments = 16);

	void Flush();
	void OnRender(const FMatrix& ViewProjection);
	// 꺼 두면 다른 물체에 가려지지 않고 항상 위에 그린다 (디버그 표시용)
	void SetDepthTest(bool bEnable) { PipelineState.DepthStencilState = bEnable ? EDepthStencilState::Default : EDepthStencilState::Disabled; }

private:
	FRenderer* Renderer;

	UWorld* World;

	FShaderProgram* LineShader;
	FPipelineState PipelineState;
	FLineBatchVertex* VertexBufferBase;  
	FLineBatchVertex* VertexBufferPtr;  
	TUniquePtr<FConstantBuffer> CB;
	uint32 VertexCount;

	uint32 MaxVertices = 0;
	TUniquePtr<FVertexBuffer> VertexBuffer;
};
