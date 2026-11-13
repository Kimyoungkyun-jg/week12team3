#include "EnginePCH.h"
#include "Components/SceneComponent.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

namespace
{
	FRotator RotationFromMatrix(const FMatrix& Matrix)
	{
		const float Horizontal = sqrtf(Matrix[0][0] * Matrix[0][0] + Matrix[0][1] * Matrix[0][1]);
		if (Horizontal > 1.0e-6f) return MatrixToRotator(Matrix);
		// Pitch가 ±90도일 때 Roll·Yaw를 각각 atan2(0, 0)로 구하면 원래 방향을 잃는다.
		return FRotator(FMath::RadiansToDegrees(atan2f(-Matrix[0][2], Horizontal)),
			FMath::RadiansToDegrees(atan2f(-Matrix[1][0], Matrix[1][1])), 0.0f);
	}

	FVector DivideScale(const FVector& Scale, const FVector& ParentScale, const FVector& Fallback)
	{
		FVector Result = Fallback;
		for (int Axis = 0; Axis < 3; ++Axis)
			if (std::fabs(ParentScale.V[Axis]) > SMALL_NUMBER)
				Result.V[Axis] = Scale.V[Axis] / ParentScale.V[Axis];
		return Result;
	}

	void UpdateChildVisibility(USceneComponent* Root, bool bPropagate, bool bVisible, bool bHidden)
	{
		TArray<USceneComponent*> Pending = Root->GetAttachChildren();
		while (!Pending.IsEmpty())
		{
			USceneComponent* Child = Pending[Pending.Num() - 1];
			Pending.RemoveLast();
			if (!Child) continue;
			if (bPropagate)
			{
				if (bVisible) Child->UActorComponent::SetVisibility(Root->IsVisible());
				if (bHidden) Child->UActorComponent::SetHiddenInGame(Root->IsHiddenInGame());
			}
			Child->MarkRenderStateDirty();
			for (USceneComponent* Descendant : Child->GetAttachChildren()) Pending.Add(Descendant);
		}
	}
}

USceneComponent::~USceneComponent()
{
	TArray<USceneComponent*> Children = AttachChildren;
	DetachFromParent();
	for (USceneComponent* Child : Children)
	{
		Child->AttachParent = nullptr;
	}
	AttachChildren.Reset();

	DetachFromParent();
}

void USceneComponent::SetRelativeLocationAndRotation(const FVector& NewLocation, const FRotator& NewRotation)
{
	Transform.Location = NewLocation;
	Transform.Rotation = NewRotation;
	MarkTransformDirty();
}

void USceneComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "Transform") 
		MarkTransformDirty();
	else if (Property.Name == "bVisible" || Property.Name == "bHiddenInGame")
		UpdateChildVisibility(this, false, false, false);
}

bool USceneComponent::IsShown(bool bGameView) const
{
	for (const USceneComponent* Component = this; Component; Component = Component->AttachParent)
		if (!Component->UActorComponent::IsShown(bGameView)) return false;
	return true;
}

void USceneComponent::SetVisibility(bool bNewVisibility, EVisibilityPropagation Propagation)
{
	const bool bChanged = bVisible != bNewVisibility;
	Super::SetVisibility(bNewVisibility);
	if (bChanged || Propagation == EVisibilityPropagation::Propagate)
		UpdateChildVisibility(this, Propagation == EVisibilityPropagation::Propagate, true, false);
}

void USceneComponent::SetHiddenInGame(bool bNewHidden, EVisibilityPropagation Propagation)
{
	const bool bChanged = bHiddenInGame != bNewHidden;
	Super::SetHiddenInGame(bNewHidden);
	if (bChanged || Propagation == EVisibilityPropagation::Propagate)
		UpdateChildVisibility(this, Propagation == EVisibilityPropagation::Propagate, false, true);
}

void USceneComponent::SetupAttachment(USceneComponent* InParent, EAttachmentRule Rule)
{
	if (InParent == this || AttachParent == InParent) return;

	// 순환 체크
	for (USceneComponent* Parent = InParent; Parent != nullptr; Parent = Parent->AttachParent)
		if (Parent == this) return;

	if (Rule == EAttachmentRule::KeepWorld)
	{
		if (InParent)
		{
			const FVector ParentScale = InParent->GetWorldScale3D();
			// 스케일이 0인 부모에는 기존 월드 TRS를 나타낼 수 없으므로 원래 계층을 유지한다.
			for (int Axis = 0; Axis < 3; ++Axis)
				if (std::fabs(ParentScale.V[Axis]) <= SMALL_NUMBER) return;
		}
		const FVector WorldLocation = GetWorldLocation();
		const FQuat WorldRotation = GetWorldRotationQuat();
		const FVector WorldScale = GetWorldScale3D();
		DetachFromParent(EAttachmentRule::KeepRelative);
		AttachParent = InParent;
		if (AttachParent)
		{
			AttachParent->AttachChildren.Add(this);
		}
		ApplyWorldTRS(WorldLocation, WorldRotation, WorldScale);
	}
	else
	{
		DetachFromParent(Rule);
		AttachParent = InParent;
		if (AttachParent)
		{
			AttachParent->AttachChildren.Add(this);
		}
	}

	if (Rule == EAttachmentRule::SnapToTarget)
	{
		SetTransform(FTransform::Identity);
	}

	MarkTransformDirty();
	MarkRenderStateDirty();
	UpdateChildVisibility(this, false, false, false);
}

void USceneComponent::DetachFromParent(EAttachmentRule Rule)
{
	if (!AttachParent) return;

	const bool bKeepWorld = Rule == EAttachmentRule::KeepWorld;
	const FVector WorldLocation = bKeepWorld ? GetWorldLocation() : FVector();
	const FQuat WorldRotation = bKeepWorld ? GetWorldRotationQuat() : FQuat();
	const FVector WorldScale = bKeepWorld ? GetWorldScale3D() : FVector();
	TArray<USceneComponent*>& Siblings = AttachParent->AttachChildren;
	for (uint32 i = 0; i < Siblings.Num(); ++i)
	{
		if (Siblings[i] == this)
		{
			Siblings.RemoveAt(i, 1);
			break;
		}
	}
	AttachParent = nullptr;
	if (bKeepWorld)
		ApplyWorldTRS(WorldLocation, WorldRotation, WorldScale);
	
	MarkTransformDirty();
	MarkRenderStateDirty();
	UpdateChildVisibility(this, false, false, false);
}

FQuat USceneComponent::GetWorldRotationQuat() const
{
	if (AttachParent)
	{
		return (AttachParent->GetWorldRotationQuat() * Transform.GetOrientation()).Normalized();
	}

	return Transform.GetOrientation();
}

FRotator USceneComponent::GetWorldRotation() const
{
	return AttachParent ? GetWorldRotationQuat().ToFRotator() : Transform.Rotation;
}

FVector USceneComponent::GetWorldLocation() const
{
	FMatrix WorldMatrix = GetWorldMatrix();

	return FVector(WorldMatrix[3][0], WorldMatrix[3][1], WorldMatrix[3][2]);
}

FVector USceneComponent::GetWorldScale3D() const
{
	if (AttachParent)
	{
		FVector ParentScale = AttachParent->GetWorldScale3D();

		return FVector(
			Transform.Scale.X * ParentScale.X,
			Transform.Scale.Y * ParentScale.Y,
			Transform.Scale.Z * ParentScale.Z
		);
	}

	return Transform.Scale;
}

FMatrix USceneComponent::GetWorldMatrix() const
{
	if (!AttachParent)
	{
		return Transform.GetLocalMatrix();
	}

	const FVector WorldLocation = 
		AttachParent->GetWorldMatrix().TransformPosition(Transform.Location);

	const FQuat WorldRotation = 
		GetWorldRotationQuat();

	const FVector WorldScale =
		Transform.Scale * AttachParent->GetWorldScale3D();

	// Keep the composed rotation as a quaternion: editor arrows have a local 90-degree
	// pitch, where an Euler round trip makes their world direction unstable.
	FMatrix WorldMatrix = WorldRotation.ToFMatrix();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (int32 Component = 0; Component < 3; ++Component)
		{
			WorldMatrix[Axis][Component] *= WorldScale.V[Axis];
		}
	}
	WorldMatrix.SetOrigin(WorldLocation);
	return WorldMatrix;
}

void USceneComponent::SetWorldTransform(const FMatrix& InWorldMatrix, bool bSweep)
{
	// 월드 TRS를 먼저 분리한다. World * Parent^-1을 분해하면 비균일 부모 스케일이
	// 상대 회전에 섞여, GetWorldMatrix가 쓰는 성분별 스케일 합성을 역산하지 못한다.
	FVector Axes[3] = {
		FVector(InWorldMatrix[0][0], InWorldMatrix[0][1], InWorldMatrix[0][2]),
		FVector(InWorldMatrix[1][0], InWorldMatrix[1][1], InWorldMatrix[1][2]),
		FVector(InWorldMatrix[2][0], InWorldMatrix[2][1], InWorldMatrix[2][2])
	};
	FVector WorldScale(Axes[0].Size(), Axes[1].Size(), Axes[2].Size());
	// 행렬만 받은 경우 반사 부호를 X축에 둬 오른손 회전 기저를 복원한다.
	if (Axes[0].Dot(Axes[1].Cross(Axes[2])) < 0.0f)
		WorldScale.X = -WorldScale.X;

	FMatrix RotationMatrix = FMatrix::Identity;
	for (int Axis = 0; Axis < 3; ++Axis)
		if (std::fabs(WorldScale.V[Axis]) > SMALL_NUMBER)
			RotationMatrix.SetAxis(Axis, Axes[Axis] / WorldScale.V[Axis]);
	ApplyWorldTRS(FVector(InWorldMatrix[3][0], InWorldMatrix[3][1], InWorldMatrix[3][2]),
		RotationFromMatrix(RotationMatrix).Quaternion(), WorldScale);
}

void USceneComponent::ApplyWorldTRS(const FVector& Location, const FQuat& Rotation, const FVector& Scale)
{
	if (AttachParent)
	{
		Transform.Location = AttachParent->GetWorldMatrix().Inverse().TransformPosition(Location);
		Transform.Rotation = RotationFromMatrix((AttachParent->GetWorldRotationQuat().Inverse() * Rotation).Normalized().ToFMatrix());
		Transform.Scale = DivideScale(Scale, AttachParent->GetWorldScale3D(), Transform.Scale);
	}
	else
	{
		Transform.Location = Location;
		Transform.Rotation = RotationFromMatrix(Rotation.ToFMatrix());
		Transform.Scale = Scale;
	}
	MarkTransformDirty();
}

void USceneComponent::SetWorldLocation(FVector NewLocation, bool bSweep)
{
	FVector NewRelLocation = NewLocation;

	if (GetAttachParent() != nullptr)
	{		
		NewRelLocation = GetAttachParent()->GetWorldMatrix().Inverse().TransformPosition(NewRelLocation);
	}

	SetRelativeLocation(NewRelLocation);
}

void USceneComponent::SetWorldRotation(FRotator NewRotation, bool bSweep)
{
	FRotator NewRelRotation = NewRotation;

	if (GetAttachParent() != nullptr)
	{
		FQuat NewRelQuat = GetAttachParent()->GetWorldRotationQuat().Inverse() * NewRelRotation.Quaternion();
		NewRelRotation = NewRelQuat.ToFRotator();
	}

	SetRelativeRotation(NewRelRotation);
}

void USceneComponent::SetWorldLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep)
{
	FVector NewRelLocation = NewLocation;
	FRotator NewRelRotation = NewRotation;

	if (GetAttachParent() != nullptr)
	{
		NewRelLocation = GetAttachParent()->GetWorldMatrix().Inverse().TransformPosition(NewRelLocation);

		FQuat NewRelQuat = GetAttachParent()->GetWorldRotationQuat().Inverse() * NewRelRotation.Quaternion();
		NewRelRotation = NewRelQuat.ToFRotator();
	}

	SetRelativeLocationAndRotation(NewRelLocation, NewRelRotation);
}

void USceneComponent::SetWorldScale3D(FVector NewScale)
{
	SetRelativeScale3D(AttachParent ? DivideScale(NewScale, AttachParent->GetWorldScale3D(), Transform.Scale) : NewScale);
}

bool USceneComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	FHitResult LocalHit(1.0f);
	FHitResult& Result = Hit ? *Hit : LocalHit;
	Result = FHitResult(1.0f);
	const FVector Start = GetWorldLocation();
	FVector Destination = Start + Delta;
	bool bBlocked = false;
	AActor* Owner = GetOwner();
	if (bSweep && Delta.SizeSquared() > SMALL_NUMBER && Owner && Owner->GetActorEnableCollision())
	{
		if (UWorld* World = Owner->GetWorld())
		{
			Result.Init(Start, Destination);
			bBlocked = World->SweepSingle(FRay(Start, Delta.Normalized()), Delta.Size(), GetCollisionRadius(), Result, Owner);
			if (bBlocked)
				Destination = Start + Delta * Result.Time + Result.ImpactNormal * 0.001f;
		}
	}
	SetWorldLocationAndRotation(Destination, NewRotation, false);
	return !bBlocked;
}

float USceneComponent::GetCollisionRadius() const
{
	if (CollisionRadiusOverride >= 0.0f) return CollisionRadiusOverride;
	const FBox Bounds = CalcBounds();
	const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
	if (Extent.SizeSquared() <= SMALL_NUMBER) return 10.0f;
	return std::min({ Extent.X, Extent.Y, Extent.Z });
}

void USceneComponent::MarkTransformDirty()
{
	OnTransformDirty();         

	for (USceneComponent* Child : AttachChildren)
		Child->MarkTransformDirty();      // 부모가 움직이면 자식의 월드 행렬도 바뀐다
}

void USceneComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // UActorComponent: Owner까지 처리됨

	FArchive& Ar = Record.GetUnderlyingArchive();
	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		UObject* ParentObj = AttachParent;
		Record << SA_VALUE("AttachParent", ParentObj);
		AttachParent = static_cast<USceneComponent*>(ParentObj);

		int32 Num = AttachChildren.Num();
		FStructuredArchive::FArray ChildArray = Record.EnterArray("AttachChildren", Num);
		if (Ar.IsLoading())
			AttachChildren.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = AttachChildren[i];
			ChildArray.EnterElement() << Obj;
			AttachChildren[i] = static_cast<USceneComponent*>(Obj);
		}
	}
}



