#include "EnginePCH.h"
#include "LightSceneProxy.h"
#include "Render/ShadowRendering.h"
#include "Components/LightComponentBase.h"

namespace
{
	// 캐스케이드 가중치가 1, e, e^2, ... 일 때 CascadeIndex 앞까지의 누적 비율 (0~1)
	float ComputeAccumulatedScale(float Exponent, int32 CascadeIndex, int32 CascadeCount)
	{
		if (CascadeIndex <= 0) return 0.0f;
		float CurrentScale = 1.0f;
		// 모든 캐스케이드 가중치의 합
		float TotalScale = 0.0f;
		// CascadeIndex 앞에 있는 캐스케이드 가중치의 합
		float Ret = 0.0f;
		for (int32 i = 0; i < CascadeCount; ++i)
		{
			if (i < CascadeIndex) Ret += CurrentScale;
			TotalScale += CurrentScale;
			CurrentScale *= Exponent;
		}
		return Ret / TotalScale;
	}

	// 카메라 프러스텀을 Distance(카메라 기준 깊이)에서 자른 단면의 네 코너
	void GetViewSliceCorners(const FSceneView& View, float Distance, FVector OutCorners[4])
	{
		const FFrustumCorners& C = View.FrustumCorners;
		const float T = (Distance - View.NearClip) / std::max(View.FarClip - View.NearClip, KINDA_SMALL_NUMBER);
		OutCorners[0] = C.NTL() + (C.FTL() - C.NTL()) * T;
		OutCorners[1] = C.NTR() + (C.FTR() - C.NTR()) * T;
		OutCorners[2] = C.NBL() + (C.FBL() - C.NBL()) * T;
		OutCorners[3] = C.NBR() + (C.FBR() - C.NBR()) * T;
	}
}

void FDirectionalLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const FLinearColor Color(BaseData.LightColor);

	Out = FLightRenderParameters{};
	Out.Type = static_cast<uint32>(ELightType::Directional);
	Out.Position = BaseData.WorldLocation;
	Out.Direction = BaseData.Forward;
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = BaseData.Intensity;
	Out.SourceSize = std::tan(FMath::DegreesToRadians(FMath::Clamp(Data.AngularRadiusDegrees, 0.0f, 10.0f)));
}

void FPointLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const FLinearColor Color(BaseData.LightColor);

	Out = FLightRenderParameters{};
	Out.Type = static_cast<uint32>(ELightType::Point);
	Out.Position = BaseData.WorldLocation;
	Out.Radius = Data.AttenuationRadius;
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = BaseData.Intensity;
	Out.FalloffExponent = Data.FalloffExponent;
	Out.SourceSize = Data.SourceRadius;
}

void FSpotLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const FLinearColor Color(BaseData.LightColor);
	const float Outer = FMath::Clamp(Data.OuterConeAngle, 0.0f, 89.0f);
	const float Inner = FMath::Clamp(Data.InnerConeAngle, 0.0f, Outer);

	Out = FLightRenderParameters{};
	Out.Position = BaseData.WorldLocation;
	Out.Radius = Data.AttenuationRadius;
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = BaseData.Intensity;
	Out.FalloffExponent = Data.FalloffExponent;

	Out.Type = static_cast<uint32>(ELightType::Spot);
	Out.Direction = BaseData.Forward;
	Out.InnerConeCos = std::cos(FMath::DegreesToRadians(Inner));
	Out.OuterConeCos = std::cos(FMath::DegreesToRadians(Outer));
	Out.SourceSize = Data.SourceRadius;
}

void FRectLightSceneProxy::GetLightShaderParameters(FLightRenderParameters& Out) const
{
	const FLinearColor Color(BaseData.LightColor);

	Out = FLightRenderParameters{};
	Out.Type = static_cast<uint32>(ELightType::Rect);
	Out.Position = BaseData.WorldLocation;
	Out.Radius = Data.AttenuationRadius;
	Out.Color = FVector(Color.R, Color.G, Color.B);
	Out.Intensity = BaseData.Intensity;
	Out.FalloffExponent = 1.0f;
	Out.Direction = BaseData.Forward;
	Out.Right = BaseData.Right;
	Out.HalfWidth = Data.HalfWidth;
	Out.HalfHeight = Data.HalfHeight;

	const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(Data.BarnDoorAngle, 0.0f, 90.0f));
	Out.BarnDoorCos = std::cos(AngleRad);
	Out.BarnDoorSin = std::sin(AngleRad);
	Out.BarnDoorLength = std::max(Data.BarnDoorLength, 0.0f);
}

// 그림자 Initializer

float FDirectionalLightSceneProxy::GetLightSourceAngle() const
{
	return 2.0f * FMath::Clamp(Data.AngularRadiusDegrees, 0.0f, 10.0f);
}

uint32 FDirectionalLightSceneProxy::GetNumViewDependentWholeSceneShadows(const FSceneView& View) const
{
	if (Data.DynamicShadowDistanceMovableLight <= 0.0f) return 0;
	const uint32 MaxCascades = std::clamp(GShadowCVars.CSMMaxCascades, 1u, MaxDynamicShadowCascades);
	return std::clamp(Data.DynamicShadowCascades, 1u, MaxCascades);
}

bool FDirectionalLightSceneProxy::GetViewDependentWholeSceneProjectedShadowInitializer(const FSceneView& View, int32 InCascadeIndex,
	FWholeSceneProjectedShadowInitializer& OutInitializer) const
{
	const FSphere Bounds = GetShadowSplitBounds(View, InCascadeIndex, &OutInitializer.CascadeSettings);
	const float R = Bounds.Radius;
	if (R <= 0.0f) return false;

	OutInitializer.PreShadowTranslation = Bounds.Center * -1.0f;
	OutInitializer.WorldToLight = BaseData.WorldRotation.Quaternion().ToFMatrix().GetTransposed();
	OutInitializer.Scales = FVector2(1.0f / R, 1.0f / R);
	OutInitializer.SubjectBounds = FBoxSphereBounds(FVector::ZeroVector, FVector(R), R);
	OutInitializer.WAxis = FVector4(0, 0, 0, 1);
	OutInitializer.MinLightW = -R * 2.0f;
	OutInitializer.MaxDistanceToCastInLightW = R;
	OutInitializer.bOnePassPointLightShadow = false;
	return true;
}

FSphere FDirectionalLightSceneProxy::GetShadowSplitBounds(const FSceneView& View, int32 InCascadeIndex, FShadowCascadeSettings* OutCascadeSettings) const
{
	const int32 NumCascades = static_cast<int32>(GetNumViewDependentWholeSceneShadows(View));

	const float SplitNear = GetSplitDistance(View, InCascadeIndex);
	float SplitFar = GetSplitDistance(View, InCascadeIndex + 1);
	const float FadePlane = SplitFar;

	const float TransitionFraction = FMath::Clamp(Data.CascadeTransitionFraction, 0.0f, 0.3f);
	const float FadeExtension = (SplitFar - SplitNear) * TransitionFraction;
	const bool bLastCascade = InCascadeIndex >= NumCascades - 1;
	if (!bLastCascade) SplitFar += FadeExtension;

	FVector Center = FVector::ZeroVector;
	float RadiusSquared = 0.0f;
	if (View.bIsPerspective)
	{
		const float TanX = 1.0f / std::max(std::abs(View.ProjectionMatrix.M[1][0]), KINDA_SMALL_NUMBER);
		const float TanY = 1.0f / std::max(std::abs(View.ProjectionMatrix.M[2][1]), KINDA_SMALL_NUMBER);
		const float DiagonalScaleSq = TanX * TanX + TanY * TanY;
		const float FarDiagonalSq = DiagonalScaleSq * SplitFar * SplitFar;
		const float NearDiagonalSq = DiagonalScaleSq * SplitNear * SplitNear;
		const float FrustumLength = std::max(SplitFar - SplitNear, KINDA_SMALL_NUMBER);
		const float OptimalOffset = (NearDiagonalSq - FarDiagonalSq) / (2.0f * FrustumLength) + FrustumLength * 0.5f;
		const float CentreZ = FMath::Clamp(SplitFar - OptimalOffset, SplitNear, SplitFar);
		Center = View.ViewLocation + View.ViewForward * CentreZ;
		RadiusSquared = std::max(FarDiagonalSq + (SplitFar - CentreZ) * (SplitFar - CentreZ),
			NearDiagonalSq + (SplitNear - CentreZ) * (SplitNear - CentreZ));
		RadiusSquared = std::max(RadiusSquared, 1.0f);
	}
	else
	{
		FVector Corners[8];
		GetViewSliceCorners(View, SplitNear, Corners);
		GetViewSliceCorners(View, SplitFar, Corners + 4);

		for (const FVector& P : Corners) Center += P;
		Center = Center * (1.0f / 8.0f);

		for (const FVector& P : Corners)
			RadiusSquared = std::max(RadiusSquared, (P - Center).SizeSquared());
	}

	if (OutCascadeSettings)
	{
		OutCascadeSettings->SplitNear = SplitNear;
		OutCascadeSettings->SplitFar = SplitFar;
		OutCascadeSettings->ShadowSplitIndex = InCascadeIndex;
		OutCascadeSettings->SplitFarFadeRegion = bLastCascade ? 0.0f : FadeExtension;
		const float PrevNear = InCascadeIndex > 0 ? GetSplitDistance(View, InCascadeIndex - 1) : SplitNear;
		OutCascadeSettings->SplitNearFadeRegion = InCascadeIndex > 0 ? (SplitNear - PrevNear) * TransitionFraction : 0.0f;

		if (bLastCascade)
		{
			const float Fadeout = (SplitFar - SplitNear) * FMath::Clamp(Data.ShadowDistanceFadeoutFraction, 0.0f, 1.0f);
			OutCascadeSettings->FadePlaneOffset = SplitFar - Fadeout;
		}
		else
		{
			OutCascadeSettings->FadePlaneOffset = FadePlane;
		}
		OutCascadeSettings->FadePlaneLength = SplitFar - OutCascadeSettings->FadePlaneOffset;
	}

	return FSphere(Center, std::sqrt(RadiusSquared));
}

float FDirectionalLightSceneProxy::GetSplitDistance(const FSceneView& View, uint32 SplitIndex) const
{
	const float ShadowNear = View.NearClip;
	const float ShadowFar = std::min(Data.DynamicShadowDistanceMovableLight, View.FarClip);
	const float Exponent = FMath::Clamp(Data.CascadeDistributionExponent, 1.0f, 4.0f);
	return ShadowNear + ComputeAccumulatedScale(Exponent, static_cast<int32>(SplitIndex),
		static_cast<int32>(GetNumViewDependentWholeSceneShadows(View))) * (ShadowFar - ShadowNear);
}

FSphere FPointLightSceneProxy::GetBoundingSphere() const
{
	return FSphere(BaseData.WorldLocation, Data.AttenuationRadius);
}

bool FPointLightSceneProxy::GetWholeSceneProjectedShadowInitializer(const FSceneView& View,
	TArray<FWholeSceneProjectedShadowInitializer>& OutInitializers) const
{
	const FVector LightPos = BaseData.WorldLocation;
	const float Radius = Data.AttenuationRadius;
	if (Radius <= 0.0f) return false;

	const FRotator Faces[6] =
	{
		FRotator{ 0, 0, 0 },
		FRotator{ 0, 180, 0 },
		FRotator{ 0, 90, 0 },
		FRotator{ 0, -90, 0 },
		FRotator{ 90, 0, 0 },
		FRotator{ -90, 0, 0 },
	};

	for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		FWholeSceneProjectedShadowInitializer& OutInitializer = OutInitializers.AddDefaulted_GetRef();
		OutInitializer.PreShadowTranslation = LightPos * -1.0f;
		OutInitializer.WorldToLight = Faces[FaceIndex].Quaternion().ToFMatrix().GetTransposed();
		OutInitializer.Scales = FVector2(1.0f, 1.0f);
		OutInitializer.SubjectBounds = FBoxSphereBounds(FVector::ZeroVector, FVector(Radius), Radius);
		OutInitializer.WAxis = FVector4(0, 0, 1, 0);
		OutInitializer.MinLightW = Radius * 0.02f;
		OutInitializer.MaxDistanceToCastInLightW = Radius;
		OutInitializer.CascadeSettings.ShadowSplitIndex = FaceIndex;
		OutInitializer.bOnePassPointLightShadow = false;
	}
	return true;
}

FSphere FSpotLightSceneProxy::GetBoundingSphere() const
{
	const float Radius = Data.AttenuationRadius;
	const float CosOuter = std::cos(FMath::DegreesToRadians(FMath::Clamp(Data.OuterConeAngle, 0.0f, 89.0f)));
	const float BoundsRadius = std::sqrt(1.25f * Radius * Radius - Radius * Radius * CosOuter);
	return FSphere(BaseData.WorldLocation + BaseData.Forward * (0.5f * Radius), BoundsRadius);
}

bool FSpotLightSceneProxy::GetWholeSceneProjectedShadowInitializer(const FSceneView& View,
	TArray<FWholeSceneProjectedShadowInitializer>& OutInitializers) const
{
	const FVector LightPos = BaseData.WorldLocation;
	const float Radius = Data.AttenuationRadius;
	if (Radius <= 0.0f) return false;

	const float Outer = FMath::Clamp(Data.OuterConeAngle, 1.0f, 89.0f);
	const float InvTan = 1.0f / std::tan(FMath::DegreesToRadians(Outer));

	FWholeSceneProjectedShadowInitializer& OutInitializer = OutInitializers.AddDefaulted_GetRef();
	OutInitializer.PreShadowTranslation = LightPos * -1.0f;
	OutInitializer.WorldToLight = BaseData.WorldRotation.Quaternion().ToFMatrix().GetTransposed();
	OutInitializer.Scales = FVector2(InvTan, InvTan);
	OutInitializer.SubjectBounds = FBoxSphereBounds(FVector::ZeroVector, FVector(Radius), Radius);
	OutInitializer.WAxis = FVector4(0, 0, 1, 0);
	OutInitializer.MinLightW = Radius * 0.02f;
	OutInitializer.MaxDistanceToCastInLightW = Radius;
	OutInitializer.bOnePassPointLightShadow = false;
	return true;
}
