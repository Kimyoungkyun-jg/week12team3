#include "EnginePCH.h"
#include "Engine/World.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "UObject/Class.h"
#include "UObject/Casts.h"
#include "Serialization/JsonArchive.h"
#include <algorithm>
#include <cmath>

namespace
{
	UParticleModule* FindLODModule(const TArray<UParticleModule*>& Modules, int32 SlotId)
	{
		for (UParticleModule* Module : Modules)
		{
			if (Module && Module->LODSlotId == SlotId)
			{
				return Module;
			}
		}
		return nullptr;
	}

	bool HasMatchingModuleSlots(const UParticleLODLevel& Base, const UParticleLODLevel& Other)
	{
		if (Base.Modules.Num() != Other.Modules.Num())
		{
			return false;
		}
		for (uint32 Index = 0; Index < Base.Modules.Num(); ++Index)
		{
			const UParticleModule* Module = Base.Modules[Index];
			if (!Module || !Other.Modules[Index] || Module->GetClass() != Other.Modules[Index]->GetClass())
			{
				return false;
			}
			for (uint32 Prior = 0; Prior < Index; ++Prior)
			{
				if (Base.Modules[Prior]->GetClass() == Module->GetClass())
				{
					return false;
				}
			}
		}
		return true;
	}

	uint8 NormalizedFloatToByte(float V)
	{
		return static_cast<uint8>(std::clamp(V, 0.0f, 1.0f) * 255.0f);
	}

	const FString& ExtractVectorParamName(const UDistributionVector* Dist)
	{
		static const FString EmptyName;
		if (const UDistributionVectorParticleParameter* Param = Cast<UDistributionVectorParticleParameter>(Dist))
			return Param->ParameterName;
		return EmptyName;
	}

	void CompileVectorToAtlasChannel(const UDistributionVector* Dist, FGpuDistributionBuildChannel& OutChannel)
	{
		if (!Dist)
		{
			OutChannel.Mode = 0;
			return;
		}
		if (!Dist->UsesCurveAtlas())
		{
			const FVector Val = Dist->GetValue(0.0f, nullptr, nullptr);
			OutChannel.Mode = 1;
			OutChannel.ConstantValue = FVector4(Val.X, Val.Y, Val.Z, 1.0f);
			OutChannel.ValueScale = OutChannel.ConstantValue;
			OutChannel.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			return;
		}
		TArray<FVector> VecSamples;
		Dist->SampleCurveVectors(FParticleCurveTexture::CurveSampleWidth, VecSamples);
		TArray<FVector4> RawSamples;
		RawSamples.SetNum(VecSamples.Num());
		for (int32 i = 0; i < VecSamples.Num(); ++i)
			RawSamples[i] = FVector4(VecSamples[i].X, VecSamples[i].Y, VecSamples[i].Z, 1.0f);
		TArray<FVector4> OptimizedSamples;
		OptimizeCurveAtlasSamples(RawSamples, 3, OptimizedSamples);
		if (OptimizedSamples.Num() <= 1)
		{
			const FVector4 ConstVal = OptimizedSamples.IsEmpty() ? FVector4(1.0f, 1.0f, 1.0f, 1.0f) : OptimizedSamples[0];
			OutChannel.Mode = 1;
			OutChannel.ConstantValue = ConstVal;
			OutChannel.ValueScale = ConstVal;
			OutChannel.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			return;
		}
		OutChannel.AtlasHandle = FParticleCurveTexture::Get().AllocateCurve(OptimizedSamples);
		OutChannel.Mode = OutChannel.AtlasHandle.IsValid() ? 2 : 0;
		OutChannel.ValueScale = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		OutChannel.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	}

	void CompileFloatToAtlasChannel(const UDistributionFloat* Dist, FGpuDistributionBuildChannel& OutChannel)
	{
		if (!Dist)
		{
			OutChannel.Mode = 0;
			return;
		}
		if (!Dist->UsesCurveAtlas())
		{
			const float Val = Dist->GetValue(0.0f, nullptr, nullptr);
			OutChannel.Mode = 1;
			OutChannel.ConstantValue = FVector4(Val, Val, Val, Val);
			OutChannel.ValueScale = OutChannel.ConstantValue;
			OutChannel.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			return;
		}
		TArray<float> FloatSamples;
		Dist->SampleCurveValues(FParticleCurveTexture::CurveSampleWidth, FloatSamples);
		TArray<FVector4> RawSamples;
		RawSamples.SetNum(FloatSamples.Num());
		for (int32 i = 0; i < FloatSamples.Num(); ++i)
			RawSamples[i] = FVector4(FloatSamples[i], FloatSamples[i], FloatSamples[i], FloatSamples[i]);
		TArray<FVector4> OptimizedSamples;
		OptimizeCurveAtlasSamples(RawSamples, 1, OptimizedSamples);
		if (OptimizedSamples.Num() <= 1)
		{
			const FVector4 ConstVal = OptimizedSamples.IsEmpty() ? FVector4(1.0f, 1.0f, 1.0f, 1.0f) : OptimizedSamples[0];
			OutChannel.Mode = 1;
			OutChannel.ConstantValue = ConstVal;
			OutChannel.ValueScale = ConstVal;
			OutChannel.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			return;
		}
		OutChannel.AtlasHandle = FParticleCurveTexture::Get().AllocateCurve(OptimizedSamples);
		OutChannel.Mode = OutChannel.AtlasHandle.IsValid() ? 2 : 0;
		OutChannel.ValueScale = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		OutChannel.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

FParticleEmitterBuildInfo::~FParticleEmitterBuildInfo()
{
	ReleaseAtlasHandles();
}

void FParticleEmitterBuildInfo::ReleaseAtlasHandles()
{
	if (FParticleCurveTexture::IsInitialized())
	{
		FParticleCurveTexture& Atlas = FParticleCurveTexture::Get();
		Atlas.FreeCurve(SizeScale.AtlasHandle);
		Atlas.FreeCurve(ColorOverLife.AtlasHandle);
		Atlas.FreeCurve(SubUV.AtlasHandle);
		Atlas.FreeCurve(VelocityScale.AtlasHandle);
		Atlas.FreeCurve(RotationRateScale.AtlasHandle);
	}
	else
	{
		SizeScale.AtlasHandle.Invalidate();
		ColorOverLife.AtlasHandle.Invalidate();
		SubUV.AtlasHandle.Invalidate();
		VelocityScale.AtlasHandle.Invalidate();
		RotationRateScale.AtlasHandle.Invalidate();
	}
}

void FParticleEmitterBuildInfo::Reset()
{
	ReleaseAtlasHandles();
	*this = FParticleEmitterBuildInfo{};
}

UParticleModuleLifetime::UParticleModuleLifetime()
{
	UDistributionFloatUniform* Dist = NewObject<UDistributionFloatUniform>(this);
	Dist->Min = 1.0f;
	Dist->Max = 1.0f;
	Lifetime = Dist;
}

void UParticleModuleLifetime::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	const float Val = Lifetime ? Lifetime->GetValue(0.0f, Random.InstanceParams, &Random) : 1.0f;
	Particle.Lifetime = std::max(0.001f, Val);
}

void UParticleModuleLifetime::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (Lifetime)
		Lifetime->CollectCurves("Lifetime", FColor(255, 190, 90, 255), OutCurves);
}

void UParticleModuleLifetime::InvalidateDistributions() const
{
	if (Lifetime) Lifetime->InvalidateLookupTable();
}

void UParticleModuleLifetime::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	if (!Lifetime)
		return;
	float MinL = 1.0f;
	float MaxL = 1.0f;
	Lifetime->GetOutRange(MinL, MaxL);
	OutBuildInfo.MaxLifetime = std::max(0.05f, MaxL);
}

UParticleModuleLocation::UParticleModuleLocation()
{
	UDistributionVectorUniform* Dist = NewObject<UDistributionVectorUniform>(this);
	Dist->Min = FVector::ZeroVector;
	Dist->Max = FVector::ZeroVector;
	StartLocation = Dist;
}

void UParticleModuleLocation::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	if (!StartLocation)
		return;
	Particle.Location += StartLocation->GetValue(0.0f, Random.InstanceParams, &Random);
}

void UParticleModuleLocation::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (StartLocation)
		StartLocation->CollectCurves("Location", FColor(120, 220, 255, 255), OutCurves);
}

void UParticleModuleLocation::InvalidateDistributions() const
{
	if (StartLocation) StartLocation->InvalidateLookupTable();
}

void UParticleModuleLocation::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	if (!StartLocation)
		return;
	FVector ModMin = FVector::ZeroVector;
	FVector ModMax = FVector::ZeroVector;
	StartLocation->GetOutRange(ModMin, ModMax);
	OutBuildInfo.SpawnLocationMin = FVector(
		std::min(OutBuildInfo.SpawnLocationMin.X, ModMin.X),
		std::min(OutBuildInfo.SpawnLocationMin.Y, ModMin.Y),
		std::min(OutBuildInfo.SpawnLocationMin.Z, ModMin.Z));
	OutBuildInfo.SpawnLocationMax = FVector(
		std::max(OutBuildInfo.SpawnLocationMax.X, ModMax.X),
		std::max(OutBuildInfo.SpawnLocationMax.Y, ModMax.Y),
		std::max(OutBuildInfo.SpawnLocationMax.Z, ModMax.Z));
}

UParticleModuleVelocity::UParticleModuleVelocity()
{
	UDistributionVectorUniform* Dist = NewObject<UDistributionVectorUniform>(this);
	Dist->Min = FVector::ZeroVector;
	Dist->Max = FVector::ZeroVector;
	StartVelocity = Dist;
}

void UParticleModuleVelocity::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	if (!StartVelocity)
		return;
	Particle.Velocity += StartVelocity->GetValue(0.0f, Random.InstanceParams, &Random);
	Particle.BaseVelocity = Particle.Velocity;
}

void UParticleModuleVelocity::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (StartVelocity)
		StartVelocity->CollectCurves("Velocity", FColor(140, 255, 160, 255), OutCurves);
}

void UParticleModuleVelocity::InvalidateDistributions() const
{
	if (StartVelocity) StartVelocity->InvalidateLookupTable();
}

void UParticleModuleVelocity::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	if (!StartVelocity)
		return;
	FVector VMin = FVector::ZeroVector;
	FVector VMax = FVector::ZeroVector;
	StartVelocity->GetOutRange(VMin, VMax);
	OutBuildInfo.MaxVelocity.X = std::max({ OutBuildInfo.MaxVelocity.X, std::abs(VMin.X), std::abs(VMax.X) });
	OutBuildInfo.MaxVelocity.Y = std::max({ OutBuildInfo.MaxVelocity.Y, std::abs(VMin.Y), std::abs(VMax.Y) });
	OutBuildInfo.MaxVelocity.Z = std::max({ OutBuildInfo.MaxVelocity.Z, std::abs(VMin.Z), std::abs(VMax.Z) });
}

UParticleModuleColor::UParticleModuleColor()
{
	UDistributionVectorConstant* ColorDist = NewObject<UDistributionVectorConstant>(this);
	ColorDist->Constant = FVector::OneVector;
	StartColor = ColorDist;

	UDistributionFloatConstant* AlphaDist = NewObject<UDistributionFloatConstant>(this);
	AlphaDist->Constant = 1.0f;
	StartAlpha = AlphaDist;
}

void UParticleModuleColor::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	const FString& ParamName = ExtractVectorParamName(StartColor);
	if (!ParamName.empty() && Random.InstanceParams)
	{
		if (const FColor* Override = Random.InstanceParams->ColorParams.Find(ParamName))
		{
			Particle.Color = *Override;
			Particle.BaseAlpha = Particle.Color.A;
			return;
		}
	}
	const FVector RGB = StartColor ? StartColor->GetValue(0.0f, Random.InstanceParams, &Random) : FVector::OneVector;
	const float Alpha = StartAlpha ? StartAlpha->GetValue(0.0f, Random.InstanceParams, &Random) : 1.0f;
	Particle.Color = FColor(
		NormalizedFloatToByte(RGB.X),
		NormalizedFloatToByte(RGB.Y),
		NormalizedFloatToByte(RGB.Z),
		NormalizedFloatToByte(Alpha));
	Particle.BaseAlpha = Particle.Color.A;
}

void UParticleModuleColor::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (StartColor) StartColor->CollectCurves("Color", FColor(255, 180, 180, 255), OutCurves);
	if (StartAlpha) StartAlpha->CollectCurves("Alpha", FColor(220, 220, 220, 255), OutCurves);
}

void UParticleModuleColor::InvalidateDistributions() const
{
	if (StartColor) StartColor->InvalidateLookupTable();
	if (StartAlpha) StartAlpha->InvalidateLookupTable();
}

void UParticleModuleColor::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	const FVector RGB = StartColor ? StartColor->GetValue(0.0f, nullptr, nullptr) : FVector::OneVector;
	const float Alpha = StartAlpha ? StartAlpha->GetValue(0.0f, nullptr, nullptr) : 1.0f;
	OutBuildInfo.InitialColorStart = FVector4(RGB.X, RGB.Y, RGB.Z, Alpha);
	const FString& ParamName = ExtractVectorParamName(StartColor);
	if (!ParamName.empty())
		OutBuildInfo.ColorParameterName = ParamName;
}

UParticleModuleSize::UParticleModuleSize()
{
	UDistributionVectorConstant* Dist = NewObject<UDistributionVectorConstant>(this);
	Dist->Constant = FVector::OneVector;
	StartSize = Dist;
}

void UParticleModuleSize::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	const FVector Val = StartSize ? StartSize->GetValue(0.0f, Random.InstanceParams, &Random) : FVector::OneVector;
	Particle.BaseSize = Val;
	Particle.Size = Val;
}

void UParticleModuleSize::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (StartSize)
		StartSize->CollectCurves("StartSize", FColor(180, 220, 255, 255), OutCurves);
}

void UParticleModuleSize::InvalidateDistributions() const
{
	if (StartSize) StartSize->InvalidateLookupTable();
}

void UParticleModuleSize::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	if (!StartSize)
		return;
	FVector SMin = FVector::OneVector;
	FVector SMax = FVector::OneVector;
	StartSize->GetOutRange(SMin, SMax);
	OutBuildInfo.MaxStartSize.X = std::max({ OutBuildInfo.MaxStartSize.X, std::abs(SMin.X), std::abs(SMax.X) });
	OutBuildInfo.MaxStartSize.Y = std::max({ OutBuildInfo.MaxStartSize.Y, std::abs(SMin.Y), std::abs(SMax.Y) });
	OutBuildInfo.MaxStartSize.Z = std::max({ OutBuildInfo.MaxStartSize.Z, std::abs(SMin.Z), std::abs(SMax.Z) });
}

UParticleModuleSizeByLife::UParticleModuleSizeByLife()
{
	LifeScale = NewObject<UDistributionVectorConstantCurve>(this);
}

void UParticleModuleSizeByLife::Update(FBaseParticle& Particle, float, uint32, const FParticleInstanceParameters* InstanceParams) const
{
	if (!LifeScale)
		return;
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	const FVector Scale = LifeScale->GetValue(T, InstanceParams, nullptr);
	Particle.Size = FVector(Particle.BaseSize.X * Scale.X, Particle.BaseSize.Y * Scale.Y, Particle.BaseSize.Z * Scale.Z);
}

void UParticleModuleSizeByLife::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (LifeScale)
		LifeScale->CollectCurves("Scale", FColor(255, 80, 80, 255), OutCurves);
}

void UParticleModuleSizeByLife::InvalidateDistributions() const
{
	if (LifeScale) LifeScale->InvalidateLookupTable();
}

void UParticleModuleSizeByLife::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	CompileVectorToAtlasChannel(LifeScale, OutBuildInfo.SizeScale);
	if (LifeScale)
	{
		FVector SMin = FVector::OneVector;
		FVector SMax = FVector::OneVector;
		LifeScale->GetOutRange(SMin, SMax);
		OutBuildInfo.MaxLifeScale.X = std::max({ OutBuildInfo.MaxLifeScale.X, std::abs(SMin.X), std::abs(SMax.X) });
		OutBuildInfo.MaxLifeScale.Y = std::max({ OutBuildInfo.MaxLifeScale.Y, std::abs(SMin.Y), std::abs(SMax.Y) });
		OutBuildInfo.MaxLifeScale.Z = std::max({ OutBuildInfo.MaxLifeScale.Z, std::abs(SMin.Z), std::abs(SMax.Z) });
	}
}

UParticleModuleColorOverLife::UParticleModuleColorOverLife()
{
	ColorScale = NewObject<UDistributionVectorConstantCurve>(this);
	UDistributionFloatConstantCurve* AlphaCurve = NewObject<UDistributionFloatConstantCurve>(this);
	AlphaCurve->Curve->Reset();
	AlphaCurve->Curve->AddKey(0.0f, 1.0f);
	AlphaCurve->Curve->AddKey(1.0f, 0.0f);
	AlphaScale = AlphaCurve;
}

float UParticleModuleColorOverLife::EvaluateAlphaScale(float RelativeTime, const FParticleInstanceParameters* InstanceParams) const
{
	const float T = std::clamp(RelativeTime, 0.0f, 1.0f);
	if (bUseFadeInOut)
	{
		const float InEnd = std::clamp(FadeInEnd, 0.0f, 1.0f);
		const float OutStart = std::clamp(FadeOutStart, InEnd, 1.0f);
		if (InEnd > 0.0f && T < InEnd)
			return std::clamp(T / InEnd, 0.0f, 1.0f);
		if (OutStart < 1.0f && T > OutStart)
			return std::clamp(1.0f - (T - OutStart) / (1.0f - OutStart), 0.0f, 1.0f);
		return 1.0f;
	}
	return AlphaScale ? std::clamp(AlphaScale->GetValue(T, InstanceParams, nullptr), 0.0f, 1.0f) : 1.0f;
}

void UParticleModuleColorOverLife::Update(FBaseParticle& Particle, float, uint32, const FParticleInstanceParameters* InstanceParams) const
{
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	const FString& ParamName = ExtractVectorParamName(ColorScale);
	FVector RGB = FVector::OneVector;
	bool bHasOverride = false;
	if (!ParamName.empty() && InstanceParams)
	{
		if (const FColor* Override = InstanceParams->ColorParams.Find(ParamName))
		{
			RGB = FVector(Override->R / 255.0f, Override->G / 255.0f, Override->B / 255.0f);
			bHasOverride = true;
		}
	}
	if (!bHasOverride && ColorScale)
		RGB = ColorScale->GetValue(T, InstanceParams, nullptr);

	const float Alpha = EvaluateAlphaScale(T, InstanceParams);
	const uint8 FinalAlpha = static_cast<uint8>(std::lround(static_cast<float>(Particle.BaseAlpha) * Alpha));
	Particle.Color = FColor(NormalizedFloatToByte(RGB.X), NormalizedFloatToByte(RGB.Y), NormalizedFloatToByte(RGB.Z), FinalAlpha);
}

void UParticleModuleColorOverLife::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (ColorScale)
		ColorScale->CollectCurves("Color", FColor(255, 80, 80, 255), OutCurves);
	if (AlphaScale && !bUseFadeInOut)
		AlphaScale->CollectCurves("Alpha", FColor(220, 220, 220, 255), OutCurves);
}

void UParticleModuleColorOverLife::InvalidateDistributions() const
{
	if (ColorScale) ColorScale->InvalidateLookupTable();
	if (AlphaScale) AlphaScale->InvalidateLookupTable();
}

void UParticleModuleColorOverLife::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	const FString& ParamName = ExtractVectorParamName(ColorScale);
	if (!ParamName.empty())
		OutBuildInfo.ColorParameterName = ParamName;

	const bool bNeedCurveAtlas = bUseFadeInOut
		|| (ColorScale && ColorScale->UsesCurveAtlas())
		|| (AlphaScale && AlphaScale->UsesCurveAtlas());

	if (!bNeedCurveAtlas)
	{
		const FVector RGB = ColorScale ? ColorScale->GetValue(0.0f, nullptr, nullptr) : FVector::OneVector;
		const float A = EvaluateAlphaScale(0.0f, nullptr);
		OutBuildInfo.ColorOverLife.Mode = 1;
		OutBuildInfo.ColorOverLife.ConstantValue = FVector4(RGB.X, RGB.Y, RGB.Z, A);
		OutBuildInfo.ColorOverLife.ValueScale = OutBuildInfo.ColorOverLife.ConstantValue;
		OutBuildInfo.ColorOverLife.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

	const uint32 Count = FParticleCurveTexture::CurveSampleWidth;
	TArray<FVector> ColorSamples;
	if (ColorScale)
		ColorScale->SampleCurveVectors(Count, ColorSamples);
	else
		ColorSamples.Init(FVector::OneVector, Count);

	TArray<FVector4> RawSamples;
	RawSamples.SetNum(Count);
	for (uint32 i = 0; i < Count; ++i)
	{
		const float T = Count > 1 ? static_cast<float>(i) / static_cast<float>(Count - 1) : 0.0f;
		const FVector& C = ColorSamples[i];
		RawSamples[i] = FVector4(C.X, C.Y, C.Z, EvaluateAlphaScale(T, nullptr));
	}
	TArray<FVector4> OptimizedSamples;
	OptimizeCurveAtlasSamples(RawSamples, 4, OptimizedSamples);
	if (OptimizedSamples.Num() <= 1)
	{
		const FVector4 ConstVal = OptimizedSamples.IsEmpty() ? FVector4(1.0f, 1.0f, 1.0f, 1.0f) : OptimizedSamples[0];
		OutBuildInfo.ColorOverLife.Mode = 1;
		OutBuildInfo.ColorOverLife.ConstantValue = ConstVal;
		OutBuildInfo.ColorOverLife.ValueScale = ConstVal;
		OutBuildInfo.ColorOverLife.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}
	OutBuildInfo.ColorOverLife.AtlasHandle = FParticleCurveTexture::Get().AllocateCurve(OptimizedSamples);
	OutBuildInfo.ColorOverLife.Mode = OutBuildInfo.ColorOverLife.AtlasHandle.IsValid() ? 2 : 0;
	OutBuildInfo.ColorOverLife.ValueScale = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	OutBuildInfo.ColorOverLife.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
}

UParticleModuleRotation::UParticleModuleRotation()
{
	UDistributionFloatUniform* RotDist = NewObject<UDistributionFloatUniform>(this);
	RotDist->Min = 0.0f;
	RotDist->Max = 0.0f;
	StartRotation = RotDist;

	UDistributionFloatUniform* RateDist = NewObject<UDistributionFloatUniform>(this);
	RateDist->Min = 0.0f;
	RateDist->Max = 0.0f;
	StartRotationRate = RateDist;
}

void UParticleModuleRotation::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	Particle.Rotation = StartRotation ? StartRotation->GetValue(0.0f, Random.InstanceParams, &Random) : 0.0f;
	Particle.RotationRate = StartRotationRate ? StartRotationRate->GetValue(0.0f, Random.InstanceParams, &Random) : 0.0f;
	Particle.BaseRotationRate = Particle.RotationRate;
}

void UParticleModuleRotation::InvalidateDistributions() const
{
	if (StartRotation) StartRotation->InvalidateLookupTable();
	if (StartRotationRate) StartRotationRate->InvalidateLookupTable();
}

void UParticleModuleRotation::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (StartRotation) StartRotation->CollectCurves("Rotation", FColor(200, 160, 255, 255), OutCurves);
	if (StartRotationRate) StartRotationRate->CollectCurves("RotationRate", FColor(160, 220, 255, 255), OutCurves);
}

UParticleModuleSubUV::UParticleModuleSubUV()
{
	UDistributionFloatConstantCurve* CurveDist = NewObject<UDistributionFloatConstantCurve>(this);
	CurveDist->Curve->Reset();
	CurveDist->Curve->AddKey(0.0f, 0.0f);
	CurveDist->Curve->AddKey(1.0f, 0.0f);
	SubImageIndex = CurveDist;
}

void UParticleModuleSubUV::Update(FBaseParticle& Particle, float, uint32 Offset, const FParticleInstanceParameters* InstanceParams) const
{
	if (Offset == 0) return;
	const int32 Cols = std::max(1, SubImagesHorizontal);
	const int32 Rows = std::max(1, SubImagesVertical);
	const int32 TotalFrames = Cols * Rows;
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 0.9999f);
	const float Rate = std::max(0.0f, PlayRate);
	const float ScaledT = T * Rate;
	const float CurveT = Rate > 1.0f ? std::fmod(ScaledT, 1.0f) : std::clamp(ScaledT, 0.0f, 1.0f);
	const float RawIndex = bUseLinearOverLife
		? (Rate > 1.0f ? std::fmod(ScaledT * static_cast<float>(TotalFrames), static_cast<float>(TotalFrames)) : (ScaledT * static_cast<float>(TotalFrames)))
		: (SubImageIndex ? SubImageIndex->GetValue(CurveT, InstanceParams, nullptr) : 0.0f);
	FSubUVPayload& Payload = *reinterpret_cast<FSubUVPayload*>(reinterpret_cast<uint8*>(&Particle) + Offset);
	Payload.ImageIndex = std::floor(std::clamp(RawIndex, 0.0f, static_cast<float>(TotalFrames - 1)));
}

void UParticleModuleSubUV::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (SubImageIndex && !bUseLinearOverLife)
		SubImageIndex->CollectCurves("SubImage Index", FColor(255, 200, 80, 255), OutCurves);
}

void UParticleModuleSubUV::InvalidateDistributions() const
{
	if (SubImageIndex) SubImageIndex->InvalidateLookupTable();
}

void UParticleModuleSubUV::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	OutBuildInfo.bHasSubUV = true;
	OutBuildInfo.SubUVCols = static_cast<float>(std::max(1, SubImagesHorizontal));
	OutBuildInfo.SubUVRows = static_cast<float>(std::max(1, SubImagesVertical));
	OutBuildInfo.bSubUVLinear = bUseLinearOverLife;
	OutBuildInfo.SubUVPlayRate = std::max(0.0f, PlayRate);
	if (!bUseLinearOverLife)
		CompileFloatToAtlasChannel(SubImageIndex, OutBuildInfo.SubUV);
}

UParticleModuleVelocityOverLife::UParticleModuleVelocityOverLife()
{
	VelocityScale = NewObject<UDistributionVectorConstantCurve>(this);
}

void UParticleModuleVelocityOverLife::Update(FBaseParticle& Particle, float, uint32, const FParticleInstanceParameters* InstanceParams) const
{
	if (!VelocityScale)
		return;
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	const FVector Scale = VelocityScale->GetValue(T, InstanceParams, nullptr);
	Particle.Velocity = FVector(Particle.BaseVelocity.X * Scale.X, Particle.BaseVelocity.Y * Scale.Y, Particle.BaseVelocity.Z * Scale.Z);
}

void UParticleModuleVelocityOverLife::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (VelocityScale)
		VelocityScale->CollectCurves("VelScale", FColor(120, 255, 180, 255), OutCurves);
}

void UParticleModuleVelocityOverLife::InvalidateDistributions() const
{
	if (VelocityScale) VelocityScale->InvalidateLookupTable();
}

void UParticleModuleVelocityOverLife::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	CompileVectorToAtlasChannel(VelocityScale, OutBuildInfo.VelocityScale);
}

UParticleModuleRotationRateOverLife::UParticleModuleRotationRateOverLife()
{
	RotationRateScale = NewObject<UDistributionFloatConstantCurve>(this);
}

void UParticleModuleRotationRateOverLife::Update(FBaseParticle& Particle, float, uint32, const FParticleInstanceParameters* InstanceParams) const
{
	if (!RotationRateScale)
		return;
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	const float Scale = RotationRateScale->GetValue(T, InstanceParams, nullptr);
	Particle.RotationRate = Particle.BaseRotationRate * Scale;
}

void UParticleModuleRotationRateOverLife::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (RotationRateScale)
		RotationRateScale->CollectCurves("RotRateScale", FColor(180, 180, 255, 255), OutCurves);
}

void UParticleModuleRotationRateOverLife::InvalidateDistributions() const
{
	if (RotationRateScale) RotationRateScale->InvalidateLookupTable();
}

void UParticleModuleRotationRateOverLife::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	CompileFloatToAtlasChannel(RotationRateScale, OutBuildInfo.RotationRateScale);
}

UParticleModuleTypeDataBeam::UParticleModuleTypeDataBeam()
{
	UDistributionFloatConstant* FactorDist = NewObject<UDistributionFloatConstant>(this);
	FactorDist->Constant = 1.0f;
	TaperFactor = FactorDist;

	UDistributionFloatConstantCurve* ScaleDist = NewObject<UDistributionFloatConstantCurve>(this);
	ScaleDist->Curve->Reset();
	ScaleDist->Curve->AddKey(0.0f, 1.0f);
	ScaleDist->Curve->AddKey(1.0f, 1.0f);
	TaperScale = ScaleDist;
}

void UParticleModuleTypeDataBeam::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (TaperFactor)
		TaperFactor->CollectCurves("TaperFactor", FColor(160, 220, 255, 255), OutCurves);
	if (TaperScale)
		TaperScale->CollectCurves("TaperScale", FColor(255, 210, 120, 255), OutCurves);
}

void UParticleModuleTypeDataBeam::InvalidateDistributions() const
{
	if (TaperFactor) TaperFactor->InvalidateLookupTable();
	if (TaperScale) TaperScale->InvalidateLookupTable();
}

UParticleModuleBeamSource::UParticleModuleBeamSource()
{
	UDistributionVectorConstant* SourceDist = NewObject<UDistributionVectorConstant>(this);
	SourceDist->Constant = FVector::ZeroVector;
	Source = SourceDist;

	UDistributionVectorConstant* TangentDist = NewObject<UDistributionVectorConstant>(this);
	TangentDist->Constant = FVector(1.0f, 0.0f, 0.0f);
	SourceTangent = TangentDist;

	UDistributionFloatConstant* StrengthDist = NewObject<UDistributionFloatConstant>(this);
	StrengthDist->Constant = 1.0f;
	SourceStrength = StrengthDist;
}

void UParticleModuleBeamSource::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (Source)
		Source->CollectCurves("BeamSource", FColor(120, 220, 255, 255), OutCurves);
	if (SourceTangent)
		SourceTangent->CollectCurves("SourceTangent", FColor(180, 255, 180, 255), OutCurves);
	if (SourceStrength)
		SourceStrength->CollectCurves("SourceStrength", FColor(255, 200, 120, 255), OutCurves);
}

void UParticleModuleBeamSource::InvalidateDistributions() const
{
	if (Source) Source->InvalidateLookupTable();
	if (SourceTangent) SourceTangent->InvalidateLookupTable();
	if (SourceStrength) SourceStrength->InvalidateLookupTable();
}

UParticleModuleBeamTarget::UParticleModuleBeamTarget()
{
	UDistributionVectorConstant* TargetDist = NewObject<UDistributionVectorConstant>(this);
	TargetDist->Constant = FVector(3.0f, 0.0f, 0.0f);
	Target = TargetDist;

	UDistributionVectorConstant* TangentDist = NewObject<UDistributionVectorConstant>(this);
	TangentDist->Constant = FVector(-1.0f, 0.0f, 0.0f);
	TargetTangent = TangentDist;

	UDistributionFloatConstant* StrengthDist = NewObject<UDistributionFloatConstant>(this);
	StrengthDist->Constant = 1.0f;
	TargetStrength = StrengthDist;
}

void UParticleModuleBeamTarget::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (Target)
		Target->CollectCurves("BeamTarget", FColor(255, 140, 140, 255), OutCurves);
	if (TargetTangent)
		TargetTangent->CollectCurves("TargetTangent", FColor(255, 200, 140, 255), OutCurves);
	if (TargetStrength)
		TargetStrength->CollectCurves("TargetStrength", FColor(255, 230, 120, 255), OutCurves);
}

void UParticleModuleBeamTarget::InvalidateDistributions() const
{
	if (Target) Target->InvalidateLookupTable();
	if (TargetTangent) TargetTangent->InvalidateLookupTable();
	if (TargetStrength) TargetStrength->InvalidateLookupTable();
}

UParticleModuleBeamNoise::UParticleModuleBeamNoise()
{
	UDistributionVectorUniform* RangeDist = NewObject<UDistributionVectorUniform>(this);
	RangeDist->Min = FVector(-0.25f, -0.25f, -0.25f);
	RangeDist->Max = FVector(0.25f, 0.25f, 0.25f);
	NoiseRange = RangeDist;
}

void UParticleModuleBeamNoise::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (NoiseRange)
		NoiseRange->CollectCurves("NoiseRange", FColor(170, 150, 255, 255), OutCurves);
}

void UParticleModuleBeamNoise::InvalidateDistributions() const
{
	if (NoiseRange) NoiseRange->InvalidateLookupTable();
}

UParticleLODLevel::~UParticleLODLevel()
{
	BuildInfo.ReleaseAtlasHandles();
}

UParticleModuleRequired* UParticleLODLevel::GetOrCreateRequired()
{
	if (!RequiredModule) RequiredModule = NewObject<UParticleModuleRequired>(this);
	return RequiredModule;
}

UParticleModuleSpawn* UParticleLODLevel::GetOrCreateSpawn()
{
	if (!SpawnModule) SpawnModule = NewObject<UParticleModuleSpawn>(this);
	return SpawnModule;
}

UParticleModuleTypeDataBase* UParticleLODLevel::SetTypeData(UClass* TypeDataClass)
{
	if (!TypeDataClass || !TypeDataClass->Constructor ||
		!TypeDataClass->IsChildOf(UParticleModuleTypeDataBase::StaticClass())) return nullptr;
	TypeDataModule = NewObject<UParticleModuleTypeDataBase>(this, TypeDataClass);
	MarkBuildDirty();
	return TypeDataModule;
}

UParticleModuleTypeDataBase* UParticleLODLevel::GetOrCreateTypeData()
{
	if (!TypeDataModule) SetTypeData(UParticleModuleTypeDataSprite::StaticClass());
	return TypeDataModule;
}

void UParticleLODLevel::MarkBuildDirty()
{
	bBuildDirty = true;
}

const FParticleEmitterBuildInfo& UParticleLODLevel::EnsureBuilt()
{
	if (!bBuildDirty)
		return BuildInfo;

	BuildInfo.Reset();
	SpawnModules.Reset();
	UpdateModules.Reset();

	for (UParticleModule* Module : EffectiveModules)
	{
		if (!Module || !Module->bEnabled) continue;
		if (Module->RunsOnSpawn())
			SpawnModules.Add(Module);
		if (Module->RunsOnUpdate())
			UpdateModules.Add(Module);
		Module->CompileModules(BuildInfo);
	}

	bBuildDirty = false;
	return BuildInfo;
}

UParticleModule* UParticleLODLevel::AddModule(UClass* ModuleClass)
{
	if (!ModuleClass || !ModuleClass->Constructor || !ModuleClass->IsChildOf(UParticleModule::StaticClass()) ||
		ModuleClass->IsChildOf(UParticleModuleRequired::StaticClass()) ||
		ModuleClass->IsChildOf(UParticleModuleSpawn::StaticClass()) ||
		ModuleClass->IsChildOf(UParticleModuleTypeDataBase::StaticClass())) return nullptr;

	UParticleModule* Module = NewObject<UParticleModule>(this, ModuleClass);
	if (Module)
	{
		int32 SlotId = 1;
		for (const UParticleModule* Existing : Modules)
		{
			if (Existing)
			{
				SlotId = std::max(SlotId, Existing->LODSlotId + 1);
			}
		}
		Module->LODSlotId = SlotId;
		Modules.Add(Module);
		MarkBuildDirty();
	}
	return Module;
}

bool UParticleLODLevel::RemoveModule(UParticleModule* Module)
{
	if (!Module) return false;
	for (int32 Index = 0; Index < Modules.Num(); ++Index)
	{
		if (Modules[Index] == Module)
		{
			Modules.RemoveAt(static_cast<uint32>(Index));
			MarkBuildDirty();
			return true;
		}
	}
	return false;
}

bool UParticleLODLevel::MoveModule(int32 FromIndex, int32 ToIndex)
{
	const int32 Count = static_cast<int32>(Modules.Num());
	if (FromIndex < 0 || FromIndex >= Count || ToIndex < 0 || ToIndex >= Count || FromIndex == ToIndex)
		return false;
	UParticleModule* Moved = Modules[FromIndex];
	Modules.RemoveAt(FromIndex);
	Modules.Insert(Moved, ToIndex);
	MarkBuildDirty();
	return true;
}

bool UParticleLODLevel::OwnsModule(const UParticleModule* Module) const
{
	if (!Module)
	{
		return false;
	}

	if (Module == RequiredModule || Module == SpawnModule || Module == TypeDataModule)
	{
		return true;
	}

	for (const UParticleModule* Owned : Modules)
	{

		if (Owned == Module)
		{
			return true;
		}
	}
	return false;
}

void UParticleEmitter::EnableModuleInheritance()
{
	if (bInheritLODModules)
	{
		return;
	}

	if (!LODLevels.IsEmpty())
	{
		if (!LODLevels[0])
		{
			return;
		}

		for (uint32 Index = 1; Index < LODLevels.Num(); ++Index)
		{
			if (!LODLevels[Index] || !HasMatchingModuleSlots(*LODLevels[0], *LODLevels[Index]))
			{
				return;
			}
		}

		for (UParticleLODLevel* LOD : LODLevels)
		{
			LOD->GetOrCreateRequired();
			LOD->GetOrCreateSpawn();
			LOD->GetOrCreateTypeData();
			for (uint32 Index = 0; Index < LOD->Modules.Num(); ++Index)
			{
				if (LOD->Modules[Index])
				{
					LOD->Modules[Index]->LODSlotId = static_cast<int32>(Index) + 1;
				}
			}
		}
	}
	bInheritLODModules = true;
}

UParticleLODLevel* UParticleEmitter::AddLODLevel()
{
	InsertLODLevel(static_cast<int32>(LODLevels.Num()));
	return LODLevels[LODLevels.Num() - 1];
}

void UParticleEmitter::InsertLODLevel(int32 Index)
{
	if (Index < 0 || Index > static_cast<int32>(LODLevels.Num())) return;
	if (Index == 0 && !LODLevels.IsEmpty()) return;
	UParticleLODLevel* Previous = Index > 0
							? LODLevels[Index - 1]
							: nullptr;
	UParticleLODLevel* LOD = !bInheritLODModules && Previous
						? Cast<UParticleLODLevel>(StaticDuplicateObject(Previous, this))
						: nullptr;
	if (!LOD)
	{
		LOD = NewObject<UParticleLODLevel>(this);
	}

	if (!bInheritLODModules || Index == 0)
	{
		LOD->GetOrCreateRequired();
		LOD->GetOrCreateSpawn();
		LOD->GetOrCreateTypeData();
	}

	if (Previous)
	{
		LOD->bEnabled = Previous->bEnabled;
	}
	LODLevels.Insert(LOD, Index);
	for (uint32 Level = Index; Level < LODLevels.Num(); ++Level)
	{
		if (LODLevels[Level])
		{
			LODLevels[Level]->Level = static_cast<int32>(Level);
		}
	}
}

void UParticleEmitter::RemoveLODLevel(int32 Index)
{
	if (Index <= 0 || static_cast<uint32>(Index) >= LODLevels.Num())
	{
		return;
	}
	LODLevels.RemoveAt(Index);

	for (uint32 Level = Index; Level < LODLevels.Num(); ++Level)
	{
		if (LODLevels[Level])
		{
			LODLevels[Level]->Level = static_cast<int32>(Level);
		}
	}
}

bool UParticleEmitter::MoveLODLevel(int32 FromIndex, int32 ToIndex)
{
	const int32 Count = static_cast<int32>(LODLevels.Num());
	if (FromIndex <= 0 || ToIndex <= 0 || FromIndex >= Count || ToIndex >= Count || FromIndex == ToIndex)
		return false;

	UParticleLODLevel* Moved = LODLevels[FromIndex];
	LODLevels.RemoveAt(FromIndex);
	LODLevels.Insert(Moved, ToIndex);
	for (int32 Index = std::min(FromIndex, ToIndex); Index <= std::max(FromIndex, ToIndex); ++Index)
		LODLevels[Index]->Level = Index;
	CacheEmitterModuleInfo();
	return true;
}

bool UParticleEmitter::RemoveBaseModule(UParticleModule* Module)
{
	if (!Module || LODLevels.IsEmpty() || !LODLevels[0] || !LODLevels[0]->RemoveModule(Module))
	{
		return false;
	}

	if (bInheritLODModules)
	{
		for (uint32 Index = 1; Index < LODLevels.Num(); ++Index)
		{
			UParticleLODLevel* LOD = LODLevels[Index];
			if (LOD)
			{
				LOD->RemoveModule(FindLODModule(LOD->Modules, Module->LODSlotId));
			}
		}
	}
	return true;
}

UParticleModule* UParticleEmitter::DuplicateLODModule(int32 TargetIndex, const UParticleModule* Slot, bool bFromHighest)
{
	if (!bInheritLODModules || !Slot || TargetIndex <= 0 || static_cast<uint32>(TargetIndex) >= LODLevels.Num()) return nullptr;
	UParticleLODLevel* Target = LODLevels[TargetIndex];
	UParticleLODLevel* Source = LODLevels[bFromHighest ? 0 : TargetIndex - 1];
	if (!Target || !Source) return nullptr;

	if (Slot == Target->EffectiveRequired)
	{
		if (!Source->EffectiveRequired) return nullptr;
		if (auto* Copy = Cast<UParticleModuleRequired>(StaticDuplicateObject(Source->EffectiveRequired, Target)))
			return Target->RequiredModule = Copy;
		return nullptr;
	}
	if (Slot == Target->EffectiveSpawn)
	{
		if (!Source->EffectiveSpawn) return nullptr;
		if (auto* Copy = Cast<UParticleModuleSpawn>(StaticDuplicateObject(Source->EffectiveSpawn, Target)))
			return Target->SpawnModule = Copy;
		return nullptr;
	}
	if (Slot == Target->EffectiveTypeData)
	{
		if (!Source->EffectiveTypeData) return nullptr;
		if (auto* Copy = Cast<UParticleModuleTypeDataBase>(StaticDuplicateObject(Source->EffectiveTypeData, Target)))
			return Target->TypeDataModule = Copy;
		return nullptr;
	}

	UParticleModule* Original = FindLODModule(Source->EffectiveModules, Slot->LODSlotId);
	if (!Original) return nullptr;
	UParticleModule* Copy = Cast<UParticleModule>(StaticDuplicateObject(Original, Target));
	if (!Copy) return nullptr;
	Copy->LODSlotId = Slot->LODSlotId;
	for (UParticleModule*& Override : Target->Modules)
	{
		if (Override && Override->LODSlotId == Copy->LODSlotId)
		{
			Override = Copy;
			return Copy;
		}
	}
	Target->Modules.Add(Copy);
	return Copy;
}

bool UParticleEmitter::InheritLODModule(int32 TargetIndex, UParticleModule* Module)
{
	if (!bInheritLODModules || !Module || TargetIndex <= 0 || static_cast<uint32>(TargetIndex) >= LODLevels.Num()) return false;
	UParticleLODLevel* Target = LODLevels[TargetIndex];
	if (!Target) return false;
	if (Target->RequiredModule == Module) Target->RequiredModule = nullptr;
	else if (Target->SpawnModule == Module) Target->SpawnModule = nullptr;
	else if (Target->TypeDataModule == Module) Target->TypeDataModule = nullptr;
	else return Target->RemoveModule(Module);
	return true;
}

void UParticleEmitter::CacheEmitterModuleInfo()
{
	UParticleLODLevel* Previous = nullptr;
	for (UParticleLODLevel* LOD : LODLevels)
	{
		if (!LOD) continue;
		if (bInheritLODModules && Previous)
		{
			LOD->EffectiveRequired = LOD->RequiredModule ? LOD->RequiredModule : Previous->EffectiveRequired;
			LOD->EffectiveSpawn = LOD->SpawnModule ? LOD->SpawnModule : Previous->EffectiveSpawn;
			LOD->EffectiveTypeData = LOD->TypeDataModule ? LOD->TypeDataModule : Previous->EffectiveTypeData;
			LOD->EffectiveModules.Reset();
			for (UParticleModule* Inherited : Previous->EffectiveModules)
			{
				if (!Inherited)
				{
					continue;
				}
				UParticleModule* Override = FindLODModule(LOD->Modules, Inherited->LODSlotId);
				LOD->EffectiveModules.Add(Override ? Override : Inherited);
			}
		}
		else
		{
			LOD->EffectiveRequired = LOD->GetOrCreateRequired();
			LOD->EffectiveSpawn = LOD->GetOrCreateSpawn();
			LOD->EffectiveTypeData = LOD->GetOrCreateTypeData();
			LOD->EffectiveModules = LOD->Modules;
		}
		LOD->MarkBuildDirty();
		LOD->EnsureBuilt();
		Previous = LOD;
	}
}

UParticleEmitter* UParticleSystem::AddEmitter()
{
	UParticleEmitter* Emitter = NewObject<UParticleEmitter>(this);
	Emitters.Add(Emitter);
	return Emitter;
}

int32 UParticleSystem::GetLODCount() const
{
	int32 Count = std::max(1, static_cast<int32>(LODDistances.Num()));
	for (const UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
		{
			Count = std::max(Count, static_cast<int32>(Emitter->GetLODLevels().Num()));
		}
	}
	return Count;
}

float UParticleSystem::GetLODDistance(int32 Index) const
{
	const int32 Last = static_cast<int32>(LODDistances.Num()) - 1;
	if (Last < 0)
	{
		return 10.0f * Index;
	}

	return Index <= Last
		? LODDistances[Index]
		: LODDistances[Last] + 10.0f * (Index - Last);
}

void UParticleSystem::FillMissingLODDistances()
{
	const int32 Count = GetLODCount();
	while (static_cast<int32>(LODDistances.Num()) < Count)
	{
		LODDistances.Add(GetLODDistance(static_cast<int32>(LODDistances.Num())));
	}
}

bool UParticleSystem::SetLODDistance(int32 Index, float Distance)
{
	const int32 Count = GetLODCount();
	if (Index <= 0 || Index >= Count || !std::isfinite(Distance) || Distance <= 0.0f)
	{
		return false;
	}
	if (Distance <= GetLODDistance(Index - 1))
	{
		return false;
	}
	if (Index + 1 < Count && Distance >= GetLODDistance(Index + 1))
	{
		return false;
	}

	FillMissingLODDistances();
	LODDistances[Index] = Distance;
	return true;
}

bool UParticleSystem::InsertLOD(int32 Index, bool bCopyPrevious)
{
	const int32 Count = GetLODCount();
	if (Index <= 0 || Index > Count)
	{
		return false;
	}

	const float Lower = GetLODDistance(Index - 1);
	const float Upper = GetLODDistance(Index);
	const float Distance = Index == Count
						? Upper
						: Lower + (Upper - Lower) * 0.5f;
	if (!std::isfinite(Distance) || Distance <= Lower || (Index < Count && Distance >= Upper))
	{
		return false;
	}

	FillMissingLODDistances();
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		while (static_cast<int32>(Emitter->GetLODLevels().Num()) < Count)
		{
			Emitter->AddLODLevel();
		}
		Emitter->InsertLODLevel(Index);
		Emitter->CacheEmitterModuleInfo();
		if (bCopyPrevious && Emitter->UsesModuleInheritance())
		{
			UParticleLODLevel* Added = Emitter->GetLODLevels()[Index];
			Emitter->DuplicateLODModule(Index, Added->EffectiveRequired, false);
			Emitter->DuplicateLODModule(Index, Added->EffectiveSpawn, false);
			Emitter->DuplicateLODModule(Index, Added->EffectiveTypeData, false);
			for (const UParticleModule* Module : Added->EffectiveModules)
				Emitter->DuplicateLODModule(Index, Module, false);
			Emitter->CacheEmitterModuleInfo();
		}
	}
	LODDistances.Insert(Distance, Index);
	return true;
}

bool UParticleSystem::RemoveLOD(int32 Index)
{
	const int32 Count = GetLODCount();
	if (Index <= 0 || Index >= Count) return false;
	FillMissingLODDistances();
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		while (static_cast<int32>(Emitter->GetLODLevels().Num()) < Count)
		{
			Emitter->AddLODLevel();
		}
		Emitter->RemoveLODLevel(Index);
		Emitter->CacheEmitterModuleInfo();
	}
	LODDistances.RemoveAt(Index);
	return true;
}

bool UParticleSystem::RemoveEmitter(UParticleEmitter* Emitter)
{
	if (!Emitter) return false;
	for (int32 Index = 0; Index < Emitters.Num(); ++Index)
	{
		if (Emitters[Index] == Emitter)
		{
			Emitters.RemoveAt(static_cast<uint32>(Index));
			return true;
		}
	}
	return false;
}

UParticleEmitter* UParticleSystem::DuplicateEmitter(const UParticleEmitter* Source)
{
	if (!Source) return nullptr;
	UParticleEmitter* Copy = Cast<UParticleEmitter>(StaticDuplicateObject(Source, this));
	if (!Copy) return nullptr;
	Copy->CacheEmitterModuleInfo();
	Emitters.Add(Copy);
	return Copy;
}

void UParticleSystem::PrepareLODModules()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter) continue;
		Emitter->EnableModuleInheritance();
		Emitter->CacheEmitterModuleInfo();
	}
}

bool UParticleSystem::MoveEmitter(int32 FromIndex, int32 ToIndex)
{
	const int32 Count = static_cast<int32>(Emitters.Num());

	if (FromIndex < 0 || FromIndex >= Count || ToIndex < 0 || ToIndex >= Count || FromIndex == ToIndex)
	{
		return false;
	}

	UParticleEmitter* Moved = Emitters[FromIndex];
	Emitters.RemoveAt(FromIndex);
	Emitters.Insert(Moved, ToIndex);

	return true;
}

bool UParticleSystem::SaveToFile(const FString& Path)
{
	return FJsonArchive::SaveObject(this, Path);
}

UParticleSystem* UParticleSystem::LoadFromFile(const FString& Path, UObject* Outer)
{
	UParticleSystem* System = NewObject<UParticleSystem>(Outer);
	if (!FJsonArchive::LoadObject(System, Path))
		return nullptr;
	System->PrepareLODModules();
	return System;
}

void UParticleModuleCollision::TickCollision(FBaseParticle& Particle, float DeltaTime, uint32 Offset, 
	const FParticleTickContext& Context, const UParticleModuleEventGenerator * EventGenerator) const
{
	if (Offset == 0)
	{
		return;
	}

	FParticleCollisionPayload& Payload = *reinterpret_cast<FParticleCollisionPayload*>(reinterpret_cast<uint8*>(&Particle) + Offset);
	if (Payload.CollisionCount >= MaxCollisions)
	{
		return;
	}

	FVector MoveDelta = Particle.Velocity * DeltaTime;
	float MoveDist = MoveDelta.Length();
	if (MoveDist < 0.001f)
	{
		return;
	}

	FVector PrevLocation = Particle.Location - MoveDelta;
	FRay WorldRay;
	WorldRay.Origin = PrevLocation;
	WorldRay.Direction = MoveDelta.Normalized();

	FHitResult Hit;
	Context.World->SweepSingle(WorldRay, MoveDist, CollisionRadius, Hit, Context.Component->GetOwner());
	if (Hit.bBlockingHit)
	{
		Particle.Location = Hit.ImpactPoint + Hit.ImpactNormal * 0.001f;

		float VDotNormal = Particle.Velocity.Dot(Hit.ImpactNormal);
		if (VDotNormal > 0)
		{
			return;
		}

		FVector ProjectedNormal = Hit.ImpactNormal * (-VDotNormal);
		FVector Vt = Particle.Velocity + ProjectedNormal;
		Vt *= (1 - FrictionMax);
		Vt += ProjectedNormal * RestitutionMax;

		Particle.Velocity = Vt;
		Payload.CollisionCount++;

		if (EventGenerator==nullptr || Payload.CollisionCount % std::max(1,EventGenerator->EventFrequency) != 0)
		{
			return;
		}

		FParticleEventCollideData Event;
		Event.EventName = EventGenerator->EventName;
		Event.Location = Hit.Location;
		Event.Normal = Hit.ImpactNormal;
		Event.Velocity = Vt;
		Event.Component = Hit.HitComponent;
		Context.Component->AddCollisionEvent(Event);
	}
}

void UParticleModuleCollision::CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const
{
	OutBuildInfo.bHasCollision = true;
	OutBuildInfo.CollisionRestitution = std::max(0.0f, RestitutionMax);
	OutBuildInfo.CollisionFriction = std::clamp(FrictionMax, 0.0f, 1.0f);
	OutBuildInfo.CollisionMaxCount = std::max(0, MaxCollisions);
	OutBuildInfo.CollisionRadius = std::max(0.0f, CollisionRadius);
}
