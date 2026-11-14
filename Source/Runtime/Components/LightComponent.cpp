#include "EnginePCH.h"
#include "LightComponent.h"

#include "Engine/LightSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Components/BillboardComponent.h"
#include "GameFramework/Actor.h"

ULightComponent::ULightComponent() {}
ULightComponent::~ULightComponent() {}
void ULightComponent::DrawDebug(FLineBatcher* LineBatcher) const {}

void ULightComponent::FillBaseProxyData(FLightProxyBaseData& OutData) const
{
	FTransform Transform;
	Transform.Location = GetWorldLocation();
	Transform.Rotation = GetWorldRotation();
	OutData.ComponentKey = reinterpret_cast<uintptr_t>(this);
	OutData.WorldLocation = Transform.Location;
	OutData.WorldRotation = Transform.Rotation;
	OutData.Forward = Transform.GetForward();
	OutData.Right = Transform.GetRight();
	OutData.LightColor = GetLightColor();
	OutData.Intensity = GetIntensity();
	OutData.bVisible = IsShown(false);
	OutData.bHiddenInGame = !IsShown(true);
	OutData.bCastShadows = CastsShadows();
	OutData.ShadowResolutionScale = ShadowResolutionScale;
	OutData.ShadowBias = ShadowBias;
	OutData.ShadowSlopeBias = ShadowSlopeBias;
	OutData.ShadowSharpen = ShadowSharpen;
}

void ULightComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
	{
		UpdateSceneProxy();
	}
	if (AActor* Owner = GetOwner())
	{
		for (UActorComponent* Comp : Owner->GetComponents())
		{
			if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Comp))
				Billboard->MarkRenderStateDirty();
		}
	}
}

void ULightComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	MarkRenderStateDirty();
}

void ULightComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddLight(this);
}

void ULightComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveLight(this);
	Super::DestroyRenderState();
}
