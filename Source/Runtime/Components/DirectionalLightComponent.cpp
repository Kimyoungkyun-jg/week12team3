#include "EnginePCH.h"
#include "DirectionalLightComponent.h"

#include "Engine/LightSceneProxy.h"

UDirectionalLightComponent::UDirectionalLightComponent()
{

}

UDirectionalLightComponent::~UDirectionalLightComponent()
{
}

void UDirectionalLightComponent::SetAngularRadiusDegrees(float InDegrees)
{
	AngularRadiusDegrees = FMath::Clamp(InDegrees, 0.0f, 10.0f);
	MarkRenderStateDirty();
}

void UDirectionalLightComponent::SetDynamicShadowDistanceMovableLight(float NewValue)
{
	DynamicShadowDistanceMovableLight = std::max(NewValue, 0.0f);
	MarkRenderStateDirty();
}

void UDirectionalLightComponent::SetDynamicShadowCascades(uint32 NewValue)
{
	DynamicShadowCascades = std::clamp(NewValue, 1u, MaxDynamicShadowCascades);
	MarkRenderStateDirty();
}

void UDirectionalLightComponent::SetCascadeDistributionExponent(float NewValue)
{
	CascadeDistributionExponent = FMath::Clamp(NewValue, 1.0f, 4.0f);
	MarkRenderStateDirty();
}

void UDirectionalLightComponent::SetCascadeTransitionFraction(float NewValue)
{
	CascadeTransitionFraction = FMath::Clamp(NewValue, 0.0f, 0.3f);
	MarkRenderStateDirty();
}

void UDirectionalLightComponent::SetShadowDistanceFadeoutFraction(float NewValue)
{
	ShadowDistanceFadeoutFraction = FMath::Clamp(NewValue, 0.0f, 1.0f);
	MarkRenderStateDirty();
}

void UDirectionalLightComponent::PostEditChangeProperty(const FProperty& Property)
{
	if (Property.Name == "AngularRadiusDegrees")
		SetAngularRadiusDegrees(AngularRadiusDegrees);
	else if (Property.Name == "DynamicShadowDistanceMovableLight")
		SetDynamicShadowDistanceMovableLight(DynamicShadowDistanceMovableLight);
	else if (Property.Name == "DynamicShadowCascades")
		SetDynamicShadowCascades(DynamicShadowCascades);
	else if (Property.Name == "CascadeDistributionExponent")
		SetCascadeDistributionExponent(CascadeDistributionExponent);
	else if (Property.Name == "CascadeTransitionFraction")
		SetCascadeTransitionFraction(CascadeTransitionFraction);
	else if (Property.Name == "ShadowDistanceFadeoutFraction")
		SetShadowDistanceFadeoutFraction(ShadowDistanceFadeoutFraction);
	Super::PostEditChangeProperty(Property);
}

FLightSceneProxy* UDirectionalLightComponent::CreateSceneProxy() const
{
	FDirectionalLightProxyData Data;
	FillBaseProxyData(Data);
	Data.AngularRadiusDegrees = AngularRadiusDegrees;
	Data.DynamicShadowDistanceMovableLight = DynamicShadowDistanceMovableLight;
	Data.DynamicShadowCascades = DynamicShadowCascades;
	Data.CascadeDistributionExponent = CascadeDistributionExponent;
	Data.CascadeTransitionFraction = CascadeTransitionFraction;
	Data.ShadowDistanceFadeoutFraction = ShadowDistanceFadeoutFraction;
	return new FDirectionalLightSceneProxy(Data);
}

void UDirectionalLightComponent::UpdateSceneProxy()
{
	if (auto* Proxy = static_cast<FDirectionalLightSceneProxy*>(SceneProxy))
	{
		FDirectionalLightProxyData Data;
		FillBaseProxyData(Data);
		Data.AngularRadiusDegrees = AngularRadiusDegrees;
		Data.DynamicShadowDistanceMovableLight = DynamicShadowDistanceMovableLight;
		Data.DynamicShadowCascades = DynamicShadowCascades;
		Data.CascadeDistributionExponent = CascadeDistributionExponent;
		Data.CascadeTransitionFraction = CascadeTransitionFraction;
		Data.ShadowDistanceFadeoutFraction = ShadowDistanceFadeoutFraction;
		Proxy->SetProxyData(Data);
	}
}
