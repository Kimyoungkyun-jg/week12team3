#include "EnginePCH.h"
#include "DecalComponent.h"

#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Render/LineBatcher.h"

void UDecalComponent::TickComponent(float DeltaTime)
{
	const float FadeInEnd = Delay + FadeIn;
	const float VisibleEnd = FadeInEnd + Visible;
	const float FadeOutEnd = VisibleEnd + FadeOut;

	if (CurrentTime > FadeOutEnd)
	{
		return;
	}

	CurrentTime += DeltaTime;

	float t = 0.0f;
	if (CurrentTime < Delay) {
		t = 0.0f;
	}
	else if (CurrentTime < FadeInEnd) {
		t = (CurrentTime - Delay) / FadeIn;
	}
	else if (CurrentTime < VisibleEnd) {
		t = 1.0f;
	}
	else if (CurrentTime < FadeOutEnd) {
		t = 1 - (CurrentTime - VisibleEnd) / FadeOut;
	}
	else {
		t = 0.0f;
	}
	Fade = Ease(t);
}

void UDecalComponent::SetDecalViewProj()
{
	FVector Forward, Up;
	const FMatrix World = this->GetWorldMatrix();
	Forward = FVector(World[0][0], World[0][1], World[0][2]);
	Up = FVector(World[2][0], World[2][1], World[2][2]);
	Forward = Forward.Normalized();
	Up=  Up.Normalized();

	FMatrix View = LookAt(this->GetWorldLocation(), this->GetWorldLocation() + Forward, Up);
	FMatrix Proj;
	
	if (bPerspective) {
		const float FovV = 2.0f * std::atan(Extent.Z / Extent.X);
		Proj = Perspective(FovV, Extent.Y / Extent.Z, 0.1f, Extent.X);
	}
	else
		Proj = Ortho(Extent.Y * 2, Extent.Y / Extent.Z, -Extent.X, Extent.X);
	DecalViewProj = View * Proj;
}

FAABB UDecalComponent::GetWorldAABB() const
{
	if (bPerspective) {
		FVector Corners[8];
		GetWorldCorners(Corners);

		FBox Box{ Corners[0], Corners[0] };
		for (int32 i = 1; i < 8; i++) {
			Box.Expand(FBox{ Corners[i], Corners[i] });
		}

		return MakeWorldBounds(Box);
	}

	const FVector Scale = GetWorldScale3D();
	const FBox Local { -Extent / Scale, Extent / Scale };
	return MakeWorldBounds(Local.GetWorldAABB(GetWorldMatrix()));
}

FOBB UDecalComponent::GetWorldOBB() const
{
	const FMatrix World = GetWorldMatrix();
	FOBB OBB;
	OBB.Axis[0] = FVector(World[0][0], World[0][1], World[0][2]).Normalized();
	OBB.Axis[1] = FVector(World[1][0], World[1][1], World[1][2]).Normalized();
	OBB.Axis[2] = FVector(World[2][0], World[2][1], World[2][2]).Normalized();

	if (bPerspective) {
		FVector Corners[8];
		GetWorldCorners(Corners);

		FVector Center(0.f, 0.f, 0.f);
		float Half[3];
		for (int32 axis = 0; axis < 3; axis++) {
			float Min = Corners[0] | OBB.Axis[axis];
			float Max = Min;
			// Corners와 Axis 내적의 최대 최소값 구하기
			for (int32 i = 1; i < 8; i++) {
				const float D = Corners[i] | OBB.Axis[axis];
				Min = std::min(Min, D);
				Max = std::max(Max, D);
			}
			Center = Center + OBB.Axis[axis] * (Min + Max) * 0.5f;
			Half[axis] = (Max - Min) * 0.5f;
		} 

		OBB.Center = Center;
		OBB.Extent = FVector(Half[0], Half[1], Half[2]);
		return OBB;
	}

	OBB.Center = GetWorldLocation();
	OBB.Extent = Extent;

	return OBB;
}

void UDecalComponent::GetWorldCorners(FVector OutCorners[8]) const
{
	const FMatrix InvVP = DecalViewProj.Inverse();

	for (int32 i = 0; i < 8; i++) {
		const FVector4 NDC = FVector4((i & 1) ? 1.f : -1.f, 
									(i & 2) ? 1.f : -1.f, 
									(i & 4) ? 1.f : 0.f, 
									1.f);
		const FVector4 World = NDC * InvVP;
		OutCorners[i] = FVector(World.X, World.Y, World.Z) / World.W;
	}
}

void UDecalComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (LineBatcher == nullptr) return;

	FVector Corners[8];
	GetWorldCorners(Corners);

	const FVector4 Color(1.f, 0.6f, 0.2f, 1.f);

	for (int32 i = 0; i < 8; i++) {
		if ((i & 1) == 0) LineBatcher->AddLine(Corners[i], Corners[i + 1], Color);
		if ((i & 2) == 0) LineBatcher->AddLine(Corners[i], Corners[i + 2], Color);
		if ((i & 4) == 0) LineBatcher->AddLine(Corners[i], Corners[i + 4], Color);
	}

	const FAABB AABB = GetWorldAABB();
	LineBatcher->AddBox(FBox{ AABB.Center - AABB.Extent, AABB.Center + AABB.Extent }, FVector4(0.3f, 0.8f, 1.f, 1.f));
}

void UDecalComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddDecal(this);
}

void UDecalComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveDecal(this);
	Super::DestroyRenderState();
}

float UDecalComponent::Ease(float t) const
{
	return FMath::CubicInterp(0.0f, 0.0f, 1.0f, 0.0f, t);
}
