#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"

namespace
{
	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPrimitiveComponent::OnUnregister()
{
	Super::OnUnregister();
	// 삭제/등록 해제된 도형을 상대의 OverlapInfos에 남기지 않는다.
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other) Other->EndComponentOverlap(this);
	}
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	return Super::MoveComponent(Delta, NewRotation, bSweep, Hit);
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	// USceneComponent가 Transform 변경 시 MarkTransformDirty로 프록시를 갱신한다
	Super::PostEditChangeProperty(Property);

	if (Property.Name == "bCastShadow" ||
		Property.Name == "bReceiveShadow" ||
		Property.Name == "bReceiveDecal")
	{
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetReceiveDecal(bool NewValue)
{
	if (bReceiveDecal == NewValue)
		return;

	bReceiveDecal = NewValue;
	MarkRenderStateDirty();
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::PushTransformToProxy()
{
	if (SceneProxy)
		SceneProxy->ApplyTransformData(BuildTransformData());
}

void UPrimitiveComponent::PushRenderStateToProxy()
{
	if (SceneProxy)
		SceneProxy->ApplyBaseRenderState(BuildBaseRenderState());
}

FPrimitiveTransformData UPrimitiveComponent::BuildTransformData() const
{
	FPrimitiveTransformData Data;
	Data.LocalToWorld = GetWorldMatrix();
	Data.WorldToLocal = Data.LocalToWorld.Inverse();
	const FBox WorldBox = CalcLocalBounds().GetWorldAABB(Data.LocalToWorld);
	Data.WorldBounds.Center = (WorldBox.Min + WorldBox.Max) * 0.5f;
	Data.WorldBounds.Extent = (WorldBox.Max - WorldBox.Min) * 0.5f;
	Data.WorldScale3D = GetWorldScale3D();
	return Data;
}

FPrimitiveRenderStateData UPrimitiveComponent::BuildBaseRenderState() const
{
	FPrimitiveRenderStateData Data;
	Data.bVisible = IsVisible();
	Data.bShownInGame = IsShown(true);
	Data.bShownInEditor = IsShown(false);
	Data.bCastsShadow = bCastShadow;
	Data.bReceiveShadow = bReceiveShadow;
	Data.bReceiveDecal = bReceiveDecal;
	return Data;
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;
	FVector LocalNormal;

	if (!RayIntersectsMesh(LocalRay, Mesh, T, &LocalNormal)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
	FVector WorldNormal = WorldMatrix.Inverse().GetTransposed().TransformVector(LocalNormal).GetSafeNormal();
	if (WorldNormal.Dot(WorldRay.Direction) > 0.0f) WorldNormal = -WorldNormal;
	OutResult.ImpactNormal = OutResult.Normal = WorldNormal;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	if (!Other) return false;

	for (const FOverlapInfo& Info : OverlapInfos)
	{
		if (Info.OtherComp == Other)
			return true;
	}

	return false;
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp || OtherComp == this || IsOverlappingComponent(OtherComp))
	{
		return;
	}

	OverlapInfos.Add({ OtherComp });

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentBeginOverlap.Broadcast(this, OtherActor, OtherComp);
	if (Owner && OtherActor)
	{
		Owner->NotifyActorBeginOverlap(OtherActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) return;

	bool bRemoved = false;

	for (int32 Index = 0;Index < OverlapInfos.Num(); ++Index)
	{
		if (OverlapInfos[Index].OtherComp == OtherComp)
		{
			OverlapInfos.RemoveAt(Index, 1);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
		return;


	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyActorEndOverlap(OtherActor);
	}
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) 
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentHit.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyHit(OtherActor);
	}
}
