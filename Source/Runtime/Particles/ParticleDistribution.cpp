#include "EnginePCH.h"
#include "Particles/ParticleDistribution.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr uint32 CurveLookupSampleCount = 128;
	bool bOptimizeParticleLookupTables = true;
	float ParticleLookupOptimizeErrorThreshold = 0.05f;

	UCurveFloat* CreateFlatCurve(UObject* Outer, float Value)
	{
		UCurveFloat* Curve = NewObject<UCurveFloat>(Outer);
		Curve->AddKey(0.0f, Value);
		Curve->AddKey(1.0f, Value);
		return Curve;
	}

	float EvalCurveSafe(const UCurveFloat* Curve, float Time, float Fallback)
	{
		return Curve ? Curve->GetFloatValue(Time) : Fallback;
	}

	float RandomRangeOrMid(FParticleRandom* Random, float A, float B)
	{
		if (Random)
			return Random->Range(A, B);
		return (A + B) * 0.5f;
	}

	float ComputeVector4Distance(const FVector4& A, const FVector4& B, uint32 ActiveComponents)
	{
		const float DX = A.X - B.X;
		float SumSq = DX * DX;
		if (ActiveComponents >= 2)
		{
			const float DY = A.Y - B.Y;
			SumSq += DY * DY;
		}
		if (ActiveComponents >= 3)
		{
			const float DZ = A.Z - B.Z;
			SumSq += DZ * DZ;
		}
		if (ActiveComponents >= 4)
		{
			const float DW = A.W - B.W;
			SumSq += DW * DW;
		}
		return std::sqrt(SumSq);
	}

	FVector4 SampleVector4Array(const TArray<FVector4>& Samples, float NormalizedT)
	{
		const uint32 Count = static_cast<uint32>(Samples.Num());
		if (Count == 0)
			return FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		if (Count == 1)
			return Samples[0];

		const float ClampedT = std::clamp(NormalizedT, 0.0f, 1.0f);
		const float Scaled = ClampedT * static_cast<float>(Count - 1);
		const uint32 Idx0 = std::clamp(static_cast<uint32>(Scaled), 0u, Count - 1u);
		const uint32 Idx1 = std::min(Idx0 + 1u, Count - 1u);
		const float Frac = Scaled - static_cast<float>(Idx0);
		const FVector4& A = Samples[Idx0];
		const FVector4& B = Samples[Idx1];
		return FVector4(
			A.X + (B.X - A.X) * Frac,
			A.Y + (B.Y - A.Y) * Frac,
			A.Z + (B.Z - A.Z) * Frac,
			A.W + (B.W - A.W) * Frac);
	}

	void SampleLookupEntryPair(
		const FParticleDistributionLookupTable& Table,
		float NormalizedT,
		TArray<float>& OutEntry)
	{
		OutEntry.SetNum(Table.EntryStride);
		if (Table.EntryStride == 0 || Table.EntryCount == 0 || Table.Values.IsEmpty())
		{
			for (uint32 i = 0; i < Table.EntryStride; ++i)
				OutEntry[i] = 0.0f;
			return;
		}

		const float ClampedT = std::clamp(NormalizedT, 0.0f, 1.0f);
		const float Scaled = std::clamp(
			ClampedT * Table.TimeScale + Table.TimeBias,
			0.0f,
			static_cast<float>(Table.EntryCount - 1));
		const uint32 Idx0 = static_cast<uint32>(Scaled);
		const uint32 Idx1 = std::min(Idx0 + 1u, Table.EntryCount - 1u);
		const float Frac = Scaled - static_cast<float>(Idx0);
		const uint32 Base0 = Idx0 * Table.EntryStride;
		const uint32 Base1 = Idx1 * Table.EntryStride;
		for (uint32 i = 0; i < Table.EntryStride; ++i)
			OutEntry[i] = Table.Values[Base0 + i] + (Table.Values[Base1 + i] - Table.Values[Base0 + i]) * Frac;
	}

	void ResampleLookupTable(
		const FParticleDistributionLookupTable& Source,
		uint32 SampleCount,
		FParticleDistributionLookupTable& OutTable)
	{
		OutTable.Op = Source.Op;
		OutTable.EntryCount = SampleCount;
		OutTable.EntryStride = Source.EntryStride;
		OutTable.SubEntryStride = Source.SubEntryStride;
		OutTable.TimeScale = SampleCount > 1 ? static_cast<float>(SampleCount - 1) : 0.0f;
		OutTable.TimeBias = 0.0f;
		OutTable.Values.SetNum(SampleCount * Source.EntryStride);

		TArray<float> Sampled;
		for (uint32 i = 0; i < SampleCount; ++i)
		{
			const float T = SampleCount > 1 ? static_cast<float>(i) / static_cast<float>(SampleCount - 1) : 0.0f;
			SampleLookupEntryPair(Source, T, Sampled);
			const uint32 Base = i * Source.EntryStride;
			for (uint32 k = 0; k < Source.EntryStride; ++k)
				OutTable.Values[Base + k] = Sampled[k];
		}
	}

	float ComputeLookupTableError(
		const FParticleDistributionLookupTable& Candidate,
		const FParticleDistributionLookupTable& Reference)
	{
		const uint32 CheckCount = std::max(Candidate.EntryCount, Reference.EntryCount);
		if (CheckCount == 0 || Reference.EntryStride == 0)
			return 0.0f;

		const uint32 VecComponents = (Reference.SubEntryStride > 0 && Reference.SubEntryStride <= Reference.EntryStride)
			? Reference.SubEntryStride
			: Reference.EntryStride;
		const bool bHasSecondSub = Reference.EntryStride >= VecComponents * 2u;

		TArray<float> ValCand;
		TArray<float> ValRef;
		float MaxError = 0.0f;
		for (uint32 i = 0; i < CheckCount; ++i)
		{
			const float T = CheckCount > 1 ? static_cast<float>(i) / static_cast<float>(CheckCount - 1) : 0.0f;
			SampleLookupEntryPair(Candidate, T, ValCand);
			SampleLookupEntryPair(Reference, T, ValRef);

			float SumSq0 = 0.0f;
			for (uint32 c = 0; c < VecComponents; ++c)
			{
				const float D = ValCand[c] - ValRef[c];
				SumSq0 += D * D;
			}
			MaxError = std::max(MaxError, std::sqrt(SumSq0));

			if (bHasSecondSub)
			{
				float SumSq1 = 0.0f;
				for (uint32 c = 0; c < VecComponents; ++c)
				{
					const float D = ValCand[VecComponents + c] - ValRef[VecComponents + c];
					SumSq1 += D * D;
				}
				MaxError = std::max(MaxError, std::sqrt(SumSq1));
			}
		}
		return MaxError;
	}

	void OptimizeLookupTable(FParticleDistributionLookupTable& InOutTable, float ErrorThreshold)
	{
		if (InOutTable.EntryCount <= 1 || InOutTable.EntryStride == 0)
			return;

		const FParticleDistributionLookupTable Original = InOutTable;
		const uint32 MaxCount = Original.EntryCount;
		FParticleDistributionLookupTable Candidate;
		for (uint32 SampleCount = 1; SampleCount < MaxCount; SampleCount <<= 1)
		{
			ResampleLookupTable(Original, SampleCount, Candidate);
			if (ComputeLookupTableError(Candidate, Original) < ErrorThreshold)
			{
				InOutTable = std::move(Candidate);
				return;
			}
		}
	}
}

FColor FParticleInstanceParameters::GetColor(const FString& Name, const FColor& Default) const
{
	if (Name.empty()) return Default;
	const FColor* Found = ColorParams.Find(Name);
	return Found ? *Found : Default;
}

FVector FParticleInstanceParameters::GetVector(const FString& Name, const FVector& Default) const
{
	if (Name.empty()) return Default;
	const FVector* Found = VectorParams.Find(Name);
	return Found ? *Found : Default;
}

float FParticleInstanceParameters::GetFloat(const FString& Name, float Default) const
{
	if (Name.empty()) return Default;
	const float* Found = FloatParams.Find(Name);
	return Found ? *Found : Default;
}

float FParticleRandom::Range(float Min, float Max)
{
	if (!std::isfinite(Min) || !std::isfinite(Max)) return 0.0f;
	if (Min > Max) std::swap(Min, Max);
	State ^= State << 13;
	State ^= State >> 17;
	State ^= State << 5;
	return Min + (Max - Min) * (State / 4294967295.0f);
}

float FParticleDistributionLookupTable::EvalFloat(float Time, FParticleRandom* Random) const
{
	if (Values.IsEmpty() || EntryCount == 0)
		return 0.0f;

	if (Op == EDistributionOp::Constant)
		return Values[0];

	if (Op == EDistributionOp::Random)
		return Values.Num() >= 2 ? RandomRangeOrMid(Random, Values[0], Values[1]) : Values[0];

	const float ClampedT = std::clamp(Time, 0.0f, 1.0f);
	const float Scaled = std::clamp(ClampedT * TimeScale + TimeBias, 0.0f, static_cast<float>(EntryCount - 1));
	const uint32 Idx0 = static_cast<uint32>(Scaled);
	const uint32 Idx1 = std::min(Idx0 + 1u, EntryCount - 1u);
	const float Frac = Scaled - static_cast<float>(Idx0);
	const uint32 Base0 = Idx0 * EntryStride;
	const uint32 Base1 = Idx1 * EntryStride;

	if (Op == EDistributionOp::ConstantCurve)
		return Values[Base0] + (Values[Base1] - Values[Base0]) * Frac;

	const float MinVal = Values[Base0] + (Values[Base1] - Values[Base0]) * Frac;
	const float MaxVal = Values[Base0 + SubEntryStride] + (Values[Base1 + SubEntryStride] - Values[Base0 + SubEntryStride]) * Frac;
	return RandomRangeOrMid(Random, MinVal, MaxVal);
}

FVector FParticleDistributionLookupTable::EvalVector(float Time, FParticleRandom* Random, bool bLockAxes) const
{
	if (Values.Num() < 3 || EntryCount == 0)
		return FVector::ZeroVector;

	if (Op == EDistributionOp::Constant)
		return FVector(Values[0], Values[1], Values[2]);

	if (Op == EDistributionOp::Random)
	{
		if (Values.Num() < 6)
			return FVector(Values[0], Values[1], Values[2]);
		if (bLockAxes)
		{
			const float Alpha = RandomRangeOrMid(Random, 0.0f, 1.0f);
			return FVector(
				Values[0] + (Values[3] - Values[0]) * Alpha,
				Values[1] + (Values[4] - Values[1]) * Alpha,
				Values[2] + (Values[5] - Values[2]) * Alpha);
		}
		return FVector(
			RandomRangeOrMid(Random, Values[0], Values[3]),
			RandomRangeOrMid(Random, Values[1], Values[4]),
			RandomRangeOrMid(Random, Values[2], Values[5]));
	}

	const float ClampedT = std::clamp(Time, 0.0f, 1.0f);
	const float Scaled = std::clamp(ClampedT * TimeScale + TimeBias, 0.0f, static_cast<float>(EntryCount - 1));
	const uint32 Idx0 = static_cast<uint32>(Scaled);
	const uint32 Idx1 = std::min(Idx0 + 1u, EntryCount - 1u);
	const float Frac = Scaled - static_cast<float>(Idx0);
	const uint32 Base0 = Idx0 * EntryStride;
	const uint32 Base1 = Idx1 * EntryStride;

	const FVector MinVec(
		Values[Base0 + 0] + (Values[Base1 + 0] - Values[Base0 + 0]) * Frac,
		Values[Base0 + 1] + (Values[Base1 + 1] - Values[Base0 + 1]) * Frac,
		Values[Base0 + 2] + (Values[Base1 + 2] - Values[Base0 + 2]) * Frac);

	if (Op == EDistributionOp::ConstantCurve)
		return MinVec;

	const FVector MaxVec(
		Values[Base0 + SubEntryStride + 0] + (Values[Base1 + SubEntryStride + 0] - Values[Base0 + SubEntryStride + 0]) * Frac,
		Values[Base0 + SubEntryStride + 1] + (Values[Base1 + SubEntryStride + 1] - Values[Base0 + SubEntryStride + 1]) * Frac,
		Values[Base0 + SubEntryStride + 2] + (Values[Base1 + SubEntryStride + 2] - Values[Base0 + SubEntryStride + 2]) * Frac);

	if (bLockAxes)
	{
		const float Alpha = RandomRangeOrMid(Random, 0.0f, 1.0f);
		return FVector(
			MinVec.X + (MaxVec.X - MinVec.X) * Alpha,
			MinVec.Y + (MaxVec.Y - MinVec.Y) * Alpha,
			MinVec.Z + (MaxVec.Z - MinVec.Z) * Alpha);
	}
	return FVector(
		RandomRangeOrMid(Random, MinVec.X, MaxVec.X),
		RandomRangeOrMid(Random, MinVec.Y, MaxVec.Y),
		RandomRangeOrMid(Random, MinVec.Z, MaxVec.Z));
}

float UDistributionFloat::GetValue(float, const FParticleInstanceParameters*, FParticleRandom*) const
{
	return 0.0f;
}

void UDistributionFloat::GetOutRange(float& OutMin, float& OutMax) const
{
	OutMin = 0.0f;
	OutMax = 0.0f;
}

void UDistributionFloat::SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
	{
		const float T = SampleCount > 1 ? static_cast<float>(i) / static_cast<float>(SampleCount - 1) : 0.0f;
		OutValues[i] = GetValue(T, nullptr, nullptr);
	}
}

const FParticleDistributionLookupTable& UDistributionFloat::EnsureLookupTable() const
{
	if (bLookupDirty)
	{
		LookupTable = FParticleDistributionLookupTable{};
		BuildLookupTable(LookupTable);
		if (bOptimizeParticleLookupTables && LookupTable.EntryCount > 1)
			OptimizeLookupTable(LookupTable, ParticleLookupOptimizeErrorThreshold);
		bLookupDirty = false;
	}
	return LookupTable;
}

void UDistributionFloat::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Constant;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 1;
	OutTable.SubEntryStride = 1;
	OutTable.Values = { 0.0f };
}

float UDistributionFloatConstant::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalFloat(Time, Random);
}

void UDistributionFloatConstant::GetOutRange(float& OutMin, float& OutMax) const
{
	OutMin = Constant;
	OutMax = Constant;
}

void UDistributionFloatConstant::SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
		OutValues[i] = Constant;
}

void UDistributionFloatConstant::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Constant;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 1;
	OutTable.SubEntryStride = 1;
	OutTable.Values = { Constant };
}

float UDistributionFloatUniform::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalFloat(Time, Random);
}

void UDistributionFloatUniform::GetOutRange(float& OutMin, float& OutMax) const
{
	OutMin = std::min(Min, Max);
	OutMax = std::max(Min, Max);
}

void UDistributionFloatUniform::SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const
{
	const float Mid = (Min + Max) * 0.5f;
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
		OutValues[i] = Mid;
}

void UDistributionFloatUniform::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Random;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 2;
	OutTable.SubEntryStride = 1;
	OutTable.Values = { Min, Max };
}

UDistributionFloatConstantCurve::UDistributionFloatConstantCurve()
{
	Curve = CreateFlatCurve(this, 1.0f);
}

float UDistributionFloatConstantCurve::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalFloat(Time, Random);
}

void UDistributionFloatConstantCurve::GetOutRange(float& OutMin, float& OutMax) const
{
	const FParticleDistributionLookupTable& Table = EnsureLookupTable();
	if (Table.Values.IsEmpty())
	{
		OutMin = 0.0f;
		OutMax = 0.0f;
		return;
	}
	OutMin = Table.Values[0];
	OutMax = Table.Values[0];
	for (float V : Table.Values)
	{
		OutMin = std::min(OutMin, V);
		OutMax = std::max(OutMax, V);
	}
}

void UDistributionFloatConstantCurve::CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const
{
	if (Curve)
		OutTracks.Add({ Prefix, Curve, Color });
}

void UDistributionFloatConstantCurve::SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
	{
		const float T = SampleCount > 1 ? static_cast<float>(i) / static_cast<float>(SampleCount - 1) : 0.0f;
		OutValues[i] = EvalCurveSafe(Curve, T, 1.0f);
	}
}

void UDistributionFloatConstantCurve::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::ConstantCurve;
	OutTable.EntryCount = CurveLookupSampleCount;
	OutTable.EntryStride = 1;
	OutTable.SubEntryStride = 1;
	OutTable.TimeScale = static_cast<float>(CurveLookupSampleCount - 1);
	OutTable.TimeBias = 0.0f;
	SampleCurveValues(CurveLookupSampleCount, OutTable.Values);
}

UDistributionFloatUniformCurve::UDistributionFloatUniformCurve()
{
	MinCurve = CreateFlatCurve(this, 1.0f);
	MaxCurve = CreateFlatCurve(this, 1.0f);
}

float UDistributionFloatUniformCurve::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalFloat(Time, Random);
}

void UDistributionFloatUniformCurve::GetOutRange(float& OutMin, float& OutMax) const
{
	const FParticleDistributionLookupTable& Table = EnsureLookupTable();
	if (Table.Values.IsEmpty())
	{
		OutMin = 0.0f;
		OutMax = 0.0f;
		return;
	}
	OutMin = Table.Values[0];
	OutMax = Table.Values[0];
	for (float V : Table.Values)
	{
		OutMin = std::min(OutMin, V);
		OutMax = std::max(OutMax, V);
	}
}

void UDistributionFloatUniformCurve::CollectCurves(const FString& Prefix, const FColor& Color, TArray<FParticleCurveTrack>& OutTracks) const
{
	if (MinCurve)
		OutTracks.Add({ Prefix + " Min", MinCurve, Color });
	if (MaxCurve)
		OutTracks.Add({ Prefix + " Max", MaxCurve, FColor(255, 210, 90, 255) });
}

void UDistributionFloatUniformCurve::SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
	{
		const float T = SampleCount > 1 ? static_cast<float>(i) / static_cast<float>(SampleCount - 1) : 0.0f;
		const float MinV = EvalCurveSafe(MinCurve, T, 1.0f);
		const float MaxV = EvalCurveSafe(MaxCurve, T, 1.0f);
		OutValues[i] = (MinV + MaxV) * 0.5f;
	}
}

void UDistributionFloatUniformCurve::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::UniformCurve;
	OutTable.EntryCount = CurveLookupSampleCount;
	OutTable.EntryStride = 2;
	OutTable.SubEntryStride = 1;
	OutTable.TimeScale = static_cast<float>(CurveLookupSampleCount - 1);
	OutTable.TimeBias = 0.0f;
	OutTable.Values.SetNum(CurveLookupSampleCount * 2);
	for (uint32 i = 0; i < CurveLookupSampleCount; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(CurveLookupSampleCount - 1);
		OutTable.Values[i * 2 + 0] = EvalCurveSafe(MinCurve, T, 1.0f);
		OutTable.Values[i * 2 + 1] = EvalCurveSafe(MaxCurve, T, 1.0f);
	}
}

float UDistributionFloatParticleParameter::GetValue(float, const FParticleInstanceParameters* Params, FParticleRandom*) const
{
	if (Params && !ParameterName.empty())
		return Params->GetFloat(ParameterName, Constant);
	return Constant;
}

void UDistributionFloatParticleParameter::GetOutRange(float& OutMin, float& OutMax) const
{
	OutMin = Constant;
	OutMax = Constant;
}

void UDistributionFloatParticleParameter::SampleCurveValues(uint32 SampleCount, TArray<float>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
		OutValues[i] = Constant;
}

void UDistributionFloatParticleParameter::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Constant;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 1;
	OutTable.SubEntryStride = 1;
	OutTable.Values = { Constant };
}

FVector UDistributionVector::GetValue(float, const FParticleInstanceParameters*, FParticleRandom*) const
{
	return FVector::ZeroVector;
}

void UDistributionVector::GetOutRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = FVector::ZeroVector;
	OutMax = FVector::ZeroVector;
}

void UDistributionVector::SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
	{
		const float T = SampleCount > 1 ? static_cast<float>(i) / static_cast<float>(SampleCount - 1) : 0.0f;
		OutValues[i] = GetValue(T, nullptr, nullptr);
	}
}

const FParticleDistributionLookupTable& UDistributionVector::EnsureLookupTable() const
{
	if (bLookupDirty)
	{
		LookupTable = FParticleDistributionLookupTable{};
		BuildLookupTable(LookupTable);
		if (bOptimizeParticleLookupTables && LookupTable.EntryCount > 1)
			OptimizeLookupTable(LookupTable, ParticleLookupOptimizeErrorThreshold);
		bLookupDirty = false;
	}
	return LookupTable;
}

void UDistributionVector::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Constant;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 3;
	OutTable.SubEntryStride = 3;
	OutTable.Values = { 0.0f, 0.0f, 0.0f };
}

FVector UDistributionVectorConstant::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalVector(Time, Random, false);
}

void UDistributionVectorConstant::GetOutRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = Constant;
	OutMax = Constant;
}

void UDistributionVectorConstant::SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
		OutValues[i] = Constant;
}

void UDistributionVectorConstant::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Constant;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 3;
	OutTable.SubEntryStride = 3;
	OutTable.Values = { Constant.X, Constant.Y, Constant.Z };
}

FVector UDistributionVectorUniform::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalVector(Time, Random, bLockAxes);
}

void UDistributionVectorUniform::GetOutRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = FVector(std::min(Min.X, Max.X), std::min(Min.Y, Max.Y), std::min(Min.Z, Max.Z));
	OutMax = FVector(std::max(Min.X, Max.X), std::max(Min.Y, Max.Y), std::max(Min.Z, Max.Z));
}

void UDistributionVectorUniform::SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const
{
	const FVector Mid = (Min + Max) * 0.5f;
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
		OutValues[i] = Mid;
}

void UDistributionVectorUniform::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Random;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 6;
	OutTable.SubEntryStride = 3;
	OutTable.Values = { Min.X, Min.Y, Min.Z, Max.X, Max.Y, Max.Z };
}

UDistributionVectorConstantCurve::UDistributionVectorConstantCurve()
{
	CurveX = CreateFlatCurve(this, 1.0f);
	CurveY = CreateFlatCurve(this, 1.0f);
	CurveZ = CreateFlatCurve(this, 1.0f);
}

FVector UDistributionVectorConstantCurve::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalVector(Time, Random, false);
}

void UDistributionVectorConstantCurve::GetOutRange(FVector& OutMin, FVector& OutMax) const
{
	const FParticleDistributionLookupTable& Table = EnsureLookupTable();
	if (Table.Values.Num() < 3)
	{
		OutMin = FVector::ZeroVector;
		OutMax = FVector::ZeroVector;
		return;
	}
	OutMin = FVector(Table.Values[0], Table.Values[1], Table.Values[2]);
	OutMax = OutMin;
	for (uint32 i = 0; i < Table.EntryCount; ++i)
	{
		const uint32 Base = i * 3;
		OutMin.X = std::min(OutMin.X, Table.Values[Base + 0]);
		OutMin.Y = std::min(OutMin.Y, Table.Values[Base + 1]);
		OutMin.Z = std::min(OutMin.Z, Table.Values[Base + 2]);
		OutMax.X = std::max(OutMax.X, Table.Values[Base + 0]);
		OutMax.Y = std::max(OutMax.Y, Table.Values[Base + 1]);
		OutMax.Z = std::max(OutMax.Z, Table.Values[Base + 2]);
	}
}

void UDistributionVectorConstantCurve::CollectCurves(const FString& Prefix, const FColor&, TArray<FParticleCurveTrack>& OutTracks) const
{
	if (CurveX) OutTracks.Add({ Prefix + " X", CurveX, FColor(255, 80, 80, 255) });
	if (CurveY) OutTracks.Add({ Prefix + " Y", CurveY, FColor(80, 255, 80, 255) });
	if (CurveZ) OutTracks.Add({ Prefix + " Z", CurveZ, FColor(80, 140, 255, 255) });
}

void UDistributionVectorConstantCurve::SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
	{
		const float T = SampleCount > 1 ? static_cast<float>(i) / static_cast<float>(SampleCount - 1) : 0.0f;
		OutValues[i] = FVector(
			EvalCurveSafe(CurveX, T, 1.0f),
			EvalCurveSafe(CurveY, T, 1.0f),
			EvalCurveSafe(CurveZ, T, 1.0f));
	}
}

void UDistributionVectorConstantCurve::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::ConstantCurve;
	OutTable.EntryCount = CurveLookupSampleCount;
	OutTable.EntryStride = 3;
	OutTable.SubEntryStride = 3;
	OutTable.TimeScale = static_cast<float>(CurveLookupSampleCount - 1);
	OutTable.TimeBias = 0.0f;
	OutTable.Values.SetNum(CurveLookupSampleCount * 3);
	for (uint32 i = 0; i < CurveLookupSampleCount; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(CurveLookupSampleCount - 1);
		OutTable.Values[i * 3 + 0] = EvalCurveSafe(CurveX, T, 1.0f);
		OutTable.Values[i * 3 + 1] = EvalCurveSafe(CurveY, T, 1.0f);
		OutTable.Values[i * 3 + 2] = EvalCurveSafe(CurveZ, T, 1.0f);
	}
}

UDistributionVectorUniformCurve::UDistributionVectorUniformCurve()
{
	MinCurveX = CreateFlatCurve(this, 1.0f);
	MinCurveY = CreateFlatCurve(this, 1.0f);
	MinCurveZ = CreateFlatCurve(this, 1.0f);
	MaxCurveX = CreateFlatCurve(this, 1.0f);
	MaxCurveY = CreateFlatCurve(this, 1.0f);
	MaxCurveZ = CreateFlatCurve(this, 1.0f);
}

FVector UDistributionVectorUniformCurve::GetValue(float Time, const FParticleInstanceParameters*, FParticleRandom* Random) const
{
	return EnsureLookupTable().EvalVector(Time, Random, bLockAxes);
}

void UDistributionVectorUniformCurve::GetOutRange(FVector& OutMin, FVector& OutMax) const
{
	const FParticleDistributionLookupTable& Table = EnsureLookupTable();
	if (Table.Values.Num() < 6)
	{
		OutMin = FVector::ZeroVector;
		OutMax = FVector::ZeroVector;
		return;
	}
	OutMin = FVector(Table.Values[0], Table.Values[1], Table.Values[2]);
	OutMax = OutMin;
	for (uint32 i = 0; i < Table.EntryCount; ++i)
	{
		const uint32 Base = i * 6;
		OutMin.X = std::min({ OutMin.X, Table.Values[Base + 0], Table.Values[Base + 3] });
		OutMin.Y = std::min({ OutMin.Y, Table.Values[Base + 1], Table.Values[Base + 4] });
		OutMin.Z = std::min({ OutMin.Z, Table.Values[Base + 2], Table.Values[Base + 5] });
		OutMax.X = std::max({ OutMax.X, Table.Values[Base + 0], Table.Values[Base + 3] });
		OutMax.Y = std::max({ OutMax.Y, Table.Values[Base + 1], Table.Values[Base + 4] });
		OutMax.Z = std::max({ OutMax.Z, Table.Values[Base + 2], Table.Values[Base + 5] });
	}
}

void UDistributionVectorUniformCurve::CollectCurves(const FString& Prefix, const FColor&, TArray<FParticleCurveTrack>& OutTracks) const
{
	if (MinCurveX) OutTracks.Add({ Prefix + " MinX", MinCurveX, FColor(255, 80, 80, 255) });
	if (MinCurveY) OutTracks.Add({ Prefix + " MinY", MinCurveY, FColor(80, 255, 80, 255) });
	if (MinCurveZ) OutTracks.Add({ Prefix + " MinZ", MinCurveZ, FColor(80, 140, 255, 255) });
	if (MaxCurveX) OutTracks.Add({ Prefix + " MaxX", MaxCurveX, FColor(255, 160, 160, 255) });
	if (MaxCurveY) OutTracks.Add({ Prefix + " MaxY", MaxCurveY, FColor(160, 255, 160, 255) });
	if (MaxCurveZ) OutTracks.Add({ Prefix + " MaxZ", MaxCurveZ, FColor(160, 200, 255, 255) });
}

void UDistributionVectorUniformCurve::SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
	{
		const float T = SampleCount > 1 ? static_cast<float>(i) / static_cast<float>(SampleCount - 1) : 0.0f;
		const float X = (EvalCurveSafe(MinCurveX, T, 1.0f) + EvalCurveSafe(MaxCurveX, T, 1.0f)) * 0.5f;
		const float Y = (EvalCurveSafe(MinCurveY, T, 1.0f) + EvalCurveSafe(MaxCurveY, T, 1.0f)) * 0.5f;
		const float Z = (EvalCurveSafe(MinCurveZ, T, 1.0f) + EvalCurveSafe(MaxCurveZ, T, 1.0f)) * 0.5f;
		OutValues[i] = FVector(X, Y, Z);
	}
}

void UDistributionVectorUniformCurve::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::UniformCurve;
	OutTable.EntryCount = CurveLookupSampleCount;
	OutTable.EntryStride = 6;
	OutTable.SubEntryStride = 3;
	OutTable.TimeScale = static_cast<float>(CurveLookupSampleCount - 1);
	OutTable.TimeBias = 0.0f;
	OutTable.Values.SetNum(CurveLookupSampleCount * 6);
	for (uint32 i = 0; i < CurveLookupSampleCount; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(CurveLookupSampleCount - 1);
		OutTable.Values[i * 6 + 0] = EvalCurveSafe(MinCurveX, T, 1.0f);
		OutTable.Values[i * 6 + 1] = EvalCurveSafe(MinCurveY, T, 1.0f);
		OutTable.Values[i * 6 + 2] = EvalCurveSafe(MinCurveZ, T, 1.0f);
		OutTable.Values[i * 6 + 3] = EvalCurveSafe(MaxCurveX, T, 1.0f);
		OutTable.Values[i * 6 + 4] = EvalCurveSafe(MaxCurveY, T, 1.0f);
		OutTable.Values[i * 6 + 5] = EvalCurveSafe(MaxCurveZ, T, 1.0f);
	}
}

FVector UDistributionVectorParticleParameter::GetValue(float, const FParticleInstanceParameters* Params, FParticleRandom*) const
{
	if (Params && !ParameterName.empty())
		return Params->GetVector(ParameterName, Constant);
	return Constant;
}

void UDistributionVectorParticleParameter::GetOutRange(FVector& OutMin, FVector& OutMax) const
{
	OutMin = Constant;
	OutMax = Constant;
}

void UDistributionVectorParticleParameter::SampleCurveVectors(uint32 SampleCount, TArray<FVector>& OutValues) const
{
	OutValues.SetNum(SampleCount);
	for (uint32 i = 0; i < SampleCount; ++i)
		OutValues[i] = Constant;
}

void UDistributionVectorParticleParameter::BuildLookupTable(FParticleDistributionLookupTable& OutTable) const
{
	OutTable.Op = FParticleDistributionLookupTable::EDistributionOp::Constant;
	OutTable.EntryCount = 1;
	OutTable.EntryStride = 3;
	OutTable.SubEntryStride = 3;
	OutTable.Values = { Constant.X, Constant.Y, Constant.Z };
}

UDistributionFloat* CreateFloatDistributionByMode(UObject* Outer, int32 ModeIndex, float InitialValue)
{
	const EDistributionMode Mode = static_cast<EDistributionMode>(std::clamp(ModeIndex, 0, 4));
	if (Mode == EDistributionMode::Uniform)
	{
		UDistributionFloatUniform* Dist = NewObject<UDistributionFloatUniform>(Outer);
		Dist->Min = InitialValue;
		Dist->Max = InitialValue;
		return Dist;
	}
	if (Mode == EDistributionMode::ConstantCurve)
	{
		UDistributionFloatConstantCurve* Dist = NewObject<UDistributionFloatConstantCurve>(Outer);
		Dist->Curve->Reset();
		Dist->Curve->AddKey(0.0f, InitialValue);
		Dist->Curve->AddKey(1.0f, InitialValue);
		return Dist;
	}
	if (Mode == EDistributionMode::UniformCurve)
	{
		UDistributionFloatUniformCurve* Dist = NewObject<UDistributionFloatUniformCurve>(Outer);
		Dist->MinCurve->Reset();
		Dist->MinCurve->AddKey(0.0f, InitialValue);
		Dist->MinCurve->AddKey(1.0f, InitialValue);
		Dist->MaxCurve->Reset();
		Dist->MaxCurve->AddKey(0.0f, InitialValue);
		Dist->MaxCurve->AddKey(1.0f, InitialValue);
		return Dist;
	}
	if (Mode == EDistributionMode::Parameter)
	{
		UDistributionFloatParticleParameter* Dist = NewObject<UDistributionFloatParticleParameter>(Outer);
		Dist->Constant = InitialValue;
		return Dist;
	}
	UDistributionFloatConstant* Dist = NewObject<UDistributionFloatConstant>(Outer);
	Dist->Constant = InitialValue;
	return Dist;
}

UDistributionVector* CreateVectorDistributionByMode(UObject* Outer, int32 ModeIndex, const FVector& InitialValue)
{
	const EDistributionMode Mode = static_cast<EDistributionMode>(std::clamp(ModeIndex, 0, 4));
	if (Mode == EDistributionMode::Uniform)
	{
		UDistributionVectorUniform* Dist = NewObject<UDistributionVectorUniform>(Outer);
		Dist->Min = InitialValue;
		Dist->Max = InitialValue;
		return Dist;
	}
	if (Mode == EDistributionMode::ConstantCurve)
	{
		UDistributionVectorConstantCurve* Dist = NewObject<UDistributionVectorConstantCurve>(Outer);
		Dist->CurveX->Reset(); Dist->CurveX->AddKey(0.0f, InitialValue.X); Dist->CurveX->AddKey(1.0f, InitialValue.X);
		Dist->CurveY->Reset(); Dist->CurveY->AddKey(0.0f, InitialValue.Y); Dist->CurveY->AddKey(1.0f, InitialValue.Y);
		Dist->CurveZ->Reset(); Dist->CurveZ->AddKey(0.0f, InitialValue.Z); Dist->CurveZ->AddKey(1.0f, InitialValue.Z);
		return Dist;
	}
	if (Mode == EDistributionMode::UniformCurve)
	{
		UDistributionVectorUniformCurve* Dist = NewObject<UDistributionVectorUniformCurve>(Outer);
		Dist->MinCurveX->Reset(); Dist->MinCurveX->AddKey(0.0f, InitialValue.X); Dist->MinCurveX->AddKey(1.0f, InitialValue.X);
		Dist->MinCurveY->Reset(); Dist->MinCurveY->AddKey(0.0f, InitialValue.Y); Dist->MinCurveY->AddKey(1.0f, InitialValue.Y);
		Dist->MinCurveZ->Reset(); Dist->MinCurveZ->AddKey(0.0f, InitialValue.Z); Dist->MinCurveZ->AddKey(1.0f, InitialValue.Z);
		Dist->MaxCurveX->Reset(); Dist->MaxCurveX->AddKey(0.0f, InitialValue.X); Dist->MaxCurveX->AddKey(1.0f, InitialValue.X);
		Dist->MaxCurveY->Reset(); Dist->MaxCurveY->AddKey(0.0f, InitialValue.Y); Dist->MaxCurveY->AddKey(1.0f, InitialValue.Y);
		Dist->MaxCurveZ->Reset(); Dist->MaxCurveZ->AddKey(0.0f, InitialValue.Z); Dist->MaxCurveZ->AddKey(1.0f, InitialValue.Z);
		return Dist;
	}
	if (Mode == EDistributionMode::Parameter)
	{
		UDistributionVectorParticleParameter* Dist = NewObject<UDistributionVectorParticleParameter>(Outer);
		Dist->Constant = InitialValue;
		return Dist;
	}
	UDistributionVectorConstant* Dist = NewObject<UDistributionVectorConstant>(Outer);
	Dist->Constant = InitialValue;
	return Dist;
}

UDistributionFloat* ConvertFloatDistributionMode(UObject* Outer, UDistributionFloat* Current, int32 NewModeIndex)
{
	if (!Current)
		return CreateFloatDistributionByMode(Outer, NewModeIndex, 1.0f);
	if (Current->GetModeIndex() == NewModeIndex)
		return Current;

	float MinV = 1.0f;
	float MaxV = 1.0f;
	Current->GetOutRange(MinV, MaxV);
	const float MidV = (MinV + MaxV) * 0.5f;

	UDistributionFloat* Next = CreateFloatDistributionByMode(Outer, NewModeIndex, MidV);
	if (NewModeIndex == static_cast<int32>(EDistributionMode::Uniform))
	{
		UDistributionFloatUniform* Uni = static_cast<UDistributionFloatUniform*>(Next);
		Uni->Min = MinV;
		Uni->Max = MaxV;
	}
	else if (NewModeIndex == static_cast<int32>(EDistributionMode::UniformCurve))
	{
		UDistributionFloatUniformCurve* UniC = static_cast<UDistributionFloatUniformCurve*>(Next);
		UniC->MinCurve->Reset(); UniC->MinCurve->AddKey(0.0f, MinV); UniC->MinCurve->AddKey(1.0f, MinV);
		UniC->MaxCurve->Reset(); UniC->MaxCurve->AddKey(0.0f, MaxV); UniC->MaxCurve->AddKey(1.0f, MaxV);
	}
	Next->InvalidateLookupTable();
	return Next;
}

UDistributionVector* ConvertVectorDistributionMode(UObject* Outer, UDistributionVector* Current, int32 NewModeIndex)
{
	if (!Current)
		return CreateVectorDistributionByMode(Outer, NewModeIndex, FVector::OneVector);
	if (Current->GetModeIndex() == NewModeIndex)
		return Current;

	FVector MinV = FVector::OneVector;
	FVector MaxV = FVector::OneVector;
	Current->GetOutRange(MinV, MaxV);
	const FVector MidV = (MinV + MaxV) * 0.5f;

	UDistributionVector* Next = CreateVectorDistributionByMode(Outer, NewModeIndex, MidV);
	if (NewModeIndex == static_cast<int32>(EDistributionMode::Uniform))
	{
		UDistributionVectorUniform* Uni = static_cast<UDistributionVectorUniform*>(Next);
		Uni->Min = MinV;
		Uni->Max = MaxV;
	}
	else if (NewModeIndex == static_cast<int32>(EDistributionMode::UniformCurve))
	{
		UDistributionVectorUniformCurve* UniC = static_cast<UDistributionVectorUniformCurve*>(Next);
		UniC->MinCurveX->Reset(); UniC->MinCurveX->AddKey(0.0f, MinV.X); UniC->MinCurveX->AddKey(1.0f, MinV.X);
		UniC->MinCurveY->Reset(); UniC->MinCurveY->AddKey(0.0f, MinV.Y); UniC->MinCurveY->AddKey(1.0f, MinV.Y);
		UniC->MinCurveZ->Reset(); UniC->MinCurveZ->AddKey(0.0f, MinV.Z); UniC->MinCurveZ->AddKey(1.0f, MinV.Z);
		UniC->MaxCurveX->Reset(); UniC->MaxCurveX->AddKey(0.0f, MaxV.X); UniC->MaxCurveX->AddKey(1.0f, MaxV.X);
		UniC->MaxCurveY->Reset(); UniC->MaxCurveY->AddKey(0.0f, MaxV.Y); UniC->MaxCurveY->AddKey(1.0f, MaxV.Y);
		UniC->MaxCurveZ->Reset(); UniC->MaxCurveZ->AddKey(0.0f, MaxV.Z); UniC->MaxCurveZ->AddKey(1.0f, MaxV.Z);
	}
	Next->InvalidateLookupTable();
	return Next;
}

bool IsParticleLookupOptimizationEnabled()
{
	return bOptimizeParticleLookupTables;
}

void SetParticleLookupOptimizationEnabled(bool bEnable)
{
	bOptimizeParticleLookupTables = bEnable;
}

float GetParticleLookupOptimizeErrorThreshold()
{
	return ParticleLookupOptimizeErrorThreshold;
}

void SetParticleLookupOptimizeErrorThreshold(float ErrorThreshold)
{
	if (std::isfinite(ErrorThreshold))
		ParticleLookupOptimizeErrorThreshold = std::clamp(ErrorThreshold, 0.0001f, 1.0f);
}

void OptimizeCurveAtlasSamples(
	const TArray<FVector4>& FullSamples,
	uint32 ActiveComponents,
	TArray<FVector4>& OutSamples)
{
	const uint32 FullCount = static_cast<uint32>(FullSamples.Num());
	if (!bOptimizeParticleLookupTables || FullCount <= 1)
	{
		OutSamples = FullSamples;
		return;
	}

	const uint32 CompCount = std::clamp<uint32>(ActiveComponents, 1u, 4u);
	TArray<FVector4> Candidate;
	for (uint32 SampleCount = 1; SampleCount < FullCount; SampleCount <<= 1)
	{
		Candidate.SetNum(SampleCount);
		for (uint32 k = 0; k < SampleCount; ++k)
		{
			const float T = SampleCount > 1 ? static_cast<float>(k) / static_cast<float>(SampleCount - 1) : 0.0f;
			Candidate[k] = SampleVector4Array(FullSamples, T);
		}

		float MaxError = 0.0f;
		for (uint32 i = 0; i < FullCount; ++i)
		{
			const float T = FullCount > 1 ? static_cast<float>(i) / static_cast<float>(FullCount - 1) : 0.0f;
			const FVector4 Approx = SampleVector4Array(Candidate, T);
			MaxError = std::max(MaxError, ComputeVector4Distance(Approx, FullSamples[i], CompCount));
		}

		if (MaxError < ParticleLookupOptimizeErrorThreshold)
		{
			OutSamples = std::move(Candidate);
			return;
		}
	}

	OutSamples = FullSamples;
}
