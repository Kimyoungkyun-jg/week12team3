#include "EnginePCH.h"
#include "Editor/Gizmo/Gizmo.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/LightActorBase.h"
#include "Camera/CameraComponent.h"
#include <algorithm>

static const FVector AxisDirs[3] = {
	FVector(1, 0, 0),
	FVector(0, 1, 0),
	FVector(0, 0, 1),
};

static const int AXIS_SCREEN = FGizmo::AxisScreen;   // 3축 자유 이동

static const float AxisLength = 1.5f;
static const float HitPixels = 12.0f;
// 이보다 작게 투영된 평면 핸들(픽셀²)은 거의 옆면이라 선택하지 않는다.
static const float MinPlaneHandleScreenArea = 16.0f;

// 원근 View에서는 기즈모를 카메라 앞 이 거리에 그려 화면 크기를 고정한다.
static const float PerspectiveGizmoDistance = 10.0f;
// 직교 View 높이에 대한 기즈모 크기 비율. 원근 FOV 60도에서 위 거리로 그린 크기와 같게 맞춘다 (tan 30도 = 0.57735).
static const float OrthoGizmoViewHeightRatio = 1.0f / (2.0f * PerspectiveGizmoDistance * 0.57735027f);

void FGizmo::SetTargets(USceneComponent* InPrimary, const TArray<USceneComponent*>& InTargets, bool InbUseActorBounds)
{
	SetTarget(InPrimary, InbUseActorBounds);
	if (!Target) return;
	for (USceneComponent* Component : InTargets)
	{
		if (Component && Targets.Find(Component) == INDEX_NONE)
			Targets.Add(Component);
	}
}

// 입력 View의 카메라 조건으로 축 선택과 드래그를 갱신한다.
void FGizmo::Update(const FRay& MouseRay, const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH, bool bMouseDown, const FVector& CameraLocation, const bool bCameraOrthographic)
{
	ViewCameraLocation = CameraLocation;
	bViewCameraOrthographic = bCameraOrthographic;
	ViewRenderScale = GetRenderScaleForView(ViewProj, bCameraOrthographic);

	if (!Target) // 만약 현재 타겟이 없다면 
	{
		HoveredAxis = -1;
		DraggingAxis = -1;
		return;
	}

	if (DraggingAxis >= 0) // 만약 드래그 중인 축이 있다면
	{
		if (bMouseDown)
			UpdateDrag(MouseRay, MousePos);
		else
			EndDrag();
		return;
	}

	HoveredAxis = PickAxis(MousePos, ViewProj, ScreenW, ScreenH); // 현재 마우스가 올라간 기즈모 축이 있으면

	if (bMouseDown && HoveredAxis >= 0) // 마우스가 올라간 축이 있고 마우스가 눌렸으면
		BeginDrag(HoveredAxis, MouseRay, MousePos);


}

// 모드에 따라 선형 축 또는 회전 링 피킹을 선택한다.
int FGizmo::PickAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH)
{
	if (Mode == EGizmoMode::Rotation)
		return PickRotationAxis(MousePos, ViewProj, ScreenW, ScreenH);

	return PickLinearAxis(MousePos, ViewProj, ScreenW, ScreenH);
}

// 투영한 축 선분과 마우스의 화면 거리로 조작 축을 고른다.
int FGizmo::PickLinearAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH)
{
	FVector Origin = GetRenderLocation();

	FVector2 Center = WorldToScreen(Origin, ViewProj, ScreenW, ScreenH);
	FVector2 d = MousePos - Center;
	if (sqrtf(d.X * d.X + d.Y * d.Y) < 15.0f)
		return AXIS_SCREEN;

	int Best = -1;
	float BestDist = HitPixels;

	for (int i = 0; i < 3; ++i)
	{
		FVector2 Start = WorldToScreen(Origin, ViewProj, ScreenW, ScreenH);
		FVector2 End = WorldToScreen(Origin + GetAxisDirection(i) * (AxisLength * ViewRenderScale), ViewProj, ScreenW, ScreenH);

		float Dist = DistanceToSegment(MousePos, Start, End);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = i;
		}
	}

	// 축 선분이 우선이고, 축에서 떨어진 곳이면 평면 핸들을 검사한다.
	if (Best < 0)
		Best = PickPlaneAxis(MousePos, ViewProj, ScreenW, ScreenH);
	return Best;
}

int FGizmo::PickPlaneAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH) const
{
	const FVector Origin = GetRenderLocation();
	const float Min = PlaneHandleMin * ViewRenderScale;
	const float Max = PlaneHandleMax * ViewRenderScale;

	for (int PlaneAxis = AxisPlaneYZ; PlaneAxis <= AxisPlaneXY; ++PlaneAxis)
	{
		FVector U, V;
		GetPlaneHandleAxes(PlaneAxis, U, V);
		const FVector2 Corners[4] = {
			WorldToScreen(Origin + U * Min + V * Min, ViewProj, ScreenW, ScreenH),
			WorldToScreen(Origin + U * Max + V * Min, ViewProj, ScreenW, ScreenH),
			WorldToScreen(Origin + U * Max + V * Max, ViewProj, ScreenW, ScreenH),
			WorldToScreen(Origin + U * Min + V * Max, ViewProj, ScreenW, ScreenH),
		};

		// 평면을 옆에서 봐서 선처럼 납작해진 핸들은 고르지 않는다.
		float TwiceArea = 0.0f;
		for (int Edge = 0; Edge < 4; ++Edge)
		{
			const FVector2& A = Corners[Edge];
			const FVector2& B = Corners[(Edge + 1) % 4];
			TwiceArea += A.X * B.Y - B.X * A.Y;
		}
		if (std::fabs(TwiceArea) < MinPlaneHandleScreenArea * 2.0f)
			continue;

		// 투영된 사각형은 볼록하므로 네 변의 외적 부호가 모두 같으면 안쪽이다.
		bool bHasPositive = false;
		bool bHasNegative = false;
		for (int Edge = 0; Edge < 4; ++Edge)
		{
			const FVector2& A = Corners[Edge];
			const FVector2& B = Corners[(Edge + 1) % 4];
			const float Cross = (B.X - A.X) * (MousePos.Y - A.Y) - (B.Y - A.Y) * (MousePos.X - A.X);
			bHasPositive |= Cross > 0.0f;
			bHasNegative |= Cross < 0.0f;
		}
		if (!(bHasPositive && bHasNegative))
			return PlaneAxis;
	}
	return -1;
}

void FGizmo::GetPlaneHandleAxes(int PlaneAxis, FVector& OutU, FVector& OutV) const
{
	const int NormalAxis = PlaneAxis - AxisPlaneYZ;
	OutU = GetAxisDirection((NormalAxis + 1) % 3);
	OutV = GetAxisDirection((NormalAxis + 2) % 3);
}

// 회전 링을 선분으로 나누어 화면 거리로 회전 축을 고른다.
int FGizmo::PickRotationAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH)
{
	const FVector Origin = GetRenderLocation();
	const int Segments = 32;
	const float AxisRingRadius = RingRadius * ViewRenderScale;

	int Best = -1;
	float BestDist = HitPixels;

	for (int Axis = 0; Axis < 3; ++Axis)
	{
		FVector u = GetAxisDirection((Axis + 1) % 3);
		FVector v = GetAxisDirection((Axis + 2) % 3);

		FVector2 prev;
		bool bHasPrev = false;

		for (int s = 0; s <= Segments; ++s)
		{
			float theta = (float)s / Segments * 2.0f * PI;
			FVector worldPos = Origin
				+ u * (AxisRingRadius * cosf(theta))
				+ v * (AxisRingRadius * sinf(theta));

			FVector2 screenPos = WorldToScreen(worldPos, ViewProj, ScreenW, ScreenH);

			if (bHasPrev)
			{
				float Dist = DistanceToSegment(MousePos, prev, screenPos);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					Best = Axis;
				}
			}

			prev = screenPos;
			bHasPrev = true;
		}
	}

	FVector CamPos = GetCameraLocation();
	FVector Forward = (Origin - CamPos).Normalized();
	FVector u = FVector(0, 0, 1).Cross(Forward).Normalized();
	FVector v = Forward.Cross(u);

	const float ScreenRingRadius = RingRadius * 1.3f * ViewRenderScale;

	FVector2 prev;
	bool bHasPrev = false;

	for (int s = 0; s <= Segments; ++s)
	{
		float theta = (float)s / Segments * 2.0f * PI;
		FVector worldPos = Origin
			+ u * (ScreenRingRadius * cosf(theta))
			+ v * (ScreenRingRadius * sinf(theta));

		FVector2 screenPos = WorldToScreen(worldPos, ViewProj, ScreenW, ScreenH);

		if (bHasPrev)
		{
			float Dist = DistanceToSegment(MousePos, prev, screenPos);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = AXIS_SCREEN;
			}
		}

		prev = screenPos;
		bHasPrev = true;
	}

	return Best;
}

// 시작 Transform·교차점을 저장하고 조작 축의 드래그 평면을 정한다.
void FGizmo::BeginDrag(int Axis, const FRay& MouseRay, const FVector2& MousePos)
{
	if (!Target) return;
	DragStartMousePos = MousePos;
	DraggingAxis = Axis;
	DragStartRenderLocation = GetRenderLocation();    
	DragStartWorldRotation = Target->GetWorldRotationQuat();
	for (int AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		DragScaleAxes[AxisIndex] = DragStartWorldRotation.RotateVector(AxisDirs[AxisIndex]);
	DragTargetStarts.Reset();
	for (USceneComponent* Component : Targets)
	{
		ULocalLightComponent* Light = GetTargetLight(Component);
		DragTargetStarts.Add({ Component, Component->GetWorldLocation(), Component->GetWorldRotationQuat(),
			Component->GetWorldScale3D(), Component->GetRelativeScale3D(), Light,
			Light ? Light->GetAttenuationRadius() : 0.0f });
	}
	// 부모를 먼저 적용한 뒤 자식의 절대 월드 값을 맞춰 중복 변환을 막는다.
	auto AttachmentDepth = [](const USceneComponent* Component)
	{
		int Depth = 0;
		for (const USceneComponent* Parent = Component->GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
			++Depth;
		return Depth;
	};
	std::stable_sort(DragTargetStarts.begin(), DragTargetStarts.end(), [&](const FDragTargetStart& A, const FDragTargetStart& B)
	{
		return AttachmentDepth(A.Component) < AttachmentDepth(B.Component);
	});

	DragAxisDirection = GetAxisDirection(Axis);
	FVector axis = DragAxisDirection;

	if (Axis == AXIS_SCREEN)
	{
		if (Mode == EGizmoMode::Rotation)
		{
			DragAxisDirection = (GetRenderLocation() - GetCameraLocation()).Normalized();
			DragPlaneNormal = DragAxisDirection;
		}
		else
		{
			DragPlaneNormal = -MouseRay.Direction;
		}
	}
	else if (IsPlaneAxis(Axis))
	{
		// 평면 핸들은 그 평면 자체에서 마우스를 따라간다.
		DragPlaneNormal = GetAxisDirection(Axis - AxisPlaneYZ);
	}
	else if (Mode == EGizmoMode::Rotation)
	{
		FVector ToCamera = (GetCameraLocation() - GetRenderLocation()).Normalized();
		if (DragAxisDirection.Dot(ToCamera) < 0.0f)
			DragAxisDirection = -DragAxisDirection;

		DragPlaneNormal = DragAxisDirection;
	}
	else
	{
		FVector ToCamera = -MouseRay.Direction;
		FVector Ortho = axis.Cross(ToCamera);
		DragPlaneNormal = Ortho.Cross(axis).Normalized();
	}

	// 원근 View의 기즈모는 카메라 앞 일정 거리에 그려지므로, 그 위치의 평면으로 이동량을 재면
	// 대상이 멀수록 마우스보다 덜 움직인다. 이동은 대상 깊이의 평면에서 재야 커서와 함께 움직인다.
	DragStartWorldLocation = Target->GetWorldLocation();
	DragPlaneOrigin = Mode == EGizmoMode::Location ? DragStartWorldLocation : DragStartRenderLocation;

	float t;
	if (RayIntersectsPlane(MouseRay, DragPlaneOrigin, DragPlaneNormal, t))
		DragStartPoint = MouseRay.Origin + MouseRay.Direction * t;
	else
		DragStartPoint = DragPlaneOrigin;

	if (Mode == EGizmoMode::Rotation)
		DragStartAngle = ComputeAngleOnPlane(DragStartPoint, Axis);
}

// 평면 교차점 변화를 이동·회전·크기에 반영한다.
void FGizmo::UpdateDrag(const FRay& MouseRay, const FVector2& MousePos)
{
	if (!Target || !IsUsing()) return;
	float t;
	if (!RayIntersectsPlane(MouseRay, DragPlaneOrigin, DragPlaneNormal, t))
		return;

	FVector current = MouseRay.Origin + MouseRay.Direction * t;

	if (DraggingAxis == AXIS_SCREEN && Mode != EGizmoMode::Rotation)
	{
		// 이동량은 월드 공간에서 쟀으므로 부모가 있어도 월드 위치로 적용한다.
		if (Mode == EGizmoMode::Location)
			ApplyTranslation(current - DragStartPoint);
		else
		{
			float dx = MousePos.X - DragStartMousePos.X;
			float dy = DragStartMousePos.Y - MousePos.Y;   // 화면 Y는 아래가 +
			float scaleDelta = (dx + dy) * 0.005f;

			float factor = 1.0f + scaleDelta;
			if (factor < 0.01f) factor = 0.01f;

			ApplyScale(FVector(factor, factor, factor), factor);
		}
		return;
	}

	if (Mode == EGizmoMode::Rotation)
	{
		float currentAngle = ComputeAngleOnPlane(current, DraggingAxis);
		float deltaAngle = currentAngle - DragStartAngle;

		FQuat delta = FQuat::MakeFromAxisAngle(DragAxisDirection, deltaAngle);
		ApplyRotation(delta);
		return;
	}

	if (IsPlaneAxis(DraggingAxis))
	{
		// 교차점이 평면 위에 있으므로 이동량도 그 평면 안의 두 축 성분만 가진다.
		const FVector PlaneDelta = current - DragStartPoint;
		if (Mode == EGizmoMode::Location)
		{
			ApplyTranslation(PlaneDelta);
			return;
		}

		// 크기: 두 축 사이 대각선 방향으로 끈 만큼 평면의 두 축을 같은 배율로 키운다.
		FVector U, V;
		const int NormalAxis = DraggingAxis - AxisPlaneYZ;
		U = DragScaleAxes[(NormalAxis + 1) % 3];
		V = DragScaleAxes[(NormalAxis + 2) % 3];
		const float PlaneAmount = PlaneDelta.Dot((U + V).Normalized());
		float PlaneFactor = 1.0f + PlaneAmount / ViewRenderScale;
		if (PlaneFactor < 0.01f) PlaneFactor = 0.01f;

		FVector Factors(1, 1, 1);
		Factors.V[(NormalAxis + 1) % 3] = PlaneFactor;
		Factors.V[(NormalAxis + 2) % 3] = PlaneFactor;
		ApplyScale(Factors, PlaneFactor);
		return;
	}

	FVector delta = current - DragStartPoint;
	float amount = delta.Dot(DragAxisDirection);

	if (Mode == EGizmoMode::Location)
		ApplyTranslation(DragAxisDirection * amount);
	else   // Scale
	{
		// 기즈모 크기 기준으로 나눠 직교 줌과 상관없이 같은 화면 드래그에 같은 배율이 되게 한다.
		float factor = 1.0f + amount / ViewRenderScale;
		if (factor < 0.01f) factor = 0.01f;

		FVector Factors(1, 1, 1);
		Factors.V[DraggingAxis] = factor;
		ApplyScale(Factors, factor);
	}
}

void FGizmo::ApplyTranslation(const FVector& Delta)
{
	for (const FDragTargetStart& Start : DragTargetStarts)
		Start.Component->SetWorldLocation(Start.WorldLocation + Delta, false);
}

void FGizmo::ApplyRotation(const FQuat& Delta)
{
	for (const FDragTargetStart& Start : DragTargetStarts)
	{
		const FVector Location = DragStartWorldLocation + Delta.RotateVector(Start.WorldLocation - DragStartWorldLocation);
		Start.Component->SetWorldLocationAndRotation(Location, (Delta * Start.WorldRotation).Normalized().ToFRotator(), false);
	}
}

void FGizmo::ApplyScale(const FVector& Factors, float LightFactor)
{
	for (const FDragTargetStart& Start : DragTargetStarts)
	{
		if (Start.Light)
			Start.Light->SetAttenuationRadius(Start.LightRadius * LightFactor);
		else
		{
			const FVector WorldScale = Start.WorldScale * Factors;
			const USceneComponent* Parent = Start.Component->GetAttachParent();
			const FVector ParentScale = Parent ? Parent->GetWorldScale3D() : FVector(1, 1, 1);
			FVector RelativeScale = Start.RelativeScale;
			for (int Axis = 0; Axis < 3; ++Axis)
				if (std::fabs(ParentScale.V[Axis]) > SMALL_NUMBER)
					RelativeScale.V[Axis] = WorldScale.V[Axis] / ParentScale.V[Axis];
			Start.Component->SetRelativeScale3D(RelativeScale);
		}

		const FVector Offset = DragStartWorldRotation.Inverse().RotateVector(Start.WorldLocation - DragStartWorldLocation);
		Start.Component->SetWorldLocation(DragStartWorldLocation + DragStartWorldRotation.RotateVector(Offset * Factors), false);
	}
}

// LocalLightComponent 가져오기 (Rect, Point, Spot 등 Radius를 파라미터로 갖는 것들)
ULocalLightComponent* FGizmo::GetTargetLight(USceneComponent* Component)
{
	if (!Component) return nullptr;
	ALightActorBase* Actor = Cast<ALightActorBase>(Component->GetOwner());
	return Actor ? Cast<ULocalLightComponent>(Actor->GetLightComponent()) : nullptr;
}

// 드래그 축을 비워 조작을 종료한다.
void FGizmo::EndDrag()
{
	DraggingAxis = -1;
	DragTargetStarts.Reset();
}

// 평면 기저에 점을 투영하고 atan2로 회전각을 구한다.
float FGizmo::ComputeAngleOnPlane(const FVector& Point, int Axis) const
{
	FVector u, v;

	if (DraggingAxis >= 0)
	{
		FVector Ref = (fabsf(DragAxisDirection.Z) > 0.9f) ? FVector(1, 0, 0) : FVector(0, 0, 1);
		u = Ref.Cross(DragAxisDirection).Normalized();
		v = DragAxisDirection.Cross(u);
	}
	else if (Axis == AXIS_SCREEN)
	{
		FVector Forward = (GetRenderLocation() - GetCameraLocation()).Normalized();
		u = FVector(0, 0, 1).Cross(Forward).Normalized();
		v = Forward.Cross(u);
	}
	else
	{
		u = AxisDirs[(Axis + 1) % 3];
		v = AxisDirs[(Axis + 2) % 3];
	}

	FVector local = Point - DragStartRenderLocation;
	return atan2f(local.Dot(v), local.Dot(u));
}

// 월드 축 또는 로컬 회전으로 변환한 정규화 축을 반환한다.
FVector FGizmo::GetAxisDirection(int Axis) const
{
	if (Axis < 0 || Axis > 2) return FVector(0, 0, 0);
	bool bUseLocal = (Space == EGizmoSpace::Local) || (Mode == EGizmoMode::Scale);

	if (bUseLocal && Target)
	{
		FMatrix rot = Target->GetWorldRotationQuat().ToFMatrix();
		FVector4 v = rot.TransformVector(AxisDirs[Axis]);
		return FVector(v.X, v.Y, v.Z).Normalized();
	}
	return AxisDirs[Axis];
}

// 입력 View 카메라 조건으로 Gizmo 표시 위치를 구한다.
FVector FGizmo::GetRenderLocation() const
{
	return GetRenderLocationForView(ViewCameraLocation, bViewCameraOrthographic);
}

// 직교에서는 대상 위치, 원근에서는 카메라 앞 일정 거리로 표시 위치를 정한다.
FVector FGizmo::GetRenderLocationForView(const FVector& CameraLocation, const bool bCameraOrthographic) const
{
	if (!Target) return FVector(0, 0, 0);

	FVector PivotLocation = Target->GetWorldLocation();

	// bounds 합집합 중앙에 기즈모 표시
	/*if (bUseActorBounds)
	{
		FBox Bounds;
		if (Target->GetOwner()->TryGetActorBounds(Bounds))
		{
			PivotLocation = (Bounds.Min + Bounds.Max) * 0.5f;
			
		}
	}*/

	if (bCameraOrthographic) return PivotLocation;

	return (PivotLocation - CameraLocation).Normalized() * PerspectiveGizmoDistance + CameraLocation;

}

float FGizmo::GetRenderScaleForView(const FMatrix& ViewProj, const bool bCameraOrthographic) const
{
	if (!bCameraOrthographic) return 1.0f;

	// 행 벡터 규약(v * VP)에서 Y 열은 월드 1유닛당 NDC Y 변화량이고, 직교에서는 그 길이가 2 / View 높이다.
	const float NdcPerUnitY = sqrtf(
		ViewProj.M[0][1] * ViewProj.M[0][1] +
		ViewProj.M[1][1] * ViewProj.M[1][1] +
		ViewProj.M[2][1] * ViewProj.M[2][1]);
	if (NdcPerUnitY <= 1.0e-8f) return 1.0f;

	const float ViewHeight = 2.0f / NdcPerUnitY;
	return ViewHeight * OrthoGizmoViewHeightRatio;
}

// 현재 입력 View의 카메라 위치를 반환한다.
FVector FGizmo::GetCameraLocation() const
{
	return ViewCameraLocation;
}
