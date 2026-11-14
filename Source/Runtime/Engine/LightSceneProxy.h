#pragma once

#include "SceneManagement.h"
#include "Render/LightRenderData.h"
#include "Render/LightSceneInfo.h"
#include "Render/SceneView.h"
#include "Math/Color.h"
#include "Math/Rotator.h"
#include "Math/Sphere.h"
#include <cstdint>

struct FLightProxyBaseData
{
	uintptr_t ComponentKey = 0;
	FVector WorldLocation = FVector::ZeroVector;
	FRotator WorldRotation{};
	FVector Forward = FVector(1.0f, 0.0f, 0.0f);
	FVector Right = FVector(0.0f, 1.0f, 0.0f);
	FColor LightColor = FColor(255, 255, 255, 255);
	float Intensity = 1.0f;
	bool bVisible = true;
	bool bHiddenInGame = false;
	bool bCastShadows = true;
	float ShadowResolutionScale = 1.0f;
	float ShadowBias = 0.5f;
	float ShadowSlopeBias = 0.5f;
	float ShadowSharpen = 0.0f;
};

struct FDirectionalLightProxyData : FLightProxyBaseData
{
	float AngularRadiusDegrees = 0.27f;
	float DynamicShadowDistanceMovableLight = 200.0f;
	uint32 DynamicShadowCascades = 4;
	float CascadeDistributionExponent = 3.0f;
	float CascadeTransitionFraction = 0.1f;
	float ShadowDistanceFadeoutFraction = 0.1f;
};

struct FPointLightProxyData : FLightProxyBaseData
{
	float AttenuationRadius = 5.0f;
	float FalloffExponent = 1.0f;
	float SourceRadius = 0.02f;
};

struct FSpotLightProxyData : FPointLightProxyData
{
	float InnerConeAngle = 15.0f;
	float OuterConeAngle = 30.0f;
};

struct FRectLightProxyData : FLightProxyBaseData
{
	float AttenuationRadius = 5.0f;
	float HalfWidth = 3.0f;
	float HalfHeight = 3.0f;
	float BarnDoorAngle = 88.0f;
	float BarnDoorLength = 2.0f;
};

// MSVC 최적화
// 가상함수 테이블과 패딩 방지
class FLightSceneProxyFixLayout
{
public:
	virtual ~FLightSceneProxyFixLayout() = default;
};

class FLightSceneProxy : public FLightSceneProxyFixLayout
{
public:
	explicit FLightSceneProxy(const FLightProxyBaseData& InBaseData)
		: BaseData(InBaseData) {}
	virtual ~FLightSceneProxy() = default;

	virtual void GetLightShaderParameters(FLightRenderParameters& OutParameters) const = 0;

	uintptr_t GetComponentKey() const { return BaseData.ComponentKey; }
	bool IsVisible() const { return BaseData.bVisible; }
	bool IsEnabled() const { return BaseData.Intensity > 0.0f; }
	bool IsShown(bool bGameView) const { return BaseData.bVisible && !(bGameView && BaseData.bHiddenInGame); }

	// 에디터 입력 범위는 프록시 조회 시점에 맞춰 사용한다
	FLightShadowSettings GetShadowSettings() const
	{
		FLightShadowSettings Settings{};
		Settings.bCastShadows = BaseData.bCastShadows;
		Settings.ResolutionScale = FMath::Clamp(BaseData.ShadowResolutionScale, 0.0625f, 4.0f);
		Settings.Bias = std::max(BaseData.ShadowBias, 0.0f);
		Settings.SlopeBias = std::max(BaseData.ShadowSlopeBias, 0.0f);
		Settings.Sharpen = FMath::Clamp(BaseData.ShadowSharpen, 0.0f, 1.0f);
		return Settings;
	}

	bool CastsDynamicShadow() const { return BaseData.bCastShadows; }
	float GetUserShadowBias() const { return std::max(BaseData.ShadowBias, 0.0f); }
	float GetUserShadowSlopeBias() const { return std::max(BaseData.ShadowSlopeBias, 0.0f); }
	float GetShadowResolutionScale() const { return FMath::Clamp(BaseData.ShadowResolutionScale, 0.0625f, 4.0f); }
	float GetShadowSharpen() const { return FMath::Clamp(BaseData.ShadowSharpen, 0.0f, 1.0f); }

	virtual bool IsDirectional() const { return false; }
	// 라이트가 영향을 주는 공간. 채널 배정, 뷰 컬링, Caster 판정에 쓴다.
	virtual FSphere GetBoundingSphere() const { return FSphere(FVector::ZeroVector, WORLD_MAX); }
	virtual bool AffectsBounds(const FSphere& Bounds) const
	{
		const FSphere LightBounds = GetBoundingSphere();
		const float Reach = LightBounds.Radius + Bounds.Radius;
		return (LightBounds.Center - Bounds.Center).SizeSquared() <= Reach * Reach;
	}
	// PCSS에 쓰는 광원의 겉보기 각도(도). Directional만 의미가 있다.
	virtual float GetLightSourceAngle() const { return 0.0f; }

	// 카메라와 무관한 그림자(Spot 1장, Point 6장)의 Initializer를 채운다.
	virtual bool GetWholeSceneProjectedShadowInitializer(const FSceneView& View,
		TArray<FWholeSceneProjectedShadowInitializer>& OutInitializers) const
	{
		return false;
	}

	// 카메라를 따라가는 그림자(Directional CSM)
	virtual uint32 GetNumViewDependentWholeSceneShadows(const FSceneView& View) const { return 0; }
	virtual bool GetViewDependentWholeSceneProjectedShadowInitializer(const FSceneView& View, int32 InCascadeIndex,
		FWholeSceneProjectedShadowInitializer& OutInitializer) const
	{
		return false;
	}
	virtual FSphere GetShadowSplitBounds(const FSceneView& View, int32 InCascadeIndex, FShadowCascadeSettings* OutCascadeSettings) const
	{
		return FSphere(FVector::ZeroVector, 0.0f);
	}

	FLightSceneInfo* GetLightSceneInfo() const { return LightSceneInfo; }

protected:
	FLightProxyBaseData BaseData{};

private:
	friend class FScene;
	FScene* Scene = nullptr;
	int32 PackedIndex = INDEX_NONE;
	FLightSceneInfo* LightSceneInfo = nullptr;
};

class FDirectionalLightSceneProxy : public FLightSceneProxy
{
public:
	explicit FDirectionalLightSceneProxy(const FDirectionalLightProxyData& InData)
		: FLightSceneProxy(InData), Data(InData) {}
	void SetProxyData(const FDirectionalLightProxyData& InData) { BaseData = InData; Data = InData; }
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;

	bool IsDirectional() const override { return true; }
	float GetLightSourceAngle() const override;

	uint32 GetNumViewDependentWholeSceneShadows(const FSceneView& View) const override;
	bool GetViewDependentWholeSceneProjectedShadowInitializer(const FSceneView& View, int32 InCascadeIndex,
		FWholeSceneProjectedShadowInitializer& OutInitializer) const override;
	FSphere GetShadowSplitBounds(const FSceneView& View, int32 InCascadeIndex, FShadowCascadeSettings* OutCascadeSettings) const override;

	// 카메라에서 SplitIndex번째 캐스케이드 경계까지의 거리
	float GetSplitDistance(const FSceneView& View, uint32 SplitIndex) const;

	static constexpr uint32 MaxDynamicShadowCascades = 4;

private:
	FDirectionalLightProxyData Data{};
};

class FLocalLightSceneProxy : public FLightSceneProxy
{
public:
	using FLightSceneProxy::FLightSceneProxy;
};

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
	explicit FPointLightSceneProxy(const FPointLightProxyData& InData)
		: FLocalLightSceneProxy(InData), Data(InData) {}
	void SetProxyData(const FPointLightProxyData& InData) { BaseData = InData; Data = InData; }
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;

	FSphere GetBoundingSphere() const override;
	bool GetWholeSceneProjectedShadowInitializer(const FSceneView& View,
		TArray<FWholeSceneProjectedShadowInitializer>& OutInitializers) const override;

private:
	FPointLightProxyData Data{};
};

class FSpotLightSceneProxy : public FLocalLightSceneProxy
{
public:
	explicit FSpotLightSceneProxy(const FSpotLightProxyData& InData)
		: FLocalLightSceneProxy(InData), Data(InData) {}
	void SetProxyData(const FSpotLightProxyData& InData) { BaseData = InData; Data = InData; }
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;

	FSphere GetBoundingSphere() const override;
	bool GetWholeSceneProjectedShadowInitializer(const FSceneView& View,
		TArray<FWholeSceneProjectedShadowInitializer>& OutInitializers) const override;

private:
	FSpotLightProxyData Data{};
};

class FRectLightSceneProxy : public FLocalLightSceneProxy
{
public:
	explicit FRectLightSceneProxy(const FRectLightProxyData& InData)
		: FLocalLightSceneProxy(InData), Data(InData) {}
	void SetProxyData(const FRectLightProxyData& InData) { BaseData = InData; Data = InData; }
	void GetLightShaderParameters(FLightRenderParameters& Out) const override;

private:
	FRectLightProxyData Data{};
};

class FAmbientLightSceneProxy
{
public:
	bool IsEnabled() const { return false; }
	bool IsShown(bool) const { return false; }
	const FAmbientLightRenderData& GetRenderData() const { return RenderData; }

private:
	FAmbientLightRenderData RenderData{};
};

class FSkyLightSceneProxy
{
public:
	bool IsEnabled() const { return false; }
	bool IsShown(bool) const { return false; }
	const FSkyLightRenderData& GetRenderData() const { return RenderData; }

private:
	FSkyLightRenderData RenderData{};
};
