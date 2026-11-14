#pragma once

#include "LightComponent.h"
#include "DirectionalLightComponent.generated.h"

UCLASS()
class UDirectionalLightComponent :public ULightComponent
{
	GENERATED_BODY()
public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override;

	virtual class FLightSceneProxy* CreateSceneProxy() const override;

	ELightType GetLightType() const override { return ELightType::Directional; }
	float GetAngularRadiusDegrees() const { return AngularRadiusDegrees; }
	void SetAngularRadiusDegrees(float InDegrees);
	void PostEditChangeProperty(const FProperty& Property) override;

	float GetDynamicShadowDistanceMovableLight() const { return DynamicShadowDistanceMovableLight; }
	uint32 GetDynamicShadowCascades() const { return DynamicShadowCascades; }
	float GetCascadeDistributionExponent() const { return CascadeDistributionExponent; }
	float GetCascadeTransitionFraction() const { return CascadeTransitionFraction; }
	float GetShadowDistanceFadeoutFraction() const { return ShadowDistanceFadeoutFraction; }

	void SetDynamicShadowDistanceMovableLight(float NewValue);
	void SetDynamicShadowCascades(uint32 NewValue);
	void SetCascadeDistributionExponent(float NewValue);
	void SetCascadeTransitionFraction(float NewValue);
	void SetShadowDistanceFadeoutFraction(float NewValue);

	static constexpr uint32 MaxDynamicShadowCascades = 4;

protected:
	void UpdateSceneProxy() override;

private:
	// 광원의 각반지름(도). PCSS 반그림자 폭 = tan(각반지름) x 가림막~수신점 거리.
	// 실제 태양은 약 0.27도 (언리얼 LightSourceAngle 기본 0.5357도는 지름).
	UPROPERTY(EditAnywhere)
	float AngularRadiusDegrees = 0.27f;

	// 카메라에서 이 거리(m)까지만 동적 그림자를 그린다. 0이면 동적 그림자를 끈다.
	// 언리얼 기본값 20000cm = 200m. 늘릴수록 모든 캐스케이드가 넓어져 가까운 그림자 텍셀이 커진다.
	UPROPERTY(EditAnywhere)
	float DynamicShadowDistanceMovableLight = 200.0f;
	// 캐스케이드 개수 (1 ~ MaxDynamicShadowCascades)
	UPROPERTY(EditAnywhere)
	uint32 DynamicShadowCascades = 4;
	// 1이면 균등 분할, 클수록 카메라 가까운 캐스케이드가 좁아진다.
	UPROPERTY(EditAnywhere)
	float CascadeDistributionExponent = 3.0f;
	// 인접 캐스케이드와 섞는 구간의 비율 (캐스케이드 길이 기준)
	UPROPERTY(EditAnywhere)
	float CascadeTransitionFraction = 0.1f;
	// 마지막 캐스케이드 끝에서 그림자를 서서히 끄는 구간의 비율
	UPROPERTY(EditAnywhere)
	float ShadowDistanceFadeoutFraction = 0.1f;
};
