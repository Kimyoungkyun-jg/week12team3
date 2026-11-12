#pragma once

#include "Components/PrimitiveComponent.h"

class FOutlineRenderer;
struct FSceneView;
struct FViewportSettings;

class FOutline
{
public:
	FOutline();
	~FOutline();

	// 대상 메시의 외곽선을 View의 카메라 기준으로 그린다. 씬 위에 올리는 패스 안에서 부른다.
	// FGizmo::Render와 같은 방식이다. UE는 선택 외곽선을 렌더러 내부에서 그리므로 대응하는 함수가 없다.
	void Render(const FSceneView* View, const FViewportSettings& Viewport) const;

	void SetTarget(UPrimitiveComponent* InTarget) { Target = InTarget; }
	UPrimitiveComponent* GetTarget() const { return Target; }
	UStaticMesh* GetMesh() const { return Target ? Target->GetRenderMesh() : nullptr; }
	FMatrix GetWorldMatrix() const { return Target->GetWorldMatrix(); }
	const FVector& GetTargetScale() const { return Target->GetRelativeScale3D(); }
private:
	UPrimitiveComponent* Target = nullptr;

	// 스텐실과 셰이더로 실제 그리기를 맡는 도구
	TUniquePtr<FOutlineRenderer> Renderer;

};