#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Engine/ParticleSceneProxy.h"
#include "Render/SceneRenderData.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "UObject/Casts.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace
{
	void ExpandParticleBox(FBox& InOutBounds, bool& bInOutHasBounds, const FVector& Min, const FVector& Max)
	{
		if (!bInOutHasBounds)
		{
			InOutBounds = FBox{ Min, Max };
			bInOutHasBounds = true;
			return;
		}
		InOutBounds.Min = FVector(
			std::min(InOutBounds.Min.X, Min.X),
			std::min(InOutBounds.Min.Y, Min.Y),
			std::min(InOutBounds.Min.Z, Min.Z));
		InOutBounds.Max = FVector(
			std::max(InOutBounds.Max.X, Max.X),
			std::max(InOutBounds.Max.Y, Max.Y),
			std::max(InOutBounds.Max.Z, Max.Z));
	}

	// 빌드 캐시로부터 GPU 에미터의 예상 최대 도달 범위를 계산한다
	void ComputeGpuEmitterReach(
		UParticleLODLevel* LOD,
		FVector& OutOffsetMin,
		FVector& OutOffsetMax,
		FVector& OutVelReach,
		float& OutHalfDiagSize,
		float& OutMaxLifetime)
	{
		OutOffsetMin = FVector::ZeroVector;
		OutOffsetMax = FVector::ZeroVector;
		OutVelReach = FVector::ZeroVector;
		OutHalfDiagSize = 1.0f;
		OutMaxLifetime = 1.0f;
		if (!LOD)
			return;

		const FParticleEmitterBuildInfo& Build = LOD->EnsureBuilt();
		OutOffsetMin = Build.SpawnLocationMin;
		OutOffsetMax = Build.SpawnLocationMax;
		OutMaxLifetime = std::max(0.05f, Build.MaxLifetime);
		OutVelReach = Build.MaxVelocity * OutMaxLifetime;
		const float FinalSizeX = Build.MaxStartSize.X * Build.MaxLifeScale.X;
		const float FinalSizeY = Build.MaxStartSize.Y * Build.MaxLifeScale.Y;
		OutHalfDiagSize = 0.5f * std::sqrt(FinalSizeX * FinalSizeX + FinalSizeY * FinalSizeY);
	}

	FGpuDistributionDesc MakeGpuDistributionDesc(const FGpuDistributionBuildChannel& Channel)
	{
		FGpuDistributionDesc Desc{};
		if (Channel.Mode == 2 && Channel.AtlasHandle.IsValid())
		{
			Desc.AtlasUVAndMode = FVector4(
				Channel.AtlasHandle.UStart,
				Channel.AtlasHandle.UScale,
				Channel.AtlasHandle.VCoord,
				2.0f);
			Desc.ValueScale = Channel.ValueScale;
			Desc.ValueBias = Channel.ValueBias;
		}
		else if (Channel.Mode == 1)
		{
			Desc.AtlasUVAndMode = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
			Desc.ValueScale = Channel.ConstantValue;
			Desc.ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		}
		return Desc;
	}

	template <typename TModule>
	const TModule* FindEnabledModule(const UParticleLODLevel* LOD)
	{
		if (!LOD)
			return nullptr;
		for (const UParticleModule* Module : LOD->EffectiveModules)
		{
			if (Module && Module->bEnabled)
			{
				if (const TModule* Typed = Cast<TModule>(Module))
					return Typed;
			}
		}
		return nullptr;
	}

	uint32 AdvanceNoiseSeed(uint32 Seed)
	{
		uint32 Next = Seed * 1664525u + 1013904223u;
		return Next ? Next : 1u;
	}

	void ResolveBeamEndpoints(
		const UParticleLODLevel* LOD,
		const UParticleModuleTypeDataBeam* BeamType,
		const FVector& Origin,
		bool bUseLocalSpace,
		const FMatrix& WorldToLocal,
		const FParticleInstanceParameters* InstanceParams,
		FParticleRandom& Random,
		FBeamTypeDataPayload& OutPayload)
	{
		const FVector BaseOrigin = bUseLocalSpace ? FVector::ZeroVector : Origin;
		const UParticleModuleBeamSource* SourceMod = FindEnabledModule<UParticleModuleBeamSource>(LOD);
		const UParticleModuleBeamTarget* TargetMod = FindEnabledModule<UParticleModuleBeamTarget>(LOD);

		FVector SourcePos = BaseOrigin;
		FVector SrcTangent = FVector::ZeroVector;
		if (SourceMod)
		{
			if (SourceMod->SourceMethod == static_cast<int32>(EBeamEndpointMethod::UserSet) && SourceMod->Source)
			{
				SourcePos = BaseOrigin + SourceMod->Source->GetValue(0.0f, InstanceParams, &Random);
			}
			else if (SourceMod->SourceMethod == static_cast<int32>(EBeamEndpointMethod::Parameter))
			{
				if (InstanceParams && !SourceMod->SourceParameterName.empty())
				{
					if (const FVector* ParamVec = InstanceParams->VectorParams.Find(SourceMod->SourceParameterName))
						SourcePos = bUseLocalSpace ? WorldToLocal.TransformPosition(*ParamVec) : *ParamVec;
				}
				else if (SourceMod->Source)
				{
					SourcePos = BaseOrigin + SourceMod->Source->GetValue(0.0f, InstanceParams, &Random);
				}
			}
			if (SourceMod->bUseSourceTangent && SourceMod->SourceTangent)
			{
				const float Strength = SourceMod->SourceStrength ? SourceMod->SourceStrength->GetValue(0.0f, InstanceParams, &Random) : 1.0f;
				SrcTangent = SourceMod->SourceTangent->GetValue(0.0f, InstanceParams, &Random) * Strength;
			}
		}

		FVector TargetPos = SourcePos + FVector(BeamType ? BeamType->Distance : 3.0f, 0.0f, 0.0f);
		FVector DstTangent = FVector::ZeroVector;
		if (BeamType && BeamType->BeamMethod == static_cast<int32>(EBeamMethod::Distance))
		{
			FVector Dir(1.0f, 0.0f, 0.0f);
			if (TargetMod && TargetMod->Target)
			{
				const FVector Candidate = TargetMod->Target->GetValue(0.0f, InstanceParams, &Random);
				if (Candidate.Dot(Candidate) > 1e-8f)
					Dir = Candidate.Normalized();
			}
			TargetPos = SourcePos + Dir * BeamType->Distance;
		}
		else if (TargetMod)
		{
			if (TargetMod->TargetMethod == static_cast<int32>(EBeamEndpointMethod::Emitter))
			{
				TargetPos = BaseOrigin;
			}
			else if (TargetMod->TargetMethod == static_cast<int32>(EBeamEndpointMethod::UserSet) && TargetMod->Target)
			{
				TargetPos = BaseOrigin + TargetMod->Target->GetValue(0.0f, InstanceParams, &Random);
			}
			else if (TargetMod->TargetMethod == static_cast<int32>(EBeamEndpointMethod::Parameter))
			{
				bool bFoundParam = false;
				if (InstanceParams && !TargetMod->TargetParameterName.empty())
				{
					if (const FVector* ParamVec = InstanceParams->VectorParams.Find(TargetMod->TargetParameterName))
					{
						TargetPos = bUseLocalSpace ? WorldToLocal.TransformPosition(*ParamVec) : *ParamVec;
						bFoundParam = true;
					}
				}
				if (!bFoundParam && TargetMod->Target)
					TargetPos = BaseOrigin + TargetMod->Target->GetValue(0.0f, InstanceParams, &Random);
			}
		}

		if (TargetMod && TargetMod->bUseTargetTangent && TargetMod->TargetTangent)
		{
			const float Strength = TargetMod->TargetStrength ? TargetMod->TargetStrength->GetValue(0.0f, InstanceParams, &Random) : 1.0f;
			DstTangent = TargetMod->TargetTangent->GetValue(0.0f, InstanceParams, &Random) * Strength;
		}

		OutPayload.SourcePoint = SourcePos;
		OutPayload.TargetPoint = TargetPos;
		OutPayload.SourceTangent = SrcTangent;
		OutPayload.TargetTangent = DstTangent;
	}

	void BuildBeamStripPoints(
		const FBaseParticle& Particle,
		const FBeamTypeDataPayload& Payload,
		const UParticleModuleTypeDataBeam* BeamType,
		const UParticleModuleBeamNoise* NoiseMod,
		const FParticleInstanceParameters* InstanceParams,
		const FMatrix& LocalToWorld,
		bool bTransformToWorld,
		FBeamTrailStrip& OutStrip) // 어디에, 어떤 폭과 색으로 Beam의 점을 놓을지
	{
		OutStrip.Points.Reset();
		if (!BeamType)
			return;

		const int32 SegCount = std::clamp(BeamType->Segments, 1, 512);
		const float Travel = std::clamp(Payload.TravelRatio, 0.0f, 1.0f);
		const bool bHasSourceTan = Payload.SourceTangent.Dot(Payload.SourceTangent) > 1e-8f;
		const bool bHasTargetTan = Payload.TargetTangent.Dot(Payload.TargetTangent) > 1e-8f;
		const bool bUseCurve = bHasSourceTan || bHasTargetTan;
		const float ChordLen = (Payload.TargetPoint - Payload.SourcePoint).Length();
		const FVector ScaledSrcTan = Payload.SourceTangent * std::max(1.0f, ChordLen);
		const FVector ScaledDstTan = Payload.TargetTangent * std::max(1.0f, ChordLen);

		TArray<FVector> NoiseNodes;
		int32 Freq = 0;
		if (NoiseMod && NoiseMod->bEnabled && NoiseMod->NoiseRange && NoiseMod->Frequency > 0)
		{
			Freq = std::clamp(NoiseMod->Frequency, 1, 256);
			NoiseNodes.SetNum(Freq + 1);
			FParticleRandom NoiseRand{ Payload.NoiseSeed ? Payload.NoiseSeed : 1u, InstanceParams };
			const int32 ModeIdx = NoiseMod->NoiseRange->GetModeIndex();
			const bool bApplyRandomSign = (ModeIdx != static_cast<int32>(EDistributionMode::Uniform))
				&& (ModeIdx != static_cast<int32>(EDistributionMode::UniformCurve));

			for (int32 k = 0; k <= Freq; ++k)
			{
				if (NoiseMod->bLockEndpoints && (k == 0 || k == Freq))
				{
					NoiseNodes[k] = FVector::ZeroVector;
					continue;
				}
				const float Frac = static_cast<float>(k) / static_cast<float>(Freq);
				FVector Offset = NoiseMod->NoiseRange->GetValue(Frac, InstanceParams, &NoiseRand);
				if (bApplyRandomSign)
				{
					Offset.X *= NoiseRand.Range(-1.0f, 1.0f);
					Offset.Y *= NoiseRand.Range(-1.0f, 1.0f);
					Offset.Z *= NoiseRand.Range(-1.0f, 1.0f);
				}
				NoiseNodes[k] = Offset;
			}
		}

		const FVector4 ParticleColor(
			Particle.Color.R / 255.0f,
			Particle.Color.G / 255.0f,
			Particle.Color.B / 255.0f,
			Particle.Color.A / 255.0f);
		const float BaseWidth = std::max(0.001f, std::abs(Particle.Size.X));
		const float Tile = std::max(0.01f, BeamType->TextureTile);

		OutStrip.Points.Reserve(SegCount + 1);
		float AccumDist = 0.0f;
		FVector PrevPos = FVector::ZeroVector;

		for (int32 i = 0; i <= SegCount; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(SegCount);
			const float U = T * Travel;

			FVector Pos;
			if (bUseCurve)
			{
				const float U2 = U * U;
				const float U3 = U2 * U;
				const float H00 = 2.0f * U3 - 3.0f * U2 + 1.0f;
				const float H10 = U3 - 2.0f * U2 + U;
				const float H01 = -2.0f * U3 + 3.0f * U2;
				const float H11 = U3 - U2;
				Pos = Payload.SourcePoint * H00
					+ ScaledSrcTan * H10
					+ Payload.TargetPoint * H01
					+ ScaledDstTan * H11;
			}
			else
			{
				Pos = Payload.SourcePoint + (Payload.TargetPoint - Payload.SourcePoint) * U;
			}

			if (Freq > 0 && !NoiseNodes.IsEmpty())
			{
				const float SampleIdx = U * static_cast<float>(Freq);
				const int32 Idx0 = std::clamp(static_cast<int32>(std::floor(SampleIdx)), 0, Freq);
				const int32 Idx1 = std::min(Idx0 + 1, Freq);
				float Frac = SampleIdx - static_cast<float>(Idx0);
				if (NoiseMod->bSmooth)
				{
					Frac = Frac * Frac * (3.0f - 2.0f * Frac);
				}
				else if (NoiseMod->NoiseTension > 0.0f)
				{
					const float SmoothFrac = Frac * Frac * (3.0f - 2.0f * Frac);
					const float Tension = std::clamp(NoiseMod->NoiseTension, 0.0f, 1.0f);
					Frac = Frac + (SmoothFrac - Frac) * Tension;
				}
				Pos += NoiseNodes[Idx0] + (NoiseNodes[Idx1] - NoiseNodes[Idx0]) * Frac;
			}

			if (bTransformToWorld)
				Pos = LocalToWorld.TransformPosition(Pos);

			if (i > 0)
				AccumDist += (Pos - PrevPos).Length();
			PrevPos = Pos;

			float Taper = 1.0f;
			if (BeamType->TaperMethod != static_cast<int32>(EBeamTaperMethod::None))
			{
				const float Factor = BeamType->TaperFactor ? BeamType->TaperFactor->GetValue(T, InstanceParams, nullptr) : 1.0f;
				const float Scale = BeamType->TaperScale ? BeamType->TaperScale->GetValue(T, InstanceParams, nullptr) : 1.0f;
				const float Evaluated = Factor * Scale;
				if (BeamType->TaperMethod == static_cast<int32>(EBeamTaperMethod::Full))
					Taper = Evaluated;
				else
					Taper = 1.0f + (Evaluated - 1.0f) * T;
			}

			FBeamTrailPoint Pt{};
			Pt.Position = Pos;
			Pt.Width = std::max(0.001f, BaseWidth * std::max(0.0f, Taper));
			Pt.Color = ParticleColor;
			Pt.UpHint = FVector(0.0f, 0.0f, 1.0f);
			Pt.UCoord = (BeamType->TextureTileDistance > 0.0f)
				? (AccumDist / BeamType->TextureTileDistance)
				: (T * Tile);
			OutStrip.Points.Add(Pt);
		}
	}

	struct FRibbonSortItem
	{
		const FBaseParticle* Particle = nullptr;
		const FRibbonTypeDataPayload* Payload = nullptr;
	};

	bool ResolveRibbonEndpoints(
		const UParticleModuleTypeDataRibbon* RibbonType,
		const FParticleInstanceParameters* InstanceParams,
		FVector& OutBase,
		FVector& OutTip)
	{
		if (!RibbonType || !InstanceParams ||
			RibbonType->SourceParameterName.empty() ||
			RibbonType->TargetParameterName.empty())
			return false;

		const FVector* BaseVec = InstanceParams->VectorParams.Find(RibbonType->SourceParameterName);
		const FVector* TipVec = InstanceParams->VectorParams.Find(RibbonType->TargetParameterName);
		if (!BaseVec || !TipVec)
			return false;

		OutBase = *BaseVec;
		OutTip = *TipVec;
		return true;
	}

	FVector ComputeRibbonBladeUp(
		const FVector& BladeVec,
		const FVector& Tangent,
		const FVector& FallbackUp)
	{
		FVector Normal = FVector::Cross(BladeVec, Tangent);
		if (Normal.Dot(Normal) > 1e-8f)
			return Normal.Normalized();
		if (FallbackUp.Dot(FallbackUp) > 1e-8f)
			return FallbackUp.Normalized();
		const FVector AltAxis = (std::abs(BladeVec.Z) < 0.9f) ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
		FVector Ortho = FVector::Cross(BladeVec, AltAxis);
		return (Ortho.Dot(Ortho) > 1e-8f) ? Ortho.Normalized() : FVector(0.0f, 0.0f, 1.0f);
	}

	float EvalRibbonKnotStep(float TPrev, const FVector& A, const FVector& B)
	{
		const float DistSq = (B - A).Dot(B - A);
		return TPrev + std::max(0.001f, std::pow(DistSq, 0.25f));
	}

	FVector EvalCentripetalCatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float Alpha)
	{
		const float T0 = 0.0f;
		const float T1 = EvalRibbonKnotStep(T0, P0, P1);
		const float T2 = EvalRibbonKnotStep(T1, P1, P2);
		const float T3 = EvalRibbonKnotStep(T2, P2, P3);
		const float T = FMath::Lerp(T1, T2, Alpha);
		const FVector A1 = P0 * ((T1 - T) / (T1 - T0)) + P1 * ((T - T0) / (T1 - T0));
		const FVector A2 = P1 * ((T2 - T) / (T2 - T1)) + P2 * ((T - T1) / (T2 - T1));
		const FVector A3 = P2 * ((T3 - T) / (T3 - T2)) + P3 * ((T - T2) / (T3 - T2));
		const FVector B1 = A1 * ((T2 - T) / (T2 - T0)) + A2 * ((T - T0) / (T2 - T0));
		const FVector B2 = A2 * ((T3 - T) / (T3 - T1)) + A3 * ((T - T1) / (T3 - T1));
		return B1 * ((T2 - T) / (T2 - T1)) + B2 * ((T - T1) / (T2 - T1));
	}

	void SmoothRibbonStripPoints(TArray<FBeamTrailPoint>& InOutPoints)
	{
		const int32 RawCount = static_cast<int32>(InOutPoints.Num());
		if (RawCount < 2)
			return;

		TArray<FBeamTrailPoint> Filtered;
		Filtered.Reserve(RawCount);
		Filtered.Add(InOutPoints[0]);
		constexpr float MinDistSq = 0.01f * 0.01f;
		for (int32 i = 1; i < RawCount; ++i)
		{
			const FBeamTrailPoint& Prev = Filtered[Filtered.Num() - 1];
			const FVector Delta = InOutPoints[i].Position - Prev.Position;
			const FVector TipDelta = (InOutPoints[i].Position + InOutPoints[i].EdgeHalfOffset) -
				(Prev.Position + Prev.EdgeHalfOffset);
			if (Delta.Dot(Delta) >= MinDistSq || TipDelta.Dot(TipDelta) >= MinDistSq)
				Filtered.Add(InOutPoints[i]);
			else if (i == RawCount - 1)
				Filtered[Filtered.Num() - 1] = InOutPoints[i];
		}

		const int32 Count = static_cast<int32>(Filtered.Num());
		if (Count < 3)
		{
			InOutPoints = std::move(Filtered);
			return;
		}

		constexpr int32 SubSteps = 6;
		TArray<FBeamTrailPoint> Smoothed;
		Smoothed.Reserve((Count - 1) * SubSteps + 1);
		for (int32 i = 0; i < Count - 1; ++i)
		{
			const FBeamTrailPoint& A0 = Filtered[std::max(0, i - 1)];
			const FBeamTrailPoint& A1 = Filtered[i];
			const FBeamTrailPoint& A2 = Filtered[i + 1];
			const FBeamTrailPoint& A3 = Filtered[std::min(Count - 1, i + 2)];
			Smoothed.Add(A1);

			const bool bExplicit = A1.bUseExplicitEdges && A2.bUseExplicitEdges;
			const FVector B0 = A0.Position - A0.EdgeHalfOffset;
			const FVector B1 = A1.Position - A1.EdgeHalfOffset;
			const FVector B2 = A2.Position - A2.EdgeHalfOffset;
			const FVector B3 = A3.Position - A3.EdgeHalfOffset;
			const FVector E0 = A0.Position + A0.EdgeHalfOffset;
			const FVector E1 = A1.Position + A1.EdgeHalfOffset;
			const FVector E2 = A2.Position + A2.EdgeHalfOffset;
			const FVector E3 = A3.Position + A3.EdgeHalfOffset;

			for (int32 Step = 1; Step < SubSteps; ++Step)
			{
				const float Alpha = static_cast<float>(Step) / static_cast<float>(SubSteps);
				FBeamTrailPoint Mid{};
				if (bExplicit)
				{
					const FVector SubBase = EvalCentripetalCatmullRom(B0, B1, B2, B3, Alpha);
					const FVector SubTip = EvalCentripetalCatmullRom(E0, E1, E2, E3, Alpha);
					Mid.Position = (SubBase + SubTip) * 0.5f;
					Mid.EdgeHalfOffset = (SubTip - SubBase) * 0.5f;
					Mid.Width = std::max(0.001f, (SubTip - SubBase).Length());
					Mid.bUseExplicitEdges = true;
				}
				else
				{
					Mid.Position = EvalCentripetalCatmullRom(A0.Position, A1.Position, A2.Position, A3.Position, Alpha);
					Mid.Width = FMath::Lerp(A1.Width, A2.Width, Alpha);
				}
				FVector InterpUp = A1.UpHint + (A2.UpHint - A1.UpHint) * Alpha;
				Mid.UpHint = (InterpUp.Dot(InterpUp) > 1e-8f) ? InterpUp.Normalized() : A1.UpHint;
				Mid.Color = FVector4(
					FMath::Lerp(A1.Color.X, A2.Color.X, Alpha),
					FMath::Lerp(A1.Color.Y, A2.Color.Y, Alpha),
					FMath::Lerp(A1.Color.Z, A2.Color.Z, Alpha),
					FMath::Lerp(A1.Color.W, A2.Color.W, Alpha));
				Smoothed.Add(Mid);
			}
		}
		Smoothed.Add(Filtered[Count - 1]);
		InOutPoints = std::move(Smoothed);
	}

	void BuildRibbonStrips(
		const FParticleEmitterInstance& Emitter,
		const UParticleLODLevel* LOD,
		const UParticleModuleTypeDataRibbon* RibbonType,
		const FMatrix& LocalToWorld,
		bool bTransformToWorld,
		const FVector& Origin,
		bool bIncludeClipOrigin,
		TArray<FBeamTrailStrip>& OutTrails) // 이미 생성되어 살아 있는 리본 파티클을 연결 가능한 선 목록으로 바꾸는 함수
	{
		OutTrails.Reset();
		if (!LOD || !RibbonType || !Emitter.ParticleData || !Emitter.ParticleIndices || Emitter.ActiveParticles <= 0)
			return;

		const uint32 RibbonOffset = Emitter.GetModuleDataOffset(LOD->EffectiveTypeData);
		if (RibbonOffset == 0)
			return;

		TArray<FRibbonSortItem> Items;
		Items.Reserve(Emitter.ActiveParticles);
		for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index) // 기본 파티클 정보 + payload 수집
		{
			const uint8* Addr = Emitter.ParticleData + static_cast<size_t>(Emitter.ParticleIndices[Index]) * static_cast<size_t>(Emitter.ParticleStride);
			const FBaseParticle* Particle = reinterpret_cast<const FBaseParticle*>(Addr);
			const FRibbonTypeDataPayload* Payload = reinterpret_cast<const FRibbonTypeDataPayload*>(Addr + RibbonOffset);
			Items.Add({ Particle, Payload });
		}

		std::sort(Items.begin(), Items.end(), [](const FRibbonSortItem& A, const FRibbonSortItem& B)
		{
			if (A.Payload->TrailIndex != B.Payload->TrailIndex)
				return A.Payload->TrailIndex < B.Payload->TrailIndex;
			return A.Payload->SpawnOrder < B.Payload->SpawnOrder;
		});

		const int32 MaxTrails = std::max(1, RibbonType->MaxTrailCount);
		const int32 MaxPerTrail = std::max(2, RibbonType->MaxParticleInTrailCount);
		const bool bLocal = LOD->EffectiveRequired && LOD->EffectiveRequired->bUseLocalSpace;

		TArray<FBeamTrailStrip> AllTrails;
		int32 RunStart = 0;
		const int32 TotalItems = static_cast<int32>(Items.Num());
		while (RunStart < TotalItems)
		{
			const int32 TrailId = Items[RunStart].Payload->TrailIndex;
			int32 RunEnd = RunStart + 1;
			while (RunEnd < TotalItems && Items[RunEnd].Payload->TrailIndex == TrailId)
				++RunEnd;

			const int32 ClampedStart = std::max(RunStart, RunEnd - MaxPerTrail);
			const int32 NodeCount = RunEnd - ClampedStart;
			const bool bAppendHead = bIncludeClipOrigin && RibbonType->bClipSourceSegment && (TrailId == Emitter.CurrentTrailIndex);

			if (NodeCount + (bAppendHead ? 1 : 0) >= 2)
			{
				FBeamTrailStrip Strip;
				Strip.Points.Reserve(NodeCount + (bAppendHead ? 1 : 0));

				for (int32 i = ClampedStart; i < RunEnd; ++i)
				{
					const FBaseParticle& P = *Items[i].Particle;
					const FRibbonTypeDataPayload& PL = *Items[i].Payload;

					FBeamTrailPoint Pt{};
					Pt.Position = (bLocal && bTransformToWorld) ? LocalToWorld.TransformPosition(P.Location) : P.Location;
					Pt.UpHint = (bLocal && bTransformToWorld) ? LocalToWorld.TransformVector(PL.SourceUp) : PL.SourceUp;
					Pt.Width = (PL.WidthOverride > 0.0f) ? PL.WidthOverride : std::max(0.001f, std::abs(P.Size.X));
					Pt.EdgeHalfOffset = (bLocal && bTransformToWorld) ? LocalToWorld.TransformVector(PL.EdgeHalfOffset) : PL.EdgeHalfOffset;
					Pt.bUseExplicitEdges = PL.bUseExplicitEdges;
					Pt.Color = FVector4(P.Color.R / 255.0f, P.Color.G / 255.0f, P.Color.B / 255.0f, P.Color.A / 255.0f);
					Strip.Points.Add(Pt);
				}

				if (bAppendHead && !Strip.Points.IsEmpty())
				{
					const FBeamTrailPoint& LastPt = Strip.Points[Strip.Points.Num() - 1];
					FVector ParamBase = FVector::ZeroVector;
					FVector ParamTip = FVector::ZeroVector;
					const bool bHasParamHead = ResolveRibbonEndpoints(RibbonType, Emitter.InstanceParameters, ParamBase, ParamTip);
					const FVector HeadPos = bHasParamHead
						? (ParamBase + ParamTip) * 0.5f
						: (bLocal ? (bTransformToWorld ? LocalToWorld.TransformPosition(FVector::ZeroVector) : FVector::ZeroVector) : Origin);
					const FVector Delta = HeadPos - LastPt.Position;
					if (Delta.Dot(Delta) > 1e-8f)
					{
						FBeamTrailPoint HeadPt = LastPt;
						HeadPt.Position = HeadPos;
						if (bHasParamHead)
						{
							const FVector BladeVec = ParamTip - ParamBase;
							if (BladeVec.Dot(BladeVec) > 1e-8f)
							{
								HeadPt.Width = BladeVec.Length();
								HeadPt.EdgeHalfOffset = BladeVec * 0.5f;
								HeadPt.bUseExplicitEdges = true;
								HeadPt.UpHint = ComputeRibbonBladeUp(BladeVec, Delta, LastPt.UpHint);
							}
						}
						else
						{
							HeadPt.UpHint = bLocal
								? (bTransformToWorld ? LocalToWorld.TransformVector(FVector(0.0f, 0.0f, 1.0f)) : FVector(0.0f, 0.0f, 1.0f))
								: Emitter.EmitterUpAxis;
						}
						Strip.Points.Add(HeadPt);
					}
				}

				if (Strip.Points.Num() >= 2)
				{
					SmoothRibbonStripPoints(Strip.Points);
					const int32 FinalCount = static_cast<int32>(Strip.Points.Num());
					float AccumDist = 0.0f;
					for (int32 i = FinalCount - 1; i >= 0; --i)
					{
						if (i < FinalCount - 1)
							AccumDist += (Strip.Points[i].Position - Strip.Points[i + 1].Position).Length();
						if (RibbonType->TilingDistance > 0.0f)
							Strip.Points[i].UCoord = AccumDist / RibbonType->TilingDistance;
						else
							Strip.Points[i].UCoord = static_cast<float>(FinalCount - 1 - i) / static_cast<float>(FinalCount - 1);
					}
					AllTrails.Add(std::move(Strip));
				}
			}

			RunStart = RunEnd;
		}

		const int32 TrailStart = std::max(0, static_cast<int32>(AllTrails.Num()) - MaxTrails);
		for (int32 i = TrailStart; i < static_cast<int32>(AllTrails.Num()); ++i)
			OutTrails.Add(std::move(AllTrails[i]));
	}
}

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	delete[] ParticleData;
	delete[] ParticleIndices;
	ParticleData = nullptr;
	ParticleIndices = nullptr;
}

FParticleEmitterInstance::FParticleEmitterInstance(const FParticleEmitterInstance& Other)
	: SpriteTemplate(Other.SpriteTemplate)
	, CurrentLODLevel(Other.CurrentLODLevel)
	, CurrentLODLevelIndex(Other.CurrentLODLevelIndex)
	, ParticleSize(Other.ParticleSize)
	, ParticleStride(Other.ParticleStride)
	, PayloadOffset(Other.PayloadOffset)
	, ModuleOffsetMap(Other.ModuleOffsetMap)
	, ActiveParticles(Other.ActiveParticles)
	, ParticleCounter(Other.ParticleCounter)
	, MaxActiveParticles(Other.MaxActiveParticles)
	, EmitterTime(Other.EmitterTime)
	, SpawnFraction(Other.SpawnFraction)
	, Random(Other.Random)
	, InstanceParameters(Other.InstanceParameters)
	, PendingGpuSpawns(Other.PendingGpuSpawns)
	, AfterimageSnapshots(Other.AfterimageSnapshots)
	, GpuWriteCursor(0)
	, LastGpuDeltaTime(Other.LastGpuDeltaTime)
	, bGpuResetRequested(true)
	, bHasGpuTrailBoundsCurr(false)
	, bHasGpuTrailBoundsPrev(false)
	, GpuTrailTimer(0.0f)
	, EmitterUpAxis(Other.EmitterUpAxis)
	, EmitterWorldToLocal(Other.EmitterWorldToLocal)
	, PrevEmitterOrigin(Other.PrevEmitterOrigin)
	, PrevRibbonBase(Other.PrevRibbonBase)
	, PrevRibbonTip(Other.PrevRibbonTip)
	, LastRibbonUp(Other.LastRibbonUp)
	, bHasPrevEmitterOrigin(false)
	, bHasPrevRibbonEndpoints(false)
	, SpawnPerUnitBuffer(0.0f)
	, CurrentTrailIndex(Other.CurrentTrailIndex)
	, bCurrentTrailActive(Other.bCurrentTrailActive)
	, bSuppressSpawning(Other.bSuppressSpawning)
	, bForceLooping(Other.bForceLooping)
	, SuppressElapsed(Other.SuppressElapsed)
{
	if (MaxActiveParticles > 0 && ParticleStride > 0 && Other.ParticleData && Other.ParticleIndices)
	{
		const size_t DataBytes = static_cast<size_t>(MaxActiveParticles) * static_cast<size_t>(ParticleStride);
		ParticleData = new uint8[DataBytes];
		std::memcpy(ParticleData, Other.ParticleData, DataBytes);
		ParticleIndices = new uint16[MaxActiveParticles];
		std::memcpy(ParticleIndices, Other.ParticleIndices, static_cast<size_t>(MaxActiveParticles) * sizeof(uint16));
	}
}

FParticleEmitterInstance& FParticleEmitterInstance::operator=(const FParticleEmitterInstance& Other)
{
	if (this == &Other) return *this;
	FParticleEmitterInstance Copy(Other);
	*this = std::move(Copy);
	return *this;
}

FParticleEmitterInstance::FParticleEmitterInstance(FParticleEmitterInstance&& Other) noexcept
	: SpriteTemplate(Other.SpriteTemplate)
	, CurrentLODLevel(Other.CurrentLODLevel)
	, CurrentLODLevelIndex(Other.CurrentLODLevelIndex)
	, ParticleData(Other.ParticleData)
	, ParticleIndices(Other.ParticleIndices)
	, ParticleSize(Other.ParticleSize)
	, ParticleStride(Other.ParticleStride)
	, PayloadOffset(Other.PayloadOffset)
	, ModuleOffsetMap(std::move(Other.ModuleOffsetMap))
	, ActiveParticles(Other.ActiveParticles)
	, ParticleCounter(Other.ParticleCounter)
	, MaxActiveParticles(Other.MaxActiveParticles)
	, EmitterTime(Other.EmitterTime)
	, SpawnFraction(Other.SpawnFraction)
	, Random(Other.Random)
	, InstanceParameters(Other.InstanceParameters)
	, GpuResources(std::move(Other.GpuResources))
	, PendingGpuSpawns(std::move(Other.PendingGpuSpawns))
	, AfterimageSnapshots(std::move(Other.AfterimageSnapshots))
	, GpuWriteCursor(Other.GpuWriteCursor)
	, LastGpuDeltaTime(Other.LastGpuDeltaTime)
	, bGpuResetRequested(Other.bGpuResetRequested)
	, GpuTrailBoundsCurr(Other.GpuTrailBoundsCurr)
	, GpuTrailBoundsPrev(Other.GpuTrailBoundsPrev)
	, bHasGpuTrailBoundsCurr(Other.bHasGpuTrailBoundsCurr)
	, bHasGpuTrailBoundsPrev(Other.bHasGpuTrailBoundsPrev)
	, GpuTrailTimer(Other.GpuTrailTimer)
	, EmitterUpAxis(Other.EmitterUpAxis)
	, EmitterWorldToLocal(Other.EmitterWorldToLocal)
	, PrevEmitterOrigin(Other.PrevEmitterOrigin)
	, PrevRibbonBase(Other.PrevRibbonBase)
	, PrevRibbonTip(Other.PrevRibbonTip)
	, LastRibbonUp(Other.LastRibbonUp)
	, bHasPrevEmitterOrigin(Other.bHasPrevEmitterOrigin)
	, bHasPrevRibbonEndpoints(Other.bHasPrevRibbonEndpoints)
	, SpawnPerUnitBuffer(Other.SpawnPerUnitBuffer)
	, CurrentTrailIndex(Other.CurrentTrailIndex)
	, bCurrentTrailActive(Other.bCurrentTrailActive)
	, bSuppressSpawning(Other.bSuppressSpawning)
	, bForceLooping(Other.bForceLooping)
	, SuppressElapsed(Other.SuppressElapsed)
{
	Other.ParticleData = nullptr;
	Other.ParticleIndices = nullptr;
	Other.ActiveParticles = 0;
	Other.MaxActiveParticles = 0;
}

FParticleEmitterInstance& FParticleEmitterInstance::operator=(FParticleEmitterInstance&& Other) noexcept
{
	if (this == &Other) return *this;
	delete[] ParticleData;
	delete[] ParticleIndices;
	SpriteTemplate = Other.SpriteTemplate;
	CurrentLODLevel = Other.CurrentLODLevel;
	CurrentLODLevelIndex = Other.CurrentLODLevelIndex;
	ParticleData = Other.ParticleData;
	ParticleIndices = Other.ParticleIndices;
	ParticleSize = Other.ParticleSize;
	ParticleStride = Other.ParticleStride;
	PayloadOffset = Other.PayloadOffset;
	ModuleOffsetMap = std::move(Other.ModuleOffsetMap);
	ActiveParticles = Other.ActiveParticles;
	ParticleCounter = Other.ParticleCounter;
	MaxActiveParticles = Other.MaxActiveParticles;
	EmitterTime = Other.EmitterTime;
	SpawnFraction = Other.SpawnFraction;
	Random = Other.Random;
	InstanceParameters = Other.InstanceParameters;
	GpuResources = std::move(Other.GpuResources);
	PendingGpuSpawns = std::move(Other.PendingGpuSpawns);
	AfterimageSnapshots = std::move(Other.AfterimageSnapshots);
	GpuWriteCursor = Other.GpuWriteCursor;
	LastGpuDeltaTime = Other.LastGpuDeltaTime;
	bGpuResetRequested = Other.bGpuResetRequested;
	EmitterUpAxis = Other.EmitterUpAxis;
	EmitterWorldToLocal = Other.EmitterWorldToLocal;
	PrevEmitterOrigin = Other.PrevEmitterOrigin;
	PrevRibbonBase = Other.PrevRibbonBase;
	PrevRibbonTip = Other.PrevRibbonTip;
	LastRibbonUp = Other.LastRibbonUp;
	bHasPrevEmitterOrigin = Other.bHasPrevEmitterOrigin;
	bHasPrevRibbonEndpoints = Other.bHasPrevRibbonEndpoints;
	SpawnPerUnitBuffer = Other.SpawnPerUnitBuffer;
	CurrentTrailIndex = Other.CurrentTrailIndex;
	bCurrentTrailActive = Other.bCurrentTrailActive;
	bSuppressSpawning = Other.bSuppressSpawning;
	bForceLooping = Other.bForceLooping;
	SuppressElapsed = Other.SuppressElapsed;
	Other.ParticleData = nullptr;
	Other.ParticleIndices = nullptr;
	Other.ActiveParticles = 0;
	Other.MaxActiveParticles = 0;
	return *this;
}

void FParticleEmitterInstance::UpdateStride()
{
	int32 NewSize = sizeof(FBaseParticle);
	PayloadOffset = NewSize;
	ModuleOffsetMap.Empty();
	if (CurrentLODLevel)
	{
		if (CurrentLODLevel->EffectiveTypeData && CurrentLODLevel->EffectiveTypeData->bEnabled)
		{
			const uint32 ReqBytes = CurrentLODLevel->EffectiveTypeData->RequiredBytes();
			if (ReqBytes > 0)
			{
				ModuleOffsetMap.Add(CurrentLODLevel->EffectiveTypeData, static_cast<uint32>(NewSize));
				NewSize += static_cast<int32>(ReqBytes);
			}
		}

		for (const UParticleModule* Module : CurrentLODLevel->EffectiveModules)
		{
			if (!Module || !Module->bEnabled) continue;
			const uint32 ReqBytes = Module->RequiredBytes();
			if (ReqBytes > 0)
			{
				ModuleOffsetMap.Add(Module, static_cast<uint32>(NewSize));
				NewSize += static_cast<int32>(ReqBytes);
			}
		}
	}
	// 다음 파티클 시작 주소를 경계에 맞추기 위해 보폭을 올림 정렬
	const int32 NewStride = (NewSize + 15) & ~15;
	if (NewStride == ParticleStride && NewSize == ParticleSize) return;

	if (MaxActiveParticles > 0 && ParticleData && ParticleIndices)
	{
		const size_t NewDataBytes = static_cast<size_t>(MaxActiveParticles) * static_cast<size_t>(NewStride);
		uint8* NewData = new uint8[NewDataBytes];
		std::memset(NewData, 0, NewDataBytes);
		const size_t CopyStride = static_cast<size_t>(std::min(ParticleStride, NewStride));
		for (int32 Index = 0; Index < ActiveParticles; ++Index)
		{
			const uint16 Slot = ParticleIndices[Index];
			const uint8* Src = ParticleData + static_cast<size_t>(Slot) * static_cast<size_t>(ParticleStride);
			uint8* Dst = NewData + static_cast<size_t>(Slot) * static_cast<size_t>(NewStride);
			std::memcpy(Dst, Src, CopyStride);
		}
		delete[] ParticleData;
		ParticleData = NewData;
	}
	ParticleSize = NewSize;
	ParticleStride = NewStride;
}

uint32 FParticleEmitterInstance::GetModuleDataOffset(const UParticleModule* Module) const
{
	if (const uint32* Found = ModuleOffsetMap.Find(Module)) return *Found;
	return 0;
}

void FParticleEmitterInstance::Resize(int32 NewMaxActiveParticles)
{
	const int32 ClampedMax = std::clamp(NewMaxActiveParticles, 0, 65535);
	if (ClampedMax == MaxActiveParticles && (ClampedMax == 0 || (ParticleData && ParticleIndices)))
	{
		if (AfterimageSnapshots.Num() != ClampedMax)
			AfterimageSnapshots.SetNum(ClampedMax);
		return;
	}

	if (ClampedMax == 0)
	{
		AfterimageSnapshots.Reset();
		delete[] ParticleData;
		delete[] ParticleIndices;
		ParticleData = nullptr;
		ParticleIndices = nullptr;
		MaxActiveParticles = 0;
		ActiveParticles = 0;
		return;
	}

	const size_t NewDataBytes = static_cast<size_t>(ClampedMax) * static_cast<size_t>(ParticleStride);
	uint8* NewData = new uint8[NewDataBytes];
	std::memset(NewData, 0, NewDataBytes);
	uint16* NewIndices = new uint16[ClampedMax];
	for (int32 Index = 0; Index < ClampedMax; ++Index)
		NewIndices[Index] = static_cast<uint16>(Index);

	const int32 PreservedCount = std::min(ActiveParticles, ClampedMax);
	TArray<TSharedPtr<FAfterimageSnapshot>> NewSnapshots;
	NewSnapshots.SetNum(ClampedMax);
	if (ParticleData && ParticleIndices && PreservedCount > 0)
	{
		// 축소 시에도 활성 파티클을 앞쪽 슬롯에 연속 배치해 인덱스 일관성을 유지한다
		for (int32 Index = 0; Index < PreservedCount; ++Index)
		{
			const uint8* Src = ParticleData + static_cast<size_t>(ParticleIndices[Index]) * static_cast<size_t>(ParticleStride);
			uint8* Dst = NewData + static_cast<size_t>(Index) * static_cast<size_t>(ParticleStride);
			std::memcpy(Dst, Src, static_cast<size_t>(ParticleStride));
			if (AfterimageSnapshots.IsValidIndex(ParticleIndices[Index]))
				NewSnapshots[Index] = AfterimageSnapshots[ParticleIndices[Index]];
		}
	}

	delete[] ParticleData;
	delete[] ParticleIndices;
	ParticleData = NewData;
	ParticleIndices = NewIndices;
	AfterimageSnapshots = std::move(NewSnapshots);
	MaxActiveParticles = ClampedMax;
	ActiveParticles = PreservedCount;
}

void FParticleEmitterInstance::Initialize(UParticleEmitter* Emitter, uint32 Seed)
{
	GpuResources.reset();
	AfterimageSnapshots.Reset();
	PendingGpuSpawns.Reset();
	GpuWriteCursor = 0;
	LastGpuDeltaTime = 0.0f;
	bGpuResetRequested = true;
	GpuTrailBoundsCurr = FBox{};
	GpuTrailBoundsPrev = FBox{};
	bHasGpuTrailBoundsCurr = false;
	bHasGpuTrailBoundsPrev = false;
	GpuTrailTimer = 0.0f;
	PrevEmitterOrigin = FVector::ZeroVector;
	PrevRibbonBase = FVector::ZeroVector;
	PrevRibbonTip = FVector::ZeroVector;
	LastRibbonUp = FVector(0.0f, 0.0f, 1.0f);
	bHasPrevEmitterOrigin = false;
	bHasPrevRibbonEndpoints = false;
	SpawnPerUnitBuffer = 0.0f;
	CurrentTrailIndex = 0;
	bCurrentTrailActive = true;
	bSuppressSpawning = false;
	bForceLooping = false;
	SuppressElapsed = 0.0f;
	ActiveParticles = 0;
	ParticleCounter = 0;
	EmitterTime = 0.0;
	SpawnFraction = 0.0f;
	SpriteTemplate = Emitter;
	CurrentLODLevel = nullptr;
	CurrentLODLevelIndex = 0;
	ParticleSize = sizeof(FBaseParticle);
	ParticleStride = (ParticleSize + 15) & ~15;
	Random.State = Seed ? Seed : 1;
	Random.InstanceParams = InstanceParameters;
	if (!Emitter) { Resize(0); return; }
	const TArray<UParticleLODLevel*>& LODs = Emitter->GetLODLevels();
	if (LODs.IsEmpty()) { Resize(0); return; }
	CurrentLODLevel = LODs[0];
	if (!CurrentLODLevel) { Resize(0); return; }
	Emitter->CacheEmitterModuleInfo();

	if (!CurrentLODLevel->EffectiveTypeData || !CurrentLODLevel->EffectiveRequired) { Resize(0); return; }
	const EParticleEmitterType Type = CurrentLODLevel->EffectiveTypeData->GetEmitterType();
	if (Type != EParticleEmitterType::Sprite
		&& Type != EParticleEmitterType::GpuSprite
		&& Type != EParticleEmitterType::Mesh
		&& Type != EParticleEmitterType::Beam
		&& Type != EParticleEmitterType::Ribbon
		&& Type != EParticleEmitterType::Afterimage)
	{
		Resize(0);
		return;
	}

	UParticleModuleRequired* Required = CurrentLODLevel->EffectiveRequired;
	if (Type == EParticleEmitterType::GpuSprite)
	{
		Resize(0);
		MaxActiveParticles = std::clamp(Required->MaxParticles, 0, 65535);
		return;
	}
	UpdateStride();
	const int32 MaxAllowed = std::clamp(Required->MaxParticles, 0, 65535);
	Resize(std::min(4, MaxAllowed));
}

void FParticleEmitterInstance::SetLODLevel(int32 NewLODIndex)
{
	if (!SpriteTemplate) return;
	const TArray<UParticleLODLevel*>& LODs = SpriteTemplate->GetLODLevels();
	if (LODs.IsEmpty()) return;

	const int32 ClampedIndex = std::clamp(NewLODIndex, 0, static_cast<int32>(LODs.Num()) - 1);
	if (CurrentLODLevelIndex == ClampedIndex && CurrentLODLevel == LODs[ClampedIndex]) return;
	UParticleLODLevel* NextLOD = LODs[ClampedIndex];

	const auto HasCompatiblePayload = [](const UParticleLODLevel& A, const UParticleLODLevel& B)
	{
		TArray<const UParticleModule*> PayloadA;
		TArray<const UParticleModule*> PayloadB;
		for (const UParticleModule* Module : A.EffectiveModules)
			if (Module && Module->bEnabled && Module->RequiredBytes() > 0) PayloadA.Add(Module);
		for (const UParticleModule* Module : B.EffectiveModules)
			if (Module && Module->bEnabled && Module->RequiredBytes() > 0) PayloadB.Add(Module);

		if (PayloadA.Num() != PayloadB.Num())
		{
			return false;
		}

		for (uint32 Index = 0; Index < PayloadA.Num(); ++Index)
		{
			const UParticleModule* Left = PayloadA[Index];
			const UParticleModule* Right = PayloadB[Index];
			if (Left->LODSlotId != Right->LODSlotId
				|| Left->GetClass() != Right->GetClass()
				|| Left->RequiredBytes() != Right->RequiredBytes())
			{
				return false;
			}
		}
		return true;
	};

	const bool bResetForLOD = (CurrentLODLevel->EffectiveTypeData->GetClass() != NextLOD->EffectiveTypeData->GetClass()
		|| CurrentLODLevel->EffectiveRequired->bUseLocalSpace != NextLOD->EffectiveRequired->bUseLocalSpace
		|| (NextLOD->EffectiveTypeData->GetEmitterType() == EParticleEmitterType::GpuSprite
			&& CurrentLODLevel->EffectiveRequired->MaxParticles != NextLOD->EffectiveRequired->MaxParticles)
		|| !HasCompatiblePayload(*CurrentLODLevel, *NextLOD));

	CurrentLODLevelIndex = ClampedIndex;
	CurrentLODLevel = NextLOD;
	if (!CurrentLODLevel) return;

	UParticleModuleRequired* Required = CurrentLODLevel->EffectiveRequired;
	if (bResetForLOD)
	{
		Reset();
		GpuResources.reset();
	}

	if (CurrentLODLevel->EffectiveTypeData->GetEmitterType() == EParticleEmitterType::GpuSprite)
	{
		Resize(0);
		MaxActiveParticles = std::clamp(Required->MaxParticles, 0, 65535);
		return;
	}
	UpdateStride();
	const int32 MaxAllowed = std::clamp(Required->MaxParticles, 0, 65535);
	Resize(std::min(std::max(4, ActiveParticles), MaxAllowed));
}

void FParticleEmitterInstance::Reset()
{
	ActiveParticles = 0;
	AfterimageSnapshots.Reset();
	AfterimageSnapshots.SetNum(MaxActiveParticles);
	ParticleCounter = 0;
	EmitterTime = 0.0;
	SpawnFraction = 0.0f;
	PendingGpuSpawns.Reset();
	GpuWriteCursor = 0;
	LastGpuDeltaTime = 0.0f;
	bGpuResetRequested = true;
	GpuTrailBoundsCurr = FBox{};
	GpuTrailBoundsPrev = FBox{};
	bHasGpuTrailBoundsCurr = false;
	bHasGpuTrailBoundsPrev = false;
	GpuTrailTimer = 0.0f;
	PrevEmitterOrigin = FVector::ZeroVector;
	PrevRibbonBase = FVector::ZeroVector;
	PrevRibbonTip = FVector::ZeroVector;
	LastRibbonUp = FVector(0.0f, 0.0f, 1.0f);
	bHasPrevEmitterOrigin = false;
	bHasPrevRibbonEndpoints = false;
	SpawnPerUnitBuffer = 0.0f;
	CurrentTrailIndex = 0;
	bCurrentTrailActive = true;
	bSuppressSpawning = false;
	SuppressElapsed = 0.0f;
	if (ParticleIndices)
	{
		for (int32 Index = 0; Index < MaxActiveParticles; ++Index)
			ParticleIndices[Index] = static_cast<uint16>(Index);
	}
}

bool FParticleEmitterInstance::IsGpuEmitter() const
{
	return CurrentLODLevel && CurrentLODLevel->bEnabled && CurrentLODLevel->EffectiveTypeData
		&& CurrentLODLevel->EffectiveTypeData->GetEmitterType() == EParticleEmitterType::GpuSprite;
}

bool FParticleEmitterInstance::HasCompleted() const
{
	if (!CurrentLODLevel || !CurrentLODLevel->bEnabled) return true;
	const UParticleModuleRequired* Required = CurrentLODLevel->EffectiveRequired;
	if (!Required) return true;
	if (bSuppressSpawning)
	{
		if (IsGpuEmitter())
		{
			const FParticleEmitterBuildInfo& Build = CurrentLODLevel->EnsureBuilt();
			return PendingGpuSpawns.IsEmpty() && SuppressElapsed >= std::max(0.05f, Build.MaxLifetime);
		}
		return ActiveParticles == 0;
	}
	const bool bEffectiveLooping = bForceLooping || Required->bLooping;
	if (bEffectiveLooping || Required->EmitterDuration <= 0.0f) return false;
	if (IsGpuEmitter())
		return EmitterTime >= static_cast<double>(Required->EmitterDuration) && PendingGpuSpawns.IsEmpty();
	return EmitterTime >= static_cast<double>(Required->EmitterDuration) && ActiveParticles == 0;
}

void FParticleEmitterInstance::Tick(float DeltaTime, const FVector& Origin, const FParticleTickContext& Context,  USkeletalMeshComponent* AfterimageSource)
{
	if (!CurrentLODLevel || !CurrentLODLevel->bEnabled || !CurrentLODLevel->EffectiveTypeData ||
		!CurrentLODLevel->EffectiveRequired || !CurrentLODLevel->EffectiveSpawn ||
		!std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	if (SpriteTemplate)
	{
		DeltaTime *= std::max(0.0f, SpriteTemplate->PlayRate);
		if (DeltaTime <= 0.0f) return;
	}
	const EParticleEmitterType EmitterType = CurrentLODLevel->EffectiveTypeData->GetEmitterType();
	if (EmitterType != EParticleEmitterType::Sprite && EmitterType != EParticleEmitterType::GpuSprite &&
		EmitterType != EParticleEmitterType::Mesh && EmitterType != EParticleEmitterType::Beam &&
		EmitterType != EParticleEmitterType::Ribbon && EmitterType != EParticleEmitterType::Afterimage) return;
	UParticleModuleRequired* Required = CurrentLODLevel->EffectiveRequired;
	const bool bIsGpu = (EmitterType == EParticleEmitterType::GpuSprite);
	const UParticleModuleEventGenerator* EventGenerator = nullptr;
	const bool bIsBeam = (EmitterType == EParticleEmitterType::Beam);
	const bool bIsRibbon = (EmitterType == EParticleEmitterType::Ribbon);
	const bool bIsAfterimage = (EmitterType == EParticleEmitterType::Afterimage);
	const UParticleModuleTypeDataBeam* BeamType = bIsBeam ? Cast<UParticleModuleTypeDataBeam>(CurrentLODLevel->EffectiveTypeData) : nullptr;
	const UParticleModuleTypeDataRibbon* RibbonType = bIsRibbon ? Cast<UParticleModuleTypeDataRibbon>(CurrentLODLevel->EffectiveTypeData) : nullptr;
	const UParticleModuleBeamNoise* BeamNoise = bIsBeam ? FindEnabledModule<UParticleModuleBeamNoise>(CurrentLODLevel) : nullptr;
	const UParticleModuleSpawnPerUnit* SpawnPerUnitMod = bIsRibbon ? FindEnabledModule<UParticleModuleSpawnPerUnit>(CurrentLODLevel) : nullptr;
	const uint32 TypeDataOffset = (bIsBeam || bIsRibbon) ? GetModuleDataOffset(CurrentLODLevel->EffectiveTypeData) : 0u;
	const int32 RawMaxAllowed = std::clamp(Required->MaxParticles, 0, 65535);
	const int32 MaxAllowed = (bIsBeam && BeamType)
		? std::min(RawMaxAllowed, std::max(1, BeamType->MaxBeamCount))
		: RawMaxAllowed;

	if (bIsGpu)
	{
		MaxActiveParticles = MaxAllowed;
		LastGpuDeltaTime = DeltaTime;
		if (MaxActiveParticles <= 0) return;
	}
	else
	{
		if (MaxAllowed <= 0)
		{
			Resize(0);
			return;
		}
		if (MaxActiveParticles > MaxAllowed)
			Resize(MaxAllowed);
		else if (MaxActiveParticles == 0)
			Resize(std::min(4, MaxAllowed));
		if (!ParticleData || !ParticleIndices || MaxActiveParticles <= 0) return;

		for (auto& Module : CurrentLODLevel->Modules)
		{
			if (UParticleModuleEventGenerator* Event = Cast<UParticleModuleEventGenerator>(Module))
			{
				EventGenerator = Event;
				break;
			}
		}

		// 살아 있는 파티클은 방출이 끝난 뒤에도 수명이 다할 때까지 갱신한다.
		for (int32 Index = 0; Index < ActiveParticles;)
		{
			DECLARE_PARTICLE(Particle, ParticleData + static_cast<size_t>(ParticleIndices[Index]) * static_cast<size_t>(ParticleStride));
			Particle.RelativeTime += DeltaTime / Particle.Lifetime;
			if (Particle.RelativeTime >= 1.0f)
			{
				if (bIsAfterimage && AfterimageSnapshots.IsValidIndex(ParticleIndices[Index]))
					AfterimageSnapshots[ParticleIndices[Index]].reset();
				std::swap(ParticleIndices[Index], ParticleIndices[ActiveParticles - 1]);
				--ActiveParticles;
				continue;
			}
			Particle.Location += Particle.Velocity * DeltaTime;
			Particle.Rotation += Particle.RotationRate * DeltaTime;
			for (const UParticleModule* Module : CurrentLODLevel->UpdateModules)
			{
				if (!Module) continue;
				Module->Update(Particle, DeltaTime, GetModuleDataOffset(Module), InstanceParameters);

				if (const UParticleModuleCollision* Collision = Cast<UParticleModuleCollision>(Module))
				{
					Collision->TickCollision(Particle, DeltaTime, GetModuleDataOffset(Module), Context, EventGenerator);
				}
			}
			if (bIsBeam && BeamType && TypeDataOffset > 0)
			{
				FBeamTypeDataPayload& BeamPayload = *reinterpret_cast<FBeamTypeDataPayload*>(
					reinterpret_cast<uint8*>(&Particle) + TypeDataOffset);
				ResolveBeamEndpoints(CurrentLODLevel, BeamType, Origin, Required->bUseLocalSpace,
					EmitterWorldToLocal, InstanceParameters, Random, BeamPayload);
				if (BeamType->Speed > 0.0f)
				{
					const float BeamDist = (BeamPayload.TargetPoint - BeamPayload.SourcePoint).Length();
					BeamPayload.TravelRatio = std::min(1.0f,
						BeamPayload.TravelRatio + (BeamType->Speed * DeltaTime) / std::max(BeamDist, 0.001f));
				}
				else
				{
					BeamPayload.TravelRatio = 1.0f;
				}
				if (BeamNoise && BeamNoise->bEnabled)
				{
					if (BeamNoise->NoiseLockTime <= 0.0f)
					{
						BeamPayload.NoiseSeed = AdvanceNoiseSeed(BeamPayload.NoiseSeed);
					}
					else
					{
						BeamPayload.NoiseLockTimer += DeltaTime;
						if (BeamPayload.NoiseLockTimer >= BeamNoise->NoiseLockTime)
						{
							BeamPayload.NoiseLockTimer = std::fmod(BeamPayload.NoiseLockTimer, BeamNoise->NoiseLockTime);
							BeamPayload.NoiseSeed = AdvanceNoiseSeed(BeamPayload.NoiseSeed);
						}
					}
				}
			}
			++Index;
		}
	}

	const double StartTime = EmitterTime;
	const double EndTime = StartTime + DeltaTime;
	EmitterTime = EndTime;
	if (bSuppressSpawning)
	{
		SuppressElapsed += DeltaTime;
		return;
	}
	const double Duration = Required->EmitterDuration;
	const bool bEffectiveLooping = bForceLooping || Required->bLooping;
	const double SpawnTime = Duration > 0.0 && !bEffectiveLooping
		? std::clamp(Duration - StartTime, 0.0, static_cast<double>(DeltaTime))
		: static_cast<double>(DeltaTime);
	const UParticleModuleSpawn* SpawnModule = CurrentLODLevel->EffectiveSpawn;
	const int32 Available = bIsGpu ? MaxActiveParticles : std::max(0, MaxAllowed - ActiveParticles);
	int32 ToSpawn = 0;
	int32 UnitSpawnCount = 0;
	bool bMovedAboveTolerance = false;
	FVector CurRibbonBase = FVector::ZeroVector;
	FVector CurRibbonTip = FVector::ZeroVector;
	const bool bUseRibbonParams = bIsRibbon && ResolveRibbonEndpoints(RibbonType, InstanceParameters, CurRibbonBase, CurRibbonTip);
	if (bUseRibbonParams && Required->bUseLocalSpace)
	{
		CurRibbonBase = EmitterWorldToLocal.TransformPosition(CurRibbonBase);
		CurRibbonTip = EmitterWorldToLocal.TransformPosition(CurRibbonTip);
	}
	const FVector EffectiveOrigin = bUseRibbonParams ? (CurRibbonBase + CurRibbonTip) * 0.5f : Origin;
	const FVector PrevOriginForSpawn = PrevEmitterOrigin;
	const FVector PrevBaseForSpawn = PrevRibbonBase;
	const FVector PrevTipForSpawn = PrevRibbonTip;
	const bool bCanInterpolateUnitSpawn = bIsRibbon && bHasPrevEmitterOrigin;
	const bool bCanInterpolateRibbonEndpoints = bUseRibbonParams && bHasPrevRibbonEndpoints;
	const FVector RibbonMoveDir = bCanInterpolateRibbonEndpoints
		? (EffectiveOrigin - ((PrevBaseForSpawn + PrevTipForSpawn) * 0.5f))
		: FVector::ZeroVector;

	if (bIsRibbon && SpawnPerUnitMod && SpawnPerUnitMod->SpawnPerUnit > 0.0f)
	{
		if (bHasPrevEmitterOrigin && SpawnTime > 0.0)
		{
			const float MoveDist = (EffectiveOrigin - PrevEmitterOrigin).Length();
			if (MoveDist > SpawnPerUnitMod->MaxFrameDistance) // 직전 프레임 위치에서 이번 프레임 위치까지 이동한 거리가 MaxFrameDistance 보다 크면 새로운 리본으로 간주
			{
				++CurrentTrailIndex;
				SpawnPerUnitBuffer = 0.0f;
			}
			else if (MoveDist >= SpawnPerUnitMod->MovementTolerance) // 이번 프레임의 이동 거리로 리본 파티클을 몇 개 생성할지 계산
			{
				bMovedAboveTolerance = true;
				SpawnPerUnitBuffer += MoveDist * SpawnPerUnitMod->SpawnPerUnit;
				const float Floored = std::floor(SpawnPerUnitBuffer);
				UnitSpawnCount = std::min(Available, static_cast<int32>(Floored));
				SpawnPerUnitBuffer -= Floored;
				ToSpawn += UnitSpawnCount;
			}
		}
		PrevEmitterOrigin = EffectiveOrigin;
		bHasPrevEmitterOrigin = true;
	}
	if (bUseRibbonParams)
	{
		PrevRibbonBase = CurRibbonBase;
		PrevRibbonTip = CurRibbonTip;
		bHasPrevRibbonEndpoints = true;
	}

	if (SpawnModule->bEnabled)
	{
		for (const FParticleBurst& Burst : SpawnModule->Bursts)
		{
			if (Burst.Count <= 0 || !std::isfinite(Burst.Time) || Burst.Time < 0.0f ||
				(Duration > 0.0 && Burst.Time >= Duration)) continue;
			double Occurrences = 0.0;
			if (Duration > 0.0 && bEffectiveLooping)
			{
				const double FirstCycle = std::max(0.0, std::ceil((StartTime - Burst.Time) / Duration));
				const double LastCycle = std::ceil((EndTime - Burst.Time) / Duration);
				Occurrences = std::max(0.0, LastCycle - FirstCycle);
			}
			else if (StartTime <= Burst.Time && Burst.Time < EndTime)
				Occurrences = 1.0;
			ToSpawn += static_cast<int32>(std::min<double>(Available - ToSpawn, Occurrences * Burst.Count));
		}

		const bool bSkipRateForUnitSpawn = SpawnPerUnitMod && SpawnPerUnitMod->bIgnoreSpawnRateWhenMoving && bMovedAboveTolerance;
		const float Rate = std::max(0.0f, SpawnModule->Rate);
		if (!bSkipRateForUnitSpawn && std::isfinite(Rate) && Rate > 0.0f && SpawnTime > 0.0)
		{
			const double Count = static_cast<double>(SpawnFraction) + static_cast<double>(Rate) * SpawnTime;
			ToSpawn += static_cast<int32>(std::min<double>(std::floor(Count), Available - ToSpawn));
			SpawnFraction = static_cast<float>(Count - std::floor(Count));
		}

		if (bIsBeam && ToSpawn == 0 && ActiveParticles == 0 && Available > 0 && SpawnTime > 0.0 && Rate > 0.0f)
			ToSpawn = 1;
	}
	if (bIsGpu)
	{
		for (int32 Index = 0; Index < ToSpawn; ++Index)
		{
			FBaseParticle Base;
			const FVector BaseOrigin = Required->bUseLocalSpace ? FVector::ZeroVector : Origin;
			Base.Location = BaseOrigin;
			Random.InstanceParams = InstanceParameters;
			for (const UParticleModule* Module : CurrentLODLevel->SpawnModules)
				if (Module) Module->Spawn(Base, Random);
			if (!Required->bUseLocalSpace)
			{
				// 월드 스페이스 스폰 시 로컬 오프셋과 속도를 컴포넌트 회전으로 변환
				const FVector LocalOffset = Base.Location - BaseOrigin;
				Base.Location = BaseOrigin + Context.ComponentToWorld.TransformVector(LocalOffset);
				Base.Velocity = Context.ComponentToWorld.TransformVector(Base.Velocity);
				Base.BaseVelocity = Base.Velocity;
			}
			FGpuParticle GpuP;
			GpuP.Position = Base.Location;
			GpuP.RelativeTime = 0.0f;
			GpuP.Velocity = Base.Velocity;
			GpuP.Lifetime = std::max(Base.Lifetime, 0.001f);
			GpuP.BaseSize = Base.BaseSize;
			GpuP.Rotation = Base.Rotation;
			GpuP.Color = FVector4(Base.Color.R / 255.0f, Base.Color.G / 255.0f, Base.Color.B / 255.0f, Base.Color.A / 255.0f);
			GpuP.Size = Base.Size;
			GpuP.RotationRate = Base.RotationRate;
			GpuP.BaseColor = GpuP.Color;
			GpuP.BaseVelocity = Base.BaseVelocity;
			GpuP.BaseRotationRate = Base.BaseRotationRate;
			GpuP.SubImageIndex = 0.0f;
			GpuP.bAlive = 1;
			PendingGpuSpawns.Add(GpuP);
			++ParticleCounter;
		}
		return;
	}
	if (bIsAfterimage && (!AfterimageSource || !AfterimageSource->GetSkeletalMesh() ||
		AfterimageSource->GetSkinningMatrices().IsEmpty()))
		ToSpawn = 0;
	if (bIsRibbon && ToSpawn > 0 && !bCurrentTrailActive)
	{
		++CurrentTrailIndex;
		bCurrentTrailActive = true;
	}
	const int32 RequiredCapacity = ActiveParticles + ToSpawn;
	if (RequiredCapacity > MaxActiveParticles)
	{
		const int32 GrownCapacity = std::max(RequiredCapacity, MaxActiveParticles * 2);
		Resize(std::min(GrownCapacity, MaxAllowed));
	}
	for (int32 Index = 0; Index < ToSpawn && ActiveParticles < MaxActiveParticles; ++Index)
	{
		const uint16 SlotIndex = ParticleIndices[ActiveParticles];
		uint8* Addr = ParticleData + static_cast<size_t>(SlotIndex) * static_cast<size_t>(ParticleStride);
		std::memset(Addr, 0, static_cast<size_t>(ParticleStride));
		new (Addr) FBaseParticle();
		DECLARE_PARTICLE(Particle, Addr);
		FVector SpawnBladeBase = CurRibbonBase;
		FVector SpawnBladeTip = CurRibbonTip;
		if (bCanInterpolateUnitSpawn && Index < UnitSpawnCount && UnitSpawnCount > 0 && (bUseRibbonParams || !Required->bUseLocalSpace))
		{
			const float Alpha = static_cast<float>(Index + 1) / static_cast<float>(UnitSpawnCount);
			if (bCanInterpolateRibbonEndpoints)
			{
				SpawnBladeBase = PrevBaseForSpawn + (CurRibbonBase - PrevBaseForSpawn) * Alpha;
				SpawnBladeTip = PrevTipForSpawn + (CurRibbonTip - PrevTipForSpawn) * Alpha;
				Particle.Location = (SpawnBladeBase + SpawnBladeTip) * 0.5f;
			}
			else
			{
				Particle.Location = PrevOriginForSpawn + (EffectiveOrigin - PrevOriginForSpawn) * Alpha;
			}
		}
		else
		{
			Particle.Location = bUseRibbonParams
				? EffectiveOrigin
				: (Required->bUseLocalSpace ? FVector::ZeroVector : EffectiveOrigin);
		}
		const FVector SpawnBaseLocation = Particle.Location;
		Random.InstanceParams = InstanceParameters;
		for (const UParticleModule* Module : CurrentLODLevel->SpawnModules)
			if (Module) Module->Spawn(Particle, Random);
		if (!Required->bUseLocalSpace && !bUseRibbonParams)
		{
			// 월드 스페이스 스폰 시 로컬 오프셋과 속도를 컴포넌트 회전으로 변환
			const FVector LocalOffset = Particle.Location - SpawnBaseLocation;
			Particle.Location = SpawnBaseLocation + Context.ComponentToWorld.TransformVector(LocalOffset);
			Particle.Velocity = Context.ComponentToWorld.TransformVector(Particle.Velocity);
			Particle.BaseVelocity = Particle.Velocity;
			if (EmitterType == EParticleEmitterType::Mesh)
				Particle.Rotation += std::atan2(Context.ComponentToWorld.M[0][1], Context.ComponentToWorld.M[0][0]);
		}
		Particle.BaseAlpha = Particle.Color.A;
		Particle.Lifetime = std::max(Particle.Lifetime, 0.001f);
		if (bIsAfterimage)
		{
			if (AfterimageSnapshots.IsValidIndex(SlotIndex))
			{
				AfterimageSnapshots[SlotIndex].reset();
				if (AfterimageSource && AfterimageSource->GetSkeletalMesh())
				{
					const TArray<FMatrix>& Matrices = AfterimageSource->GetSkinningMatrices();
					if (!Matrices.IsEmpty())
					{
						auto Snapshot = MakeShared<FAfterimageSnapshot>();
						Snapshot->Mesh = AfterimageSource->GetSkeletalMesh();
						Snapshot->World = AfterimageSource->GetWorldMatrix();
						Snapshot->SkinningMatrices = Matrices;
						Snapshot->WorldBounds = Snapshot->Mesh->CalcSkinnedBounds(Matrices).GetWorldAABB(Snapshot->World);
						AfterimageSnapshots[SlotIndex] = std::move(Snapshot);
					}
				}
			}
		}

		if (bIsBeam && BeamType && TypeDataOffset > 0)
		{
			FBeamTypeDataPayload& BeamPayload = *reinterpret_cast<FBeamTypeDataPayload*>(Addr + TypeDataOffset);
			new (&BeamPayload) FBeamTypeDataPayload();
			ResolveBeamEndpoints(CurrentLODLevel, BeamType, Origin, Required->bUseLocalSpace,
				EmitterWorldToLocal, InstanceParameters, Random, BeamPayload);
			BeamPayload.TravelRatio = (BeamType->Speed > 0.0f) ? 0.0f : 1.0f;
			BeamPayload.NoiseSeed = AdvanceNoiseSeed(Random.State + ParticleCounter);
			BeamPayload.NoiseLockTimer = 0.0f;
		}
		else if (bIsRibbon && TypeDataOffset > 0)
		{
			FRibbonTypeDataPayload& RibbonPayload = *reinterpret_cast<FRibbonTypeDataPayload*>(Addr + TypeDataOffset);
			new (&RibbonPayload) FRibbonTypeDataPayload();
			RibbonPayload.SpawnOrder = ParticleCounter;
			RibbonPayload.TrailIndex = CurrentTrailIndex;
			if (bUseRibbonParams)
			{
				const FVector BladeVec = SpawnBladeTip - SpawnBladeBase;
				RibbonPayload.WidthOverride = std::max(0.001f, BladeVec.Length());
				RibbonPayload.EdgeHalfOffset = BladeVec * 0.5f;
				RibbonPayload.bUseExplicitEdges = true;
				RibbonPayload.SourceUp = ComputeRibbonBladeUp(BladeVec, RibbonMoveDir, LastRibbonUp);
				if (RibbonMoveDir.Dot(RibbonMoveDir) > 1e-8f)
					LastRibbonUp = RibbonPayload.SourceUp;
			}
			else
			{
				RibbonPayload.SourceUp = Required->bUseLocalSpace ? FVector(0.0f, 0.0f, 1.0f) : EmitterUpAxis;
			}
		}

		++ActiveParticles;
		++ParticleCounter;
	}
}

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::PostUpdateWork;
	bCastShadow = false;
	bReceiveShadow = false;
	bReceiveDecal = false;
}

FBox UParticleSystemComponent::CalcFixedLocalBounds() const
{
	if (!Template)
		return FBox{ FVector(-50.0f, -50.0f, -50.0f), FVector(50.0f, 50.0f, 50.0f) };
	const FVector Min(
		std::min(Template->FixedBoundsMin.X, Template->FixedBoundsMax.X),
		std::min(Template->FixedBoundsMin.Y, Template->FixedBoundsMax.Y),
		std::min(Template->FixedBoundsMin.Z, Template->FixedBoundsMax.Z));
	const FVector Max(
		std::max(Template->FixedBoundsMin.X, Template->FixedBoundsMax.X),
		std::max(Template->FixedBoundsMin.Y, Template->FixedBoundsMax.Y),
		std::max(Template->FixedBoundsMin.Z, Template->FixedBoundsMax.Z));
	return FBox{ Min, Max };
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	if (Template && Template->bUseFixedRelativeBoundingBox)
		return CalcFixedLocalBounds();

	FBox LocalBounds{};
	bool bHasBounds = false;
	for (const FParticleEmitterInstance& Emitter : EmitterInstances)
	{
		UParticleLODLevel* LOD = Emitter.CurrentLODLevel;
		if (!LOD || !LOD->bEnabled || !LOD->EffectiveRequired || !LOD->EffectiveTypeData)
			continue;
		if (LOD->EffectiveTypeData->GetEmitterType() == EParticleEmitterType::GpuSprite)
		{
			FVector OffsetMin, OffsetMax, VelReach;
			float HalfDiagSize = 1.0f;
			float MaxLifetime = 1.0f;
			ComputeGpuEmitterReach(LOD, OffsetMin, OffsetMax, VelReach, HalfDiagSize, MaxLifetime);
			const FVector Reach(
				std::max(50.0f, VelReach.X + HalfDiagSize),
				std::max(50.0f, VelReach.Y + HalfDiagSize),
				std::max(50.0f, VelReach.Z + HalfDiagSize));
			ExpandParticleBox(LocalBounds, bHasBounds, OffsetMin - Reach, OffsetMax + Reach);
		}
		else if (Emitter.ParticleData && Emitter.ParticleIndices)
		{
			const EParticleEmitterType EmitterType = LOD->EffectiveTypeData->GetEmitterType();
			if (EmitterType == EParticleEmitterType::Afterimage)
			{
				const FMatrix InvWorld = GetWorldMatrix().Inverse();
				for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
				{
					const uint16 Slot = Emitter.ParticleIndices[Index];
					if (!Emitter.AfterimageSnapshots.IsValidIndex(Slot) || !Emitter.AfterimageSnapshots[Slot]) continue;
					const FBox LocalBox = Emitter.AfterimageSnapshots[Slot]->WorldBounds.GetWorldAABB(InvWorld);
					ExpandParticleBox(LocalBounds, bHasBounds, LocalBox.Min, LocalBox.Max);
				}
				continue;
			}
			const bool bLocal = LOD->EffectiveRequired->bUseLocalSpace;
			const FMatrix InvWorld = bLocal ? FMatrix::Identity : GetWorldMatrix().Inverse();
			if (EmitterType == EParticleEmitterType::Beam)
			{
				const UParticleModuleTypeDataBeam* BeamType = Cast<UParticleModuleTypeDataBeam>(LOD->EffectiveTypeData);
				const UParticleModuleBeamNoise* NoiseMod = FindEnabledModule<UParticleModuleBeamNoise>(LOD);
				const uint32 BeamOffset = Emitter.GetModuleDataOffset(LOD->EffectiveTypeData);
				if (BeamType && BeamOffset > 0)
				{
					FBeamTrailStrip Strip;
					for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
					{
						const uint8* Addr = Emitter.ParticleData + static_cast<size_t>(Emitter.ParticleIndices[Index]) * static_cast<size_t>(Emitter.ParticleStride);
						const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(Addr);
						const FBeamTypeDataPayload& Payload = *reinterpret_cast<const FBeamTypeDataPayload*>(Addr + BeamOffset);
						BuildBeamStripPoints(Particle, Payload, BeamType, NoiseMod, Emitter.InstanceParameters, FMatrix::Identity, false, Strip);
						for (const FBeamTrailPoint& Pt : Strip.Points)
						{
							const FVector LocalPos = bLocal ? Pt.Position : InvWorld.TransformPosition(Pt.Position);
							const float HalfW = Pt.Width * 0.5f;
							const FVector Extent(HalfW, HalfW, HalfW);
							ExpandParticleBox(LocalBounds, bHasBounds, LocalPos - Extent, LocalPos + Extent);
						}
					}
				}
			}
			else
			{
				const UParticleModuleTypeDataMesh* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(LOD->EffectiveTypeData);
				UStaticMesh* MeshAsset = MeshTypeData ? MeshTypeData->Mesh : nullptr;
				const FVector MeshBaseExtent = MeshAsset
					? (MeshAsset->GetRenderBounds().Max - MeshAsset->GetRenderBounds().Min) * 0.5f
					: FVector::ZeroVector;
				const float MeshBaseRadius = MeshAsset ? MeshBaseExtent.Length() : 0.0f;
				for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
				{
					DECLARE_PARTICLE_CONST(Particle, Emitter.ParticleData + static_cast<size_t>(Emitter.ParticleIndices[Index]) * static_cast<size_t>(Emitter.ParticleStride));
					const FVector LocalPos = bLocal ? Particle.Location : InvWorld.TransformPosition(Particle.Location);
					float Radius = 0.5f * std::sqrt(Particle.Size.X * Particle.Size.X + Particle.Size.Y * Particle.Size.Y);
					if (MeshAsset)
					{
						const float MaxAxisScale = std::max({ std::abs(Particle.Size.X), std::abs(Particle.Size.Y), std::abs(Particle.Size.Z) });
						Radius = std::max(1.0f, MeshBaseRadius * MaxAxisScale);
					}
					const FVector Extent(Radius, Radius, Radius);
					ExpandParticleBox(LocalBounds, bHasBounds, LocalPos - Extent, LocalPos + Extent);
				}
			}
		}
	}
	if (!bHasBounds)
		return FBox{ FVector(-50.0f, -50.0f, -50.0f), FVector(50.0f, 50.0f, 50.0f) };
	return LocalBounds;
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSceneProxy(BuildDynamicData());
}

void UParticleSystemComponent::PushTransformToProxy()
{
	if (FParticleSceneProxy* ParticleProxy = static_cast<FParticleSceneProxy*>(GetSceneProxy()))
		ParticleProxy->ApplyParticleTransform(BuildTransformData());
}

void UParticleSystemComponent::PushRenderStateToProxy()
{
	if (FParticleSceneProxy* ParticleProxy = static_cast<FParticleSceneProxy*>(GetSceneProxy()))
		ParticleProxy->SetDynamicData(BuildDynamicData());
}

FParticleDynamicData UParticleSystemComponent::BuildDynamicData()
{
	FParticleDynamicData Data;
	Data.LocalToWorld = GetWorldMatrix();
	Data.WorldLocation = GetWorldLocation();
	Data.WorldScale = GetWorldScale3D();
	Data.BaseState = BuildBaseRenderState();

	bool bHasBounds = false;
	bool bHasWorldSpaceParticles = false;
	if (Template && Template->bUseFixedRelativeBoundingBox)
	{
		Data.bFixedBounds = true;
		Data.Bounds = CalcFixedLocalBounds().GetWorldAABB(Data.LocalToWorld);
		bHasBounds = true;
	}

	for (FParticleEmitterInstance& Emitter : EmitterInstances)
	{
		UParticleLODLevel* LOD = Emitter.CurrentLODLevel;
		if (!LOD || !LOD->bEnabled || !LOD->EffectiveRequired || !LOD->EffectiveTypeData) continue;
		const EParticleEmitterType EmitterType = LOD->EffectiveTypeData->GetEmitterType();
		if (EmitterType == EParticleEmitterType::Afterimage)
		{
			auto AfterimageData = MakeUnique<FDynamicAfterimageEmitterData>();
			AfterimageData->Material = LOD->EffectiveRequired->Material;
			if (Emitter.ParticleData && Emitter.ParticleIndices)
			{
				for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
				{
					const uint16 Slot = Emitter.ParticleIndices[Index];
					if (!Emitter.AfterimageSnapshots.IsValidIndex(Slot) || !Emitter.AfterimageSnapshots[Slot]) continue;
					const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(
						Emitter.ParticleData + static_cast<size_t>(Slot) * static_cast<size_t>(Emitter.ParticleStride));
					const auto& Snapshot = Emitter.AfterimageSnapshots[Slot];
					AfterimageData->Particles.Add({ Snapshot, (1.0f - Particle.RelativeTime) * (Particle.Color.A / 255.0f) * FadeAlpha });
					if (!Data.bFixedBounds)
						ExpandParticleBox(Data.Bounds, bHasBounds, Snapshot->WorldBounds.Min, Snapshot->WorldBounds.Max);
				}
			}
			if (AfterimageData->HasParticles())
			{
				bHasWorldSpaceParticles = true;
				Data.DynamicEmitters.Add(std::move(AfterimageData));
			}
			continue;
		}
		if (EmitterType == EParticleEmitterType::GpuSprite)
		{
			if (Emitter.MaxActiveParticles <= 0) continue;
			if (!Emitter.GpuResources)
				Emitter.GpuResources = MakeShared<FGpuParticleEmitterResources>();

			FVector OffsetMin, OffsetMax, VelReach;
			float HalfDiagSize = 1.0f;
			float MaxLifetime = 1.0f;
			ComputeGpuEmitterReach(LOD, OffsetMin, OffsetMax, VelReach, HalfDiagSize, MaxLifetime);
			const bool bLocal = LOD->EffectiveRequired->bUseLocalSpace;
			if (!bLocal)
			{
				bHasWorldSpaceParticles = true;
				Emitter.GpuTrailTimer += Emitter.LastGpuDeltaTime;
				const float RotateInterval = std::max(0.1f, MaxLifetime);
				if (Emitter.GpuTrailTimer >= RotateInterval)
				{
					Emitter.GpuTrailBoundsPrev = Emitter.GpuTrailBoundsCurr;
					Emitter.bHasGpuTrailBoundsPrev = Emitter.bHasGpuTrailBoundsCurr;
					Emitter.GpuTrailBoundsCurr = FBox{};
					Emitter.bHasGpuTrailBoundsCurr = false;
					Emitter.GpuTrailTimer = 0.0f;
				}
				const FVector SpawnPad = VelReach + FVector(HalfDiagSize, HalfDiagSize, HalfDiagSize);
				for (const FGpuParticle& Spawn : Emitter.PendingGpuSpawns)
				{
					ExpandParticleBox(Emitter.GpuTrailBoundsCurr, Emitter.bHasGpuTrailBoundsCurr,
						Spawn.Position - SpawnPad, Spawn.Position + SpawnPad); // 이전에 남긴 파티클 궤적(Trail) 범위를 누적 기록
				}
			}

			if (!Data.bFixedBounds)
			{
				const FVector Reach(
					std::max(50.0f, VelReach.X + HalfDiagSize),
					std::max(50.0f, VelReach.Y + HalfDiagSize),
					std::max(50.0f, VelReach.Z + HalfDiagSize));
				const FBox LocalReachBox{ OffsetMin - Reach, OffsetMax + Reach };
				if (bLocal)
				{
					const FBox WorldReachBox = LocalReachBox.GetWorldAABB(Data.LocalToWorld);
					ExpandParticleBox(Data.Bounds, bHasBounds, WorldReachBox.Min, WorldReachBox.Max);
				}
				else
				{
					ExpandParticleBox(Data.Bounds, bHasBounds,
						Data.WorldLocation + LocalReachBox.Min, Data.WorldLocation + LocalReachBox.Max);
					if (Emitter.bHasGpuTrailBoundsCurr)
						ExpandParticleBox(Data.Bounds, bHasBounds, Emitter.GpuTrailBoundsCurr.Min, Emitter.GpuTrailBoundsCurr.Max);
					if (Emitter.bHasGpuTrailBoundsPrev)
						ExpandParticleBox(Data.Bounds, bHasBounds, Emitter.GpuTrailBoundsPrev.Min, Emitter.GpuTrailBoundsPrev.Max);
				}
			}

			auto GpuData = MakeUnique<FDynamicGpuSpriteEmitterData>();
			GpuData->GpuResources = Emitter.GpuResources;
			GpuData->Material = LOD->EffectiveRequired->Material;
			GpuData->MaxParticles = static_cast<uint32>(Emitter.MaxActiveParticles);
			GpuData->ParticleStride = static_cast<uint32>(sizeof(FGpuParticle));
			GpuData->bResetRequested = Emitter.bGpuResetRequested;
			Emitter.bGpuResetRequested = false;

			const uint32 SpawnCount = static_cast<uint32>(Emitter.PendingGpuSpawns.Num());
			if (SpawnCount > 0)
			{
				const size_t Bytes = static_cast<size_t>(SpawnCount) * sizeof(FGpuParticle);
				GpuData->PendingSpawnBytes.SetNum(static_cast<uint32>(Bytes));
				std::memcpy(GpuData->PendingSpawnBytes.GetData(), Emitter.PendingGpuSpawns.GetData(), Bytes);
				Emitter.PendingGpuSpawns.Reset();
			}

			const FParticleEmitterBuildInfo& Build = LOD->EnsureBuilt();
			GpuData->SimParams.DeltaTime = Emitter.LastGpuDeltaTime;
			Emitter.LastGpuDeltaTime = 0.0f;
			GpuData->SimParams.MaxParticles = GpuData->MaxParticles;
			GpuData->SimParams.SpawnCount = SpawnCount;
			GpuData->SimParams.WriteCursor = Emitter.GpuWriteCursor;
			if (GpuData->MaxParticles > 0 && SpawnCount > 0)
				Emitter.GpuWriteCursor = (Emitter.GpuWriteCursor + SpawnCount) % GpuData->MaxParticles;

			GpuData->SimParams.SizeScale = MakeGpuDistributionDesc(Build.SizeScale);
			GpuData->SimParams.ColorOverLife = MakeGpuDistributionDesc(Build.ColorOverLife);
			GpuData->SimParams.SubUV = MakeGpuDistributionDesc(Build.SubUV);
			GpuData->SimParams.VelocityScale = MakeGpuDistributionDesc(Build.VelocityScale);
			GpuData->SimParams.RotationRateScale = MakeGpuDistributionDesc(Build.RotationRateScale);
			GpuData->SimParams.SubUVCols = Build.SubUVCols;
			GpuData->SimParams.SubUVRows = Build.SubUVRows;
			GpuData->SimParams.SubUVPlayRate = Build.SubUVPlayRate;
			GpuData->SimParams.SubUVLinear = Build.bSubUVLinear ? 1u : 0u;

			if (!Build.ColorParameterName.empty())
			{
				if (const FColor* Override = InstanceParameters.ColorParams.Find(Build.ColorParameterName))
				{
					GpuData->SimParams.HasColorOverride = 1;
					GpuData->SimParams.ColorOverride = FVector4(
						Override->R / 255.0f, Override->G / 255.0f, Override->B / 255.0f, Override->A / 255.0f);
				}
			}

			GpuData->bUseLocalSpace = LOD->EffectiveRequired->bUseLocalSpace;
			GpuData->LocalToWorld = Data.LocalToWorld;
			GpuData->WorldLocation = Data.WorldLocation;
			GpuData->WorldScale = Data.WorldScale;
			GpuData->AtlasParams = FVector4(
				Build.bHasSubUV ? Build.SubUVRows : 1.0f,
				Build.bHasSubUV ? Build.SubUVCols : 1.0f,
				1.0f,
				Build.bHasSubUV ? 1.0f : 0.0f);
			Data.DynamicEmitters.Add(std::move(GpuData));
			continue;
		}

		if (EmitterType == EParticleEmitterType::Beam)
		{
			const UParticleModuleTypeDataBeam* BeamType = Cast<UParticleModuleTypeDataBeam>(LOD->EffectiveTypeData);
			const UParticleModuleBeamNoise* NoiseMod = FindEnabledModule<UParticleModuleBeamNoise>(LOD);
			const uint32 BeamOffset = Emitter.GetModuleDataOffset(LOD->EffectiveTypeData);
			if (!BeamType || BeamOffset == 0 || !Emitter.ParticleData || !Emitter.ParticleIndices || Emitter.ActiveParticles <= 0)
				continue;

			const bool bLocal = LOD->EffectiveRequired->bUseLocalSpace;
			if (!bLocal)
				bHasWorldSpaceParticles = true;

			TUniquePtr<FDynamicBeamEmitterData> BeamData = MakeUnique<FDynamicBeamEmitterData>();
			BeamData->Material = LOD->EffectiveRequired->Material;
			BeamData->Sheets = std::max(1, BeamType->Sheets);
			BeamData->Beams.Reserve(Emitter.ActiveParticles);

			for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
			{
				const uint8* Addr = Emitter.ParticleData + static_cast<size_t>(Emitter.ParticleIndices[Index]) * static_cast<size_t>(Emitter.ParticleStride);
				const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(Addr);
				const FBeamTypeDataPayload& Payload = *reinterpret_cast<const FBeamTypeDataPayload*>(Addr + BeamOffset);

				FBeamTrailStrip Strip;
				BuildBeamStripPoints(Particle, Payload, BeamType, NoiseMod, Emitter.InstanceParameters, Data.LocalToWorld, bLocal, Strip);
				if (FadeAlpha < 1.0f)
				{
					for (FBeamTrailPoint& Pt : Strip.Points)
						Pt.Color.W *= FadeAlpha;
				}
				if (!Data.bFixedBounds)
				{
					for (const FBeamTrailPoint& Pt : Strip.Points)
					{
						const float HalfW = Pt.Width * 0.5f;
						const FVector Extent(HalfW, HalfW, HalfW);
						ExpandParticleBox(Data.Bounds, bHasBounds, Pt.Position - Extent, Pt.Position + Extent);
					}
				}
				if (Strip.Points.Num() >= 2)
					BeamData->Beams.Add(std::move(Strip));
			}

			if (BeamData->HasParticles())
				Data.DynamicEmitters.Add(std::move(BeamData));
			continue;
		}

		if (EmitterType == EParticleEmitterType::Ribbon)
		{
			const UParticleModuleTypeDataRibbon* RibbonType = Cast<UParticleModuleTypeDataRibbon>(LOD->EffectiveTypeData);
			if (!RibbonType || !Emitter.ParticleData || !Emitter.ParticleIndices || Emitter.ActiveParticles <= 0)
				continue;

			const bool bLocal = LOD->EffectiveRequired->bUseLocalSpace;
			if (!bLocal)
				bHasWorldSpaceParticles = true;

			TUniquePtr<FDynamicRibbonEmitterData> RibbonData = MakeUnique<FDynamicRibbonEmitterData>();
			RibbonData->Material = LOD->EffectiveRequired->Material;
			RibbonData->SheetsPerTrail = std::max(1, RibbonType->SheetsPerTrail);
			RibbonData->FacingMode = RibbonType->FacingMode;
			BuildRibbonStrips(Emitter, LOD, RibbonType, Data.LocalToWorld, bLocal, Data.WorldLocation, Emitter.bCurrentTrailActive, RibbonData->Trails);
			if (FadeAlpha < 1.0f)
			{
				for (FBeamTrailStrip& Strip : RibbonData->Trails)
				{
					for (FBeamTrailPoint& Pt : Strip.Points)
						Pt.Color.W *= FadeAlpha;
				}
			}

			if (!Data.bFixedBounds)
			{
				for (const FBeamTrailStrip& Strip : RibbonData->Trails)
				{
					for (const FBeamTrailPoint& Pt : Strip.Points)
					{
						const float HalfW = Pt.Width * 0.5f;
						const FVector Extent(HalfW, HalfW, HalfW);
						ExpandParticleBox(Data.Bounds, bHasBounds, Pt.Position - Extent, Pt.Position + Extent);
					}
				}
			}

			if (RibbonData->HasParticles())
				Data.DynamicEmitters.Add(std::move(RibbonData));
			continue;
		}

		if (EmitterType != EParticleEmitterType::Sprite && EmitterType != EParticleEmitterType::Mesh) continue;
		const bool bIsMeshEmitter = (EmitterType == EParticleEmitterType::Mesh);
		const auto* MeshTypeData = bIsMeshEmitter ? Cast<UParticleModuleTypeDataMesh>(LOD->EffectiveTypeData) : nullptr;
		UStaticMesh* MeshAsset = MeshTypeData ? MeshTypeData->Mesh : nullptr;
		if (bIsMeshEmitter && !MeshAsset) continue;

		const bool bLocal = LOD->EffectiveRequired->bUseLocalSpace;
		if (!bLocal)
			bHasWorldSpaceParticles = true;
		const UParticleModuleSubUV* SubUVModule = nullptr;
		if (!bIsMeshEmitter)
		{
			for (const UParticleModule* Module : LOD->EffectiveModules)
			{
				if (Module && Module->bEnabled)
				{
					if (const auto* Candidate = Cast<UParticleModuleSubUV>(Module))
						SubUVModule = Candidate;
				}
			}
		}
		const float AtlasRowSize = SubUVModule ? static_cast<float>(std::max(1, SubUVModule->SubImagesVertical)) : 1.0f;
		const float AtlasColSize = SubUVModule ? static_cast<float>(std::max(1, SubUVModule->SubImagesHorizontal)) : 1.0f;
		const uint32 SubUVOffset = SubUVModule ? Emitter.GetModuleDataOffset(SubUVModule) : 0;
		UMaterialInterface* EmitterMaterial = LOD->EffectiveRequired->Material;
		const float MeshBaseRadius = MeshAsset
			? ((MeshAsset->GetRenderBounds().Max - MeshAsset->GetRenderBounds().Min) * 0.5f).Length()
			: 0.0f;

		if (!Emitter.ParticleData || !Emitter.ParticleIndices || Emitter.ActiveParticles <= 0) continue;

		TUniquePtr<FDynamicSpriteEmitterData> SpriteEmitter;
		TUniquePtr<FDynamicMeshEmitterData> MeshEmitter;
		TArray<FParticleRenderData>* TargetList = nullptr;
		if (bIsMeshEmitter)
		{
			MeshEmitter = MakeUnique<FDynamicMeshEmitterData>();
			MeshEmitter->Mesh = MeshAsset;
			MeshEmitter->MaterialOverride = EmitterMaterial;
			MeshEmitter->bUseLocalSpace = bLocal;
			MeshEmitter->LocalToWorld = Data.LocalToWorld;
			MeshEmitter->Particles.Reserve(Emitter.ActiveParticles);
			TargetList = &MeshEmitter->Particles;
		}
		else
		{
			SpriteEmitter = MakeUnique<FDynamicSpriteEmitterData>();
			SpriteEmitter->Sprites.Reserve(Emitter.ActiveParticles);
			TargetList = &SpriteEmitter->Sprites;
		}

		for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
		{
			DECLARE_PARTICLE_CONST(Particle, Emitter.ParticleData + static_cast<size_t>(Emitter.ParticleIndices[Index]) * static_cast<size_t>(Emitter.ParticleStride));
			FParticleRenderData& ParticleData = TargetList->AddDefaulted_GetRef();
			ParticleData.bIsMesh = bIsMeshEmitter;
			ParticleData.bUseLocalSpace = bLocal;
			ParticleData.Mesh = MeshAsset;
			ParticleData.MeshScale = Particle.Size;
			ParticleData.Location = bIsMeshEmitter
				? Particle.Location
				: (bLocal ? Data.LocalToWorld.TransformPosition(Particle.Location) : Particle.Location);
			ParticleData.Width = std::abs(Particle.Size.X * (bLocal ? Data.WorldScale.Y : 1.0f));
			ParticleData.Height = std::abs(Particle.Size.Y * (bLocal ? Data.WorldScale.Z : 1.0f));
			ParticleData.Rotation = Particle.Rotation;
			ParticleData.Material = EmitterMaterial;
			float CurrentFrame = 0.0f;
			if (SubUVModule && SubUVOffset > 0)
			{
				const FSubUVPayload& SubUVPayload = *reinterpret_cast<const FSubUVPayload*>(
					reinterpret_cast<const uint8*>(&Particle) + SubUVOffset);
				CurrentFrame = SubUVPayload.ImageIndex;
			}
			ParticleData.Constants.CurrentFrame = CurrentFrame;
			ParticleData.Constants.AtlasRowSize = AtlasRowSize;
			ParticleData.Constants.AtlasColSize = AtlasColSize;
			ParticleData.Constants.Alpha = (Particle.Color.A / 255.0f) * FadeAlpha;
			ParticleData.Constants.Tint = FVector4(Particle.Color.R / 255.0f, Particle.Color.G / 255.0f,
				Particle.Color.B / 255.0f, 1.0f);
			ParticleData.Constants.DecodeSRGB = 1.0f;
			if (!Data.bFixedBounds)
			{
				float Radius = 0.5f * std::sqrt(ParticleData.Width * ParticleData.Width + ParticleData.Height * ParticleData.Height);
				FVector BoundsCenter = ParticleData.Location;
				if (bIsMeshEmitter)
				{
					const FVector Scale3D = bLocal ? (Particle.Size * Data.WorldScale) : Particle.Size;
					const float MaxAxisScale = std::max({ std::abs(Scale3D.X), std::abs(Scale3D.Y), std::abs(Scale3D.Z) });
					Radius = std::max(1.0f, MeshBaseRadius * MaxAxisScale);
					if (bLocal)
						BoundsCenter = Data.LocalToWorld.TransformPosition(Particle.Location);
				}
				const FVector Extent(Radius, Radius, Radius);
				ExpandParticleBox(Data.Bounds, bHasBounds, BoundsCenter - Extent, BoundsCenter + Extent);
			}
		}

		if (MeshEmitter)
			Data.DynamicEmitters.Add(std::move(MeshEmitter));
		else if (SpriteEmitter)
			Data.DynamicEmitters.Add(std::move(SpriteEmitter));
	}
	Data.bLocalBoundsOnly = !Data.bFixedBounds && !bHasWorldSpaceParticles;
	if (!bHasBounds)
		Data.Bounds = CalcLocalBounds().GetWorldAABB(Data.LocalToWorld);
	return Data;
}

void UParticleSystemComponent::AddCollisionEvent(const FParticleEventCollideData& Event)
{
	CollisionEvents.Add(Event);
}

void UParticleSystemComponent::BeginPlay()
{
	Super::BeginPlay();
	LoadTemplateIfNeeded();
}

void UParticleSystemComponent::EndPlay()
{
	ResetParticles();
	Super::EndPlay();
}

void UParticleSystemComponent::Activate(bool bReset)
{
	bFadingOut = false;
	FadeAlpha = 1.0f;
	FadeOutDuration = 0.0f;
	FadeOutElapsed = 0.0f;
	for (FParticleEmitterInstance& Instance : EmitterInstances)
	{
		Instance.bSuppressSpawning = false;
		Instance.SuppressElapsed = 0.0f;
	}
	Super::Activate(bReset);
	if (bReset)
	{
		ResetParticles();
	}
	else
	{
		for (FParticleEmitterInstance& Instance : EmitterInstances)
		{
			++Instance.CurrentTrailIndex;
			Instance.bCurrentTrailActive = true;
			Instance.bHasPrevEmitterOrigin = false;
			Instance.bHasPrevRibbonEndpoints = false;
			Instance.SpawnPerUnitBuffer = 0.0f;
		}
	}
}

void UParticleSystemComponent::Deactivate()
{
	bFadingOut = false;
	FadeAlpha = 1.0f;
	Super::Deactivate();
	for (FParticleEmitterInstance& Instance : EmitterInstances)
	{
		Instance.bForceLooping = false;
		Instance.bCurrentTrailActive = false;
		Instance.bHasPrevRibbonEndpoints = false;
	}
	MarkRenderStateDirty();
}

void UParticleSystemComponent::StopEmission(float InFadeOutDuration)
{
	if (!IsActive())
		return;
	bFadingOut = true;
	FadeOutDuration = std::max(0.0f, InFadeOutDuration);
	FadeOutElapsed = 0.0f;
	FadeAlpha = 1.0f;
	for (FParticleEmitterInstance& Instance : EmitterInstances)
	{
		Instance.bForceLooping = false;
		Instance.bSuppressSpawning = true;
		Instance.bCurrentTrailActive = false;
		Instance.bHasPrevRibbonEndpoints = false;
		Instance.SuppressElapsed = 0.0f;
	}
}

void UParticleSystemComponent::SetForceLooping(bool bEnable)
{
	for (FParticleEmitterInstance& Instance : EmitterInstances)
		Instance.bForceLooping = bEnable;
}

bool UParticleSystemComponent::HasCompleted() const
{
	if (bFadingOut && FadeOutDuration > 0.0f && FadeAlpha <= 0.0f)
		return true;
	if (!Template || EmitterInstances.IsEmpty()) return true;
	bool bHasEnabledEmitter = false;
	bool bHasEmitterEnabledAtAnotherLOD = false;
	for (const FParticleEmitterInstance& Instance : EmitterInstances)
	{
		if (!Instance.HasCompleted())
		{
			return false;
		}

		if (!Instance.CurrentLODLevel || !Instance.SpriteTemplate)
		{
			continue;
		}

		if (Instance.CurrentLODLevel->bEnabled)
		{
			bHasEnabledEmitter = true;
		}
		else
		{
			for (const UParticleLODLevel* LOD : Instance.SpriteTemplate->GetLODLevels())
			{
				if (LOD && LOD->bEnabled)
				{
					bHasEmitterEnabledAtAnotherLOD = true;
				}
			}
		}
	}
	return bHasEnabledEmitter || !bHasEmitterEnabledAtAnotherLOD;
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	Template = InTemplate;
	LoadedTemplatePath = TemplatePath;
	EmitterInstances.Reset();
	if (!Template) { MarkRenderStateDirty(); return; }
	uint32 Seed = 1;
	for (UParticleEmitter* Emitter : Template->GetEmitters())
	{
		FParticleEmitterInstance& Instance = EmitterInstances.AddDefaulted_GetRef();
		Instance.InstanceParameters = &InstanceParameters;
		Instance.Initialize(Emitter, Seed++);
	}

	if (ForcedLODIndex >= 0)
	{
		UpdateLOD(0.0f);
	}
	MarkRenderStateDirty();
}

void UParticleSystemComponent::LoadTemplateIfNeeded()
{
	if (TemplatePath.empty())
	{
		if (!LoadedTemplatePath.empty()) SetTemplate(nullptr);
		return;
	}
	if (TemplatePath == LoadedTemplatePath) return;
	LoadedTemplatePath = TemplatePath;
	SetTemplate(UParticleSystem::LoadFromFile(TemplatePath, this));
}

void UParticleSystemComponent::UpdateEmitterStrides()
{
	for (FParticleEmitterInstance& Instance : EmitterInstances)
	{
		if (Instance.SpriteTemplate)
			Instance.SpriteTemplate->CacheEmitterModuleInfo();
		Instance.UpdateStride();
	}
	MarkRenderStateDirty();
}

void UParticleSystemComponent::UpdateLOD(float Distance)
{
	if (!Template || EmitterInstances.IsEmpty())
	{
		return;
	}

	const TArray<float>& Distances = Template->LODDistances;
	int32 TargetLOD = 0;

	if (ForcedLODIndex >= 0)
	{
		TargetLOD = ForcedLODIndex;
	}
	else
	{
		for (int32 Index = 0; Index < static_cast<int32>(Distances.Num()); ++Index)
		{
			if (Distance >= Distances[Index])
			{
				TargetLOD = Index;
			}
		}
	}

	bool bLODChanged = false;
	for (FParticleEmitterInstance& Instance : EmitterInstances)
	{
		const UParticleLODLevel* PreviousLOD = Instance.CurrentLODLevel;
		Instance.SetLODLevel(TargetLOD);
		if (PreviousLOD != Instance.CurrentLODLevel)
		{
			bLODChanged = true;
		}
	}

	if (bLODChanged)
	{
		MarkRenderStateDirty();
	}
}

void UParticleSystemComponent::SetForcedLOD(int32 Index)
{
	if (ForcedLODIndex == Index)
	{
		return;
	}

	ForcedLODIndex = Index;
	if (ForcedLODIndex >= 0)
	{
		UpdateLOD(0.0f);
	}

	MarkRenderStateDirty();
}

void UParticleSystemComponent::ResetParticles()
{
	for (FParticleEmitterInstance& Instance : EmitterInstances) Instance.Reset();
	MarkRenderStateDirty();
}

void UParticleSystemComponent::SyncInstanceParameters()
{
	InstanceParameters.ColorParams.Reset();
	InstanceParameters.VectorParams.Reset();
	InstanceParameters.FloatParams.Reset();
	for (const FParticleSysParam& Param : InstanceParamList)
	{
		if (Param.Name.empty()) continue;
		if (Param.ParamType == EParticleSysParamType::Color)
			InstanceParameters.ColorParams[Param.Name] = Param.Color;
		else if (Param.ParamType == EParticleSysParamType::Vector)
			InstanceParameters.VectorParams[Param.Name] = Param.Vector;
		else if (Param.ParamType == EParticleSysParamType::Scalar)
			InstanceParameters.FloatParams[Param.Name] = Param.Scalar;
	}
}

void UParticleSystemComponent::SetColorParameter(const FString& Name, const FColor& Value)
{
	if (Name.empty()) return;
	for (FParticleSysParam& Param : InstanceParamList)
	{
		if (Param.Name == Name)
		{
			Param.ParamType = EParticleSysParamType::Color;
			Param.Color = Value;
			SyncInstanceParameters();
			return;
		}
	}
	FParticleSysParam NewParam;
	NewParam.Name = Name;
	NewParam.ParamType = EParticleSysParamType::Color;
	NewParam.Color = Value;
	InstanceParamList.Add(NewParam);
	SyncInstanceParameters();
}

void UParticleSystemComponent::SetVectorParameter(const FString& Name, const FVector& Value)
{
	if (Name.empty()) return;
	for (FParticleSysParam& Param : InstanceParamList)
	{
		if (Param.Name == Name)
		{
			Param.ParamType = EParticleSysParamType::Vector;
			Param.Vector = Value;
			SyncInstanceParameters();
			return;
		}
	}
	FParticleSysParam NewParam;
	NewParam.Name = Name;
	NewParam.ParamType = EParticleSysParamType::Vector;
	NewParam.Vector = Value;
	InstanceParamList.Add(NewParam);
	SyncInstanceParameters();
}

void UParticleSystemComponent::ClearVectorParameter(const FString& Name)
{
	if (Name.empty()) return;
	bool bRemoved = false;
	for (int32 Index = InstanceParamList.Num() - 1; Index >= 0; --Index)
	{
		if (InstanceParamList[Index].Name == Name &&
			InstanceParamList[Index].ParamType == EParticleSysParamType::Vector)
		{
			InstanceParamList.RemoveAt(Index);
			bRemoved = true;
		}
	}
	if (bRemoved)
		SyncInstanceParameters();
}

void UParticleSystemComponent::SetFloatParameter(const FString& Name, float Value)
{
	if (Name.empty()) return;
	for (FParticleSysParam& Param : InstanceParamList)
	{
		if (Param.Name == Name)
		{
			Param.ParamType = EParticleSysParamType::Scalar;
			Param.Scalar = Value;
			SyncInstanceParameters();
			return;
		}
	}
	FParticleSysParam NewParam;
	NewParam.Name = Name;
	NewParam.ParamType = EParticleSysParamType::Scalar;
	NewParam.Scalar = Value;
	InstanceParamList.Add(NewParam);
	SyncInstanceParameters();
}

void UParticleSystemComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Archive = Record.GetUnderlyingArchive();
	FString Data;
	if (Archive.IsSaving())
	{
		nlohmann::json Arr = nlohmann::json::array();
		for (const FParticleSysParam& Param : InstanceParamList)
		{
			Arr.push_back({
				{ "Name", Param.Name },
				{ "Type", static_cast<int>(Param.ParamType) },
				{ "Scalar", Param.Scalar },
				{ "Vector", { Param.Vector.X, Param.Vector.Y, Param.Vector.Z } },
				{ "Color", { Param.Color.R, Param.Color.G, Param.Color.B, Param.Color.A } }
			});
		}
		Data = Arr.dump();
	}
	Record.EnterField("InstanceParameters") << Data;
	if (Archive.IsLoading() && !Data.empty())
	{
		const nlohmann::json Arr = nlohmann::json::parse(Data, nullptr, false);
		if (Arr.is_array())
		{
			InstanceParamList.Reset();
			for (const nlohmann::json& Entry : Arr)
			{
				if (!Entry.is_object()) continue;
				FParticleSysParam Param;
				Param.Name = Entry.value("Name", FString());
				Param.ParamType = static_cast<EParticleSysParamType>(
					std::clamp(Entry.value("Type", 2), 0, 2));
				Param.Scalar = Entry.value("Scalar", 0.0f);
				if (Entry.contains("Vector") && Entry["Vector"].is_array() && Entry["Vector"].size() >= 3)
					Param.Vector = FVector(Entry["Vector"][0].get<float>(), Entry["Vector"][1].get<float>(), Entry["Vector"][2].get<float>());
				if (Entry.contains("Color") && Entry["Color"].is_array() && Entry["Color"].size() >= 4)
					Param.Color = FColor(Entry["Color"][0].get<uint8>(), Entry["Color"][1].get<uint8>(), Entry["Color"][2].get<uint8>(), Entry["Color"][3].get<uint8>());
				InstanceParamList.Add(Param);
			}
			SyncInstanceParameters();
		}
	}
}

FColor UParticleSystemComponent::GetColorParameter(const FString& Name, const FColor& Default) const
{
	return InstanceParameters.GetColor(Name, Default);
}

FVector UParticleSystemComponent::GetVectorParameter(const FString& Name, const FVector& Default) const
{
	return InstanceParameters.GetVector(Name, Default);
}

float UParticleSystemComponent::GetFloatParameter(const FString& Name, float Default) const
{
	return InstanceParameters.GetFloat(Name, Default);
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	LoadTemplateIfNeeded();
	if (!Template || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const bool bIsGame = GetWorld() && GetWorld()->IsGameWorld();
	if ((bIsGame || !bPreviewInEditor) && !IsActive()) return;
	const float ScaledDeltaTime = DeltaTime * std::max(0.0f, Template->PlayRate) * std::max(0.0f, CustomTimeDilation);
	if (ScaledDeltaTime <= 0.0f)
	{
		for (FParticleEmitterInstance& Instance : EmitterInstances)
			Instance.LastGpuDeltaTime = 0.0f;
		CollisionEvents.Reset();
		return;
	}
	if (bFadingOut && FadeOutDuration > 0.0f)
	{
		FadeOutElapsed += DeltaTime;
		FadeAlpha = std::clamp(1.0f - FadeOutElapsed / FadeOutDuration, 0.0f, 1.0f);
	}
	const FVector Origin = GetWorldLocation();
	const FVector UpAxis = GetWorldRotationQuat().RotateVector(FVector(0.0f, 0.0f, 1.0f));
	const FMatrix WorldToLocal = GetWorldMatrix().Inverse();
	FParticleTickContext Context;
	Context.World = GetWorld();
	Context.ComponentToWorld = GetWorldMatrix();
	Context.Component = this;
	for (FParticleEmitterInstance& Instance : EmitterInstances)
	{
		Instance.EmitterUpAxis = UpAxis;
		Instance.EmitterWorldToLocal = WorldToLocal;
		USkeletalMeshComponent* Source = nullptr;
		if (Instance.CurrentLODLevel && Instance.CurrentLODLevel->EffectiveTypeData &&
			Instance.CurrentLODLevel->EffectiveTypeData->GetEmitterType() == EParticleEmitterType::Afterimage)
		{
			if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
				Source = Character->GetSkeletalMeshComponent();
		}
		Instance.Tick(ScaledDeltaTime, Origin, Context, Source);
	}

	for (auto& Event : CollisionEvents)
	{
		OnParticleCollision.Broadcast(Event);
	}
	CollisionEvents.Reset();
	MarkRenderStateDirty();
	if (HasCompleted())
	{
		if (bFadingOut)
			ResetParticles();
		if (IsActive()) Deactivate();
		if (bAutoDestroy && bIsGame && GetOwner())
			GetOwner()->Destroy();
	}
}
