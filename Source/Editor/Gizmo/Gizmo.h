#pragma once

#include "Collision/Ray.h"
#include "Math/Transform.h"
#include "Components/SceneComponent.h"
#include "Components/LocalLightComponent.h"

enum class EGizmoMode
{
	Location,
	Rotation,
	Scale,
};

enum class EGizmoSpace
{
	Local,
	World,
};

class FGizmo   // 상태 + 로직
{
public:
	// 축 번호: 0~2 = X/Y/Z 축, 3~5 = YZ/ZX/XY 평면(번호 - 3이 평면의 법선 축), 6 = 화면 축(중앙 구·바깥 링)
	static constexpr int AxisPlaneYZ = 3;
	static constexpr int AxisPlaneZX = 4;
	static constexpr int AxisPlaneXY = 5;
	static constexpr int AxisScreen = 6;
	static bool IsPlaneAxis(int Axis) { return Axis >= AxisPlaneYZ && Axis <= AxisPlaneXY; }

	// 평면 핸들 사각형이 두 축을 따라 차지하는 구간. 축 길이와 같은 기즈모 단위다.
	static constexpr float PlaneHandleMin = 0.3f;
	static constexpr float PlaneHandleMax = 0.6f;
	// 평면 핸들을 이루는 두 축 방향. 법선 축의 다음 두 축 순서(YZ, ZX, XY)다.
	void GetPlaneHandleAxes(int PlaneAxis, FVector& OutU, FVector& OutV) const;

	// Gizmo 대상 설정
	void SetTarget(USceneComponent* InTarget, bool InbUseActorBounds = false)
	{ 
		EndDrag();
		HoveredAxis = -1;
		Target = InTarget; 
		bUseActorBounds = InbUseActorBounds; 
		Targets.Reset();
		if (Target)
			Targets.Add(Target);
	}
	// Primary를 공통 피벗으로 삼아 선택된 컴포넌트 전체를 변환한다.
	void SetTargets(USceneComponent* InPrimary, const TArray<USceneComponent*>& InTargets, bool InbUseActorBounds = false);
	USceneComponent* GetTarget() const { return Target; }

	// Gizmo 모드(이동, 회전, 크기)
	void SetMode(EGizmoMode InMode) { if (Mode != InMode) EndDrag(); Mode = InMode; }
	EGizmoMode GetMode() const { return Mode; }

	void SetSpace(EGizmoSpace InSpace) { if (Space != InSpace) EndDrag(); Space = InSpace; }
	EGizmoSpace GetSpace() const { return Space; }

	// Active View의 Ray와 행렬만 사용해 Hover·Drag 상태를 갱신한다.
	void Update(const FRay& MouseRay, const FVector2& MousePos,
		const FMatrix& ViewProj, int ScreenW, int ScreenH,
		bool bMouseDown, const FVector& CameraLocation, bool bCameraOrthographic);

	bool IsUsing() const { return DraggingAxis >= 0; }
	int GetHoveredAxis() const { return HoveredAxis; }
	// 커서가 View를 벗어나 Update를 부르지 않을 때 축 강조를 끈다
	void ClearHover() { HoveredAxis = -1; }

	int PickAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);

	int PickLinearAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);
	int PickRotationAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);
	// 화면에 투영한 평면 핸들 사각형 안에 마우스가 있는 평면을 고른다. 없으면 -1이다.
	int PickPlaneAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH) const;


	void BeginDrag(int Axis, const FRay& MouseRay, const FVector2& MousePos);
	void UpdateDrag(const FRay& MouseRay, const FVector2& MousePos);
	void EndDrag();

	float ComputeAngleOnPlane(const FVector& Point, int Axis) const;

	inline FTransform GetTransform() const { return Target ? Target->GetTransform() : FTransform(); }
	inline FVector GetLocation() const { return Target ? Target->GetRelativeLocation() : FVector(0, 0, 0); }
	inline FRotator GetRotation() const { return Target ? Target->GetRelativeRotation() : FRotator(0, 0, 0); }
	inline FVector GetScale() const { return Target ? Target->GetRelativeScale3D() : FVector(0, 0, 0); }

	FVector GetRenderLocation() const;
	// 원근 View에서만 카메라 거리에 맞춰 크기 보정 위치를 구하고 직교 View는 월드 위치를 유지한다.
	FVector GetRenderLocationForView(const FVector& CameraLocation, bool bCameraOrthographic) const;
	// 직교 View는 줌과 상관없이 화면 크기가 같도록 View 높이에 비례한 배율을 구한다. 원근은 위치로 보정하므로 1이다.
	float GetRenderScaleForView(const FMatrix& ViewProj, bool bCameraOrthographic) const;
	FVector GetCameraLocation() const;
	FVector GetAxisDirection(int Axis) const;
private:
	EGizmoMode Mode = EGizmoMode::Location;
	EGizmoSpace Space = EGizmoSpace::Local;
	FTransform Transform = FTransform();
	USceneComponent* Target = nullptr;
	bool bUseActorBounds = false;
	TArray<USceneComponent*> Targets;
	struct FDragTargetStart
	{
		USceneComponent* Component;
		FVector WorldLocation;
		FQuat WorldRotation;
		FVector WorldScale;
		FVector RelativeScale;
		ULocalLightComponent* Light;
		float LightRadius;
	};
	TArray<FDragTargetStart> DragTargetStarts;
	void ApplyTranslation(const FVector& Delta);
	void ApplyRotation(const FQuat& Delta);
	void ApplyScale(const FVector& Factors, float LightFactor);
	FQuat DragStartWorldRotation;
	FVector DragScaleAxes[3];

	int HoveredAxis = -1;
	int DraggingAxis = -1;

	FVector2 DragStartMousePos;
	FVector DragStartPoint;
	FVector DragAxisDirection;
	FVector DragStartRenderLocation;

	FVector DragPlaneNormal;
	// 드래그 평면이 지나는 점. 이동은 실제 대상 위치, 회전·크기는 화면 고정 크기로 그린 기즈모 위치다.
	FVector DragPlaneOrigin;
	FVector DragStartWorldLocation;

	FVector ViewCameraLocation{};
	bool bViewCameraOrthographic = false;
	float ViewRenderScale = 1.0f;

	// 크기 모드로 점·스팟·사각 라이트를 고르면 스케일 대신 감쇠 반지름을 바꾼다 (TEAM6).
	static ULocalLightComponent* GetTargetLight(USceneComponent* Component);

	float DragStartAngle;
	float RingRadius = 1.0f;
};
