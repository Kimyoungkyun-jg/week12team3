#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Engine/ParticleSceneProxy.h"
#include "Render/SceneRenderData.h"
#include "GameFramework/Actor.h"
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

	float SampleMaxCurveAbs(const UCurveFloat* Curve)
	{
		if (!Curve)
			return 1.0f;
		float MaxVal = 0.0f;
		constexpr int32 SampleCount = 8;
		for (int32 i = 0; i <= SampleCount; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(SampleCount);
			MaxVal = std::max(MaxVal, std::abs(Curve->GetFloatValue(T)));
		}
		return MaxVal;
	}

	void ComputeGpuEmitterReach(
		const UParticleLODLevel* LOD,
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

		FVector VelMax = FVector::ZeroVector;
		float MaxSizeX = 1.0f;
		float MaxSizeY = 1.0f;
		float ScaleX = 1.0f;
		float ScaleY = 1.0f;

		for (const UParticleModule* Module : LOD->Modules)
		{
			if (!Module || !Module->bEnabled)
				continue;
			if (const auto* LifeMod = Cast<UParticleModuleLifetime>(Module))
			{
				OutMaxLifetime = std::max(0.05f, LifeMod->Max);
			}
			else if (const auto* LocMod = Cast<UParticleModuleLocation>(Module))
			{
				const FVector ModMin(
					std::min(LocMod->Min.X, LocMod->Max.X),
					std::min(LocMod->Min.Y, LocMod->Max.Y),
					std::min(LocMod->Min.Z, LocMod->Max.Z));
				const FVector ModMax(
					std::max(LocMod->Min.X, LocMod->Max.X),
					std::max(LocMod->Min.Y, LocMod->Max.Y),
					std::max(LocMod->Min.Z, LocMod->Max.Z));
				OutOffsetMin = FVector(
					std::min(OutOffsetMin.X, ModMin.X),
					std::min(OutOffsetMin.Y, ModMin.Y),
					std::min(OutOffsetMin.Z, ModMin.Z));
				OutOffsetMax = FVector(
					std::max(OutOffsetMax.X, ModMax.X),
					std::max(OutOffsetMax.Y, ModMax.Y),
					std::max(OutOffsetMax.Z, ModMax.Z));
			}
			else if (const auto* VelMod = Cast<UParticleModuleVelocity>(Module))
			{
				VelMax.X = std::max({ VelMax.X, std::abs(VelMod->Min.X), std::abs(VelMod->Max.X) });
				VelMax.Y = std::max({ VelMax.Y, std::abs(VelMod->Min.Y), std::abs(VelMod->Max.Y) });
				VelMax.Z = std::max({ VelMax.Z, std::abs(VelMod->Min.Z), std::abs(VelMod->Max.Z) });
			}
			else if (const auto* SizeMod = Cast<UParticleModuleSize>(Module))
			{
				MaxSizeX = std::max({ MaxSizeX, std::abs(SizeMod->Start.X), std::abs(SizeMod->End.X) });
				MaxSizeY = std::max({ MaxSizeY, std::abs(SizeMod->Start.Y), std::abs(SizeMod->End.Y) });
			}
			else if (const auto* SizeByLife = Cast<UParticleModuleSizeByLife>(Module))
			{
				ScaleX = std::max(ScaleX, SampleMaxCurveAbs(SizeByLife->ScaleX));
				ScaleY = std::max(ScaleY, SampleMaxCurveAbs(SizeByLife->ScaleY));
			}
		}

		OutVelReach = VelMax * OutMaxLifetime;
		const float FinalSizeX = MaxSizeX * ScaleX;
		const float FinalSizeY = MaxSizeY * ScaleY;
		OutHalfDiagSize = 0.5f * std::sqrt(FinalSizeX * FinalSizeX + FinalSizeY * FinalSizeY);
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
	, GpuWriteCursor(0)
	, LastGpuDeltaTime(Other.LastGpuDeltaTime)
	, bGpuResetRequested(true)
	, bHasGpuTrailBoundsCurr(false)
	, bHasGpuTrailBoundsPrev(false)
	, GpuTrailTimer(0.0f)
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
	, GpuWriteCursor(Other.GpuWriteCursor)
	, LastGpuDeltaTime(Other.LastGpuDeltaTime)
	, bGpuResetRequested(Other.bGpuResetRequested)
	, GpuTrailBoundsCurr(Other.GpuTrailBoundsCurr)
	, GpuTrailBoundsPrev(Other.GpuTrailBoundsPrev)
	, bHasGpuTrailBoundsCurr(Other.bHasGpuTrailBoundsCurr)
	, bHasGpuTrailBoundsPrev(Other.bHasGpuTrailBoundsPrev)
	, GpuTrailTimer(Other.GpuTrailTimer)
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
	GpuWriteCursor = Other.GpuWriteCursor;
	LastGpuDeltaTime = Other.LastGpuDeltaTime;
	bGpuResetRequested = Other.bGpuResetRequested;
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
		if (CurrentLODLevel->TypeDataModule && CurrentLODLevel->TypeDataModule->bEnabled)
		{
			const uint32 ReqBytes = CurrentLODLevel->TypeDataModule->RequiredBytes();
			if (ReqBytes > 0)
			{
				ModuleOffsetMap.Add(CurrentLODLevel->TypeDataModule, static_cast<uint32>(NewSize));
				NewSize += static_cast<int32>(ReqBytes);
			}
		}
		for (const UParticleModule* Module : CurrentLODLevel->Modules)
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
	if (ClampedMax == MaxActiveParticles && (ClampedMax == 0 || (ParticleData && ParticleIndices))) return;

	if (ClampedMax == 0)
	{
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
	if (ParticleData && ParticleIndices && PreservedCount > 0)
	{
		// 축소 시에도 활성 파티클을 앞쪽 슬롯에 연속 배치해 인덱스 일관성을 유지한다
		for (int32 Index = 0; Index < PreservedCount; ++Index)
		{
			const uint8* Src = ParticleData + static_cast<size_t>(ParticleIndices[Index]) * static_cast<size_t>(ParticleStride);
			uint8* Dst = NewData + static_cast<size_t>(Index) * static_cast<size_t>(ParticleStride);
			std::memcpy(Dst, Src, static_cast<size_t>(ParticleStride));
		}
	}

	delete[] ParticleData;
	delete[] ParticleIndices;
	ParticleData = NewData;
	ParticleIndices = NewIndices;
	MaxActiveParticles = ClampedMax;
	ActiveParticles = PreservedCount;
}

void FParticleEmitterInstance::Initialize(UParticleEmitter* Emitter, uint32 Seed)
{
	GpuResources.reset();
	PendingGpuSpawns.Reset();
	GpuWriteCursor = 0;
	LastGpuDeltaTime = 0.0f;
	bGpuResetRequested = true;
	GpuTrailBoundsCurr = FBox{};
	GpuTrailBoundsPrev = FBox{};
	bHasGpuTrailBoundsCurr = false;
	bHasGpuTrailBoundsPrev = false;
	GpuTrailTimer = 0.0f;
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
	const EParticleEmitterType Type = CurrentLODLevel->GetOrCreateTypeData()->GetEmitterType();
	if (Type != EParticleEmitterType::Sprite && Type != EParticleEmitterType::GpuSprite && Type != EParticleEmitterType::Mesh) { Resize(0); return; }
	UParticleModuleRequired* Required = CurrentLODLevel->GetOrCreateRequired();
	if (Type == EParticleEmitterType::GpuSprite)
	{
		Resize(0);
		MaxActiveParticles = std::clamp(Required->MaxParticles, 0, 65535);
		return;
	}
	UpdateStride();
	Resize(Required->MaxParticles);
}

void FParticleEmitterInstance::SetLODLevel(int32 NewLODIndex)
{
	if (!SpriteTemplate) return;
	const TArray<UParticleLODLevel*>& LODs = SpriteTemplate->GetLODLevels();
	if (LODs.IsEmpty()) return;
	const int32 ClampedIndex = std::clamp(NewLODIndex, 0, static_cast<int32>(LODs.Num()) - 1);
	if (CurrentLODLevelIndex == ClampedIndex && CurrentLODLevel == LODs[ClampedIndex]) return;
	CurrentLODLevelIndex = ClampedIndex;
	CurrentLODLevel = LODs[ClampedIndex];
	if (!CurrentLODLevel) return;
	UParticleModuleRequired* Required = CurrentLODLevel->GetOrCreateRequired();
	if (IsGpuEmitter())
	{
		Resize(0);
		MaxActiveParticles = std::clamp(Required->MaxParticles, 0, 65535);
		return;
	}
	UpdateStride();
	Resize(Required->MaxParticles);
}

void FParticleEmitterInstance::Reset()
{
	ActiveParticles = 0;
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
	if (ParticleIndices)
	{
		for (int32 Index = 0; Index < MaxActiveParticles; ++Index)
			ParticleIndices[Index] = static_cast<uint16>(Index);
	}
}

bool FParticleEmitterInstance::IsGpuEmitter() const
{
	return CurrentLODLevel && CurrentLODLevel->bEnabled &&
		CurrentLODLevel->TypeDataModule &&
		CurrentLODLevel->TypeDataModule->GetEmitterType() == EParticleEmitterType::GpuSprite;
}

bool FParticleEmitterInstance::HasCompleted() const
{
	if (!CurrentLODLevel || !CurrentLODLevel->bEnabled) return true;
	const UParticleModuleRequired* Required = CurrentLODLevel->RequiredModule;
	if (!Required || Required->bLooping || Required->EmitterDuration <= 0.0f) return false;
	if (IsGpuEmitter())
		return EmitterTime >= static_cast<double>(Required->EmitterDuration) && PendingGpuSpawns.IsEmpty();
	return EmitterTime >= static_cast<double>(Required->EmitterDuration) && ActiveParticles == 0;
}

void FParticleEmitterInstance::Tick(float DeltaTime, const FVector& Origin)
{
	if (!CurrentLODLevel || !CurrentLODLevel->bEnabled || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	if (SpriteTemplate)
	{
		DeltaTime *= std::max(0.0f, SpriteTemplate->PlayRate);
		if (DeltaTime <= 0.0f) return;
	}
	const EParticleEmitterType EmitterType = CurrentLODLevel->GetOrCreateTypeData()->GetEmitterType();
	if (EmitterType != EParticleEmitterType::Sprite && EmitterType != EParticleEmitterType::GpuSprite && EmitterType != EParticleEmitterType::Mesh) return;
	UParticleModuleRequired* Required = CurrentLODLevel->GetOrCreateRequired();
	const bool bIsGpu = (EmitterType == EParticleEmitterType::GpuSprite);

	if (bIsGpu)
	{
		MaxActiveParticles = std::clamp(Required->MaxParticles, 0, 65535);
		LastGpuDeltaTime = DeltaTime;
		if (MaxActiveParticles <= 0) return;
	}
	else
	{
		Resize(Required->MaxParticles);
		if (!ParticleData || !ParticleIndices || MaxActiveParticles <= 0) return;

		// 살아 있는 파티클은 방출이 끝난 뒤에도 수명이 다할 때까지 갱신한다.
		for (int32 Index = 0; Index < ActiveParticles;)
		{
			DECLARE_PARTICLE(Particle, ParticleData + static_cast<size_t>(ParticleIndices[Index]) * static_cast<size_t>(ParticleStride));
			Particle.RelativeTime += DeltaTime / Particle.Lifetime;
			if (Particle.RelativeTime >= 1.0f)
			{
				std::swap(ParticleIndices[Index], ParticleIndices[ActiveParticles - 1]);
				--ActiveParticles;
				continue;
			}
			Particle.Location += Particle.Velocity * DeltaTime;
			Particle.Rotation += Particle.RotationRate * DeltaTime;
			for (const UParticleModule* Module : CurrentLODLevel->Modules)
			{
				if (!Module || !Module->bEnabled) continue;
				Module->Update(Particle, DeltaTime, GetModuleDataOffset(Module));
				if (!InstanceParameters) continue;
				if (const auto* ColorMod = Cast<UParticleModuleColor>(Module))
				{
					if (!ColorMod->ParameterName.empty())
					{
						if (const FColor* Override = InstanceParameters->ColorParams.Find(ColorMod->ParameterName))
							Particle.Color = *Override;
					}
				}
				else if (const auto* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
				{
					if (!ColorOverLife->ParameterName.empty())
					{
						if (const FColor* Override = InstanceParameters->ColorParams.Find(ColorOverLife->ParameterName))
							Particle.Color = FColor(Override->R, Override->G, Override->B, Particle.Color.A);
					}
				}
			}
			++Index;
		}
	}

	const double StartTime = EmitterTime;
	const double EndTime = StartTime + DeltaTime;
	EmitterTime = EndTime;
	const double Duration = Required->EmitterDuration;
	const double SpawnTime = Duration > 0.0 && !Required->bLooping
		? std::clamp(Duration - StartTime, 0.0, static_cast<double>(DeltaTime))
		: static_cast<double>(DeltaTime);
	const UParticleModuleSpawn* SpawnModule = CurrentLODLevel->GetOrCreateSpawn();
	const int32 Available = bIsGpu ? MaxActiveParticles : std::max(0, MaxActiveParticles - ActiveParticles);
	int32 ToSpawn = 0;
	if (SpawnModule->bEnabled)
	{
		for (const FParticleBurst& Burst : SpawnModule->Bursts)
		{
			if (Burst.Count <= 0 || !std::isfinite(Burst.Time) || Burst.Time < 0.0f ||
				(Duration > 0.0 && Burst.Time >= Duration)) continue;
			double Occurrences = 0.0;
			if (Duration > 0.0 && Required->bLooping)
			{
				const double FirstCycle = std::max(0.0, std::ceil((StartTime - Burst.Time) / Duration));
				const double LastCycle = std::ceil((EndTime - Burst.Time) / Duration);
				Occurrences = std::max(0.0, LastCycle - FirstCycle);
			}
			else if (StartTime <= Burst.Time && Burst.Time < EndTime)
				Occurrences = 1.0;
			ToSpawn += static_cast<int32>(std::min<double>(Available - ToSpawn, Occurrences * Burst.Count));
		}

		const float Rate = std::max(0.0f, SpawnModule->Rate);
		if (std::isfinite(Rate) && Rate > 0.0f && SpawnTime > 0.0)
		{
			const double Count = static_cast<double>(SpawnFraction) + static_cast<double>(Rate) * SpawnTime;
			ToSpawn += static_cast<int32>(std::min<double>(std::floor(Count), Available - ToSpawn));
			SpawnFraction = static_cast<float>(Count - std::floor(Count));
		}
	}
	if (bIsGpu)
	{
		for (int32 Index = 0; Index < ToSpawn; ++Index)
		{
			FBaseParticle Base;
			Base.Location = Required->bUseLocalSpace ? FVector::ZeroVector : Origin;
			Random.InstanceParams = InstanceParameters;
			for (const UParticleModule* Module : CurrentLODLevel->Modules)
				if (Module && Module->bEnabled) Module->Spawn(Base, Random);
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
			GpuP.SubImageIndex = 0.0f;
			GpuP.bAlive = 1;
			PendingGpuSpawns.Add(GpuP);
			++ParticleCounter;
		}
		return;
	}
	for (int32 Index = 0; Index < ToSpawn && ActiveParticles < MaxActiveParticles; ++Index)
	{
		const uint16 SlotIndex = ParticleIndices[ActiveParticles];
		uint8* Addr = ParticleData + static_cast<size_t>(SlotIndex) * static_cast<size_t>(ParticleStride);
		std::memset(Addr, 0, static_cast<size_t>(ParticleStride));
		new (Addr) FBaseParticle();
		DECLARE_PARTICLE(Particle, Addr);
		Particle.Location = Required->bUseLocalSpace ? FVector::ZeroVector : Origin;
		Random.InstanceParams = InstanceParameters;
		for (const UParticleModule* Module : CurrentLODLevel->Modules)
			if (Module && Module->bEnabled) Module->Spawn(Particle, Random);
		Particle.Lifetime = std::max(Particle.Lifetime, 0.001f);
		++ActiveParticles;
		++ParticleCounter;
	}
}

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
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
		const UParticleLODLevel* LOD = Emitter.CurrentLODLevel;
		if (!LOD || !LOD->bEnabled || !LOD->RequiredModule || !LOD->TypeDataModule)
			continue;
		if (LOD->TypeDataModule->GetEmitterType() == EParticleEmitterType::GpuSprite)
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
			const bool bLocal = LOD->RequiredModule->bUseLocalSpace;
			const FMatrix InvWorld = bLocal ? FMatrix::Identity : GetWorldMatrix().Inverse();
			const auto* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(LOD->TypeDataModule);
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
		const UParticleLODLevel* LOD = Emitter.CurrentLODLevel;
		if (!LOD || !LOD->bEnabled || !LOD->RequiredModule || !LOD->TypeDataModule) continue;
		const EParticleEmitterType EmitterType = LOD->TypeDataModule->GetEmitterType();
		if (EmitterType == EParticleEmitterType::GpuSprite)
		{
			if (Emitter.MaxActiveParticles <= 0) continue;
			if (!Emitter.GpuResources)
				Emitter.GpuResources = MakeShared<FGpuParticleEmitterResources>();

			FVector OffsetMin, OffsetMax, VelReach;
			float HalfDiagSize = 1.0f;
			float MaxLifetime = 1.0f;
			ComputeGpuEmitterReach(LOD, OffsetMin, OffsetMax, VelReach, HalfDiagSize, MaxLifetime);
			const bool bLocal = LOD->RequiredModule->bUseLocalSpace;
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
						Spawn.Position - SpawnPad, Spawn.Position + SpawnPad);
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

			FParticleDynamicGpuEmitterData& GpuData = Data.GpuEmitters.AddDefaulted_GetRef();
			GpuData.GpuResources = Emitter.GpuResources;
			GpuData.Material = LOD->RequiredModule->Material;
			GpuData.MaxParticles = static_cast<uint32>(Emitter.MaxActiveParticles);
			GpuData.ParticleStride = static_cast<uint32>(sizeof(FGpuParticle));
			GpuData.bResetRequested = Emitter.bGpuResetRequested;
			Emitter.bGpuResetRequested = false;

			const uint32 SpawnCount = static_cast<uint32>(Emitter.PendingGpuSpawns.Num());
			if (SpawnCount > 0)
			{
				const size_t Bytes = static_cast<size_t>(SpawnCount) * sizeof(FGpuParticle);
				GpuData.PendingSpawnBytes.SetNum(static_cast<uint32>(Bytes));
				std::memcpy(GpuData.PendingSpawnBytes.GetData(), Emitter.PendingGpuSpawns.GetData(), Bytes);
				Emitter.PendingGpuSpawns.Reset();
			}

			GpuData.SimParams.DeltaTime = Emitter.LastGpuDeltaTime;
			Emitter.LastGpuDeltaTime = 0.0f;
			GpuData.SimParams.MaxParticles = GpuData.MaxParticles;
			GpuData.SimParams.SpawnCount = SpawnCount;
			GpuData.SimParams.WriteCursor = Emitter.GpuWriteCursor;
			if (GpuData.MaxParticles > 0 && SpawnCount > 0)
				Emitter.GpuWriteCursor = (Emitter.GpuWriteCursor + SpawnCount) % GpuData.MaxParticles;

			GpuData.CurveLUT.SetNum(384);
			for (int32 i = 0; i < 384; ++i)
				GpuData.CurveLUT[i] = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

			const UParticleModuleSubUV* SubUVMod = nullptr;
			for (const UParticleModule* Module : LOD->Modules)
			{
				if (!Module || !Module->bEnabled) continue;
				if (const auto* ColorMod = Cast<UParticleModuleColor>(Module))
				{
					GpuData.SimParams.UseColorModule = 1;
					GpuData.SimParams.ColorStart = FVector4(ColorMod->Start.R / 255.0f, ColorMod->Start.G / 255.0f, ColorMod->Start.B / 255.0f, ColorMod->Start.A / 255.0f);
					GpuData.SimParams.ColorEnd = FVector4(ColorMod->End.R / 255.0f, ColorMod->End.G / 255.0f, ColorMod->End.B / 255.0f, ColorMod->End.A / 255.0f);
					if (!ColorMod->ParameterName.empty())
					{
						if (const FColor* Override = InstanceParameters.ColorParams.Find(ColorMod->ParameterName))
						{
							GpuData.SimParams.HasColorOverride = 1;
							GpuData.SimParams.ColorOverride = FVector4(Override->R / 255.0f, Override->G / 255.0f, Override->B / 255.0f, Override->A / 255.0f);
						}
					}
				}
				else if (const auto* SizeMod = Cast<UParticleModuleSize>(Module))
				{
					GpuData.SimParams.UseSizeModule = 1;
					GpuData.SimParams.SizeStart = SizeMod->Start;
					GpuData.SimParams.SizeEnd = SizeMod->End;
				}
				else if (const auto* SizeByLife = Cast<UParticleModuleSizeByLife>(Module))
				{
					GpuData.SimParams.UseSizeByLife = 1;
					for (int32 i = 0; i < 128; ++i)
					{
						const float T = static_cast<float>(i) / 127.0f;
						GpuData.CurveLUT[i] = FVector4(
							SizeByLife->ScaleX ? SizeByLife->ScaleX->GetFloatValue(T) : 1.0f,
							SizeByLife->ScaleY ? SizeByLife->ScaleY->GetFloatValue(T) : 1.0f,
							SizeByLife->ScaleZ ? SizeByLife->ScaleZ->GetFloatValue(T) : 1.0f,
							1.0f);
					}
				}
				else if (const auto* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
				{
					GpuData.SimParams.UseColorOverLife = 1;
					for (int32 i = 0; i < 128; ++i)
					{
						const float T = static_cast<float>(i) / 127.0f;
						GpuData.CurveLUT[128 + i] = FVector4(
							ColorOverLife->ColorR ? ColorOverLife->ColorR->GetFloatValue(T) : 1.0f,
							ColorOverLife->ColorG ? ColorOverLife->ColorG->GetFloatValue(T) : 1.0f,
							ColorOverLife->ColorB ? ColorOverLife->ColorB->GetFloatValue(T) : 1.0f,
							ColorOverLife->Alpha ? ColorOverLife->Alpha->GetFloatValue(T) : 1.0f);
					}
					if (!ColorOverLife->ParameterName.empty())
					{
						if (const FColor* Override = InstanceParameters.ColorParams.Find(ColorOverLife->ParameterName))
						{
							GpuData.SimParams.HasColorOverride = 1;
							GpuData.SimParams.ColorOverride = FVector4(Override->R / 255.0f, Override->G / 255.0f, Override->B / 255.0f, Override->A / 255.0f);
						}
					}
				}
				else if (const auto* CandidateSubUV = Cast<UParticleModuleSubUV>(Module))
				{
					SubUVMod = CandidateSubUV;
					GpuData.SimParams.UseSubUV = 1;
					GpuData.SimParams.SubUVCols = static_cast<float>(std::max(1, CandidateSubUV->SubImagesHorizontal));
					GpuData.SimParams.SubUVRows = static_cast<float>(std::max(1, CandidateSubUV->SubImagesVertical));
					GpuData.SimParams.SubUVLinear = CandidateSubUV->bUseLinearOverLife ? 1u : 0u;
					GpuData.SimParams.SubUVPlayRate = CandidateSubUV->PlayRate;
					if (!CandidateSubUV->bUseLinearOverLife && CandidateSubUV->SubImageIndex)
					{
						for (int32 i = 0; i < 128; ++i)
						{
							const float T = static_cast<float>(i) / 127.0f;
							const float V = CandidateSubUV->SubImageIndex->GetFloatValue(T);
							GpuData.CurveLUT[256 + i] = FVector4(V, 0.0f, 0.0f, 0.0f);
						}
					}
				}
			}

			GpuData.bUseLocalSpace = LOD->RequiredModule->bUseLocalSpace;
			GpuData.AtlasParams = FVector4(
				SubUVMod ? static_cast<float>(std::max(1, SubUVMod->SubImagesVertical)) : 1.0f,
				SubUVMod ? static_cast<float>(std::max(1, SubUVMod->SubImagesHorizontal)) : 1.0f,
				0.0f,
				SubUVMod ? 1.0f : 0.0f);
			continue;
		}

		if (EmitterType != EParticleEmitterType::Sprite && EmitterType != EParticleEmitterType::Mesh) continue;
		const bool bIsMeshEmitter = (EmitterType == EParticleEmitterType::Mesh);
		const auto* MeshTypeData = bIsMeshEmitter ? Cast<UParticleModuleTypeDataMesh>(LOD->TypeDataModule) : nullptr;
		UStaticMesh* MeshAsset = MeshTypeData ? MeshTypeData->Mesh : nullptr;
		if (bIsMeshEmitter && !MeshAsset) continue;

		const bool bLocal = LOD->RequiredModule->bUseLocalSpace;
		if (!bLocal)
			bHasWorldSpaceParticles = true;
		const UParticleModuleSubUV* SubUVModule = nullptr;
		if (!bIsMeshEmitter)
		{
			for (const UParticleModule* Module : LOD->Modules)
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
		UMaterial* EmitterMaterial = LOD->RequiredModule->Material
			? LOD->RequiredModule->Material
			: (MeshAsset ? MeshAsset->GetMaterial(0) : nullptr);
		const float MeshBaseRadius = MeshAsset
			? ((MeshAsset->GetRenderBounds().Max - MeshAsset->GetRenderBounds().Min) * 0.5f).Length()
			: 0.0f;

		if (!Emitter.ParticleData || !Emitter.ParticleIndices) continue;
		for (int32 Index = 0; Index < Emitter.ActiveParticles; ++Index)
		{
			DECLARE_PARTICLE_CONST(Particle, Emitter.ParticleData + static_cast<size_t>(Emitter.ParticleIndices[Index]) * static_cast<size_t>(Emitter.ParticleStride));
			FParticleSpriteRenderData& Sprite = Data.Sprites.AddDefaulted_GetRef();
			Sprite.bIsMesh = bIsMeshEmitter;
			Sprite.bUseLocalSpace = bLocal;
			Sprite.Mesh = MeshAsset;
			Sprite.MeshScale = Particle.Size;
			Sprite.Location = bIsMeshEmitter
				? Particle.Location
				: (bLocal ? Data.LocalToWorld.TransformPosition(Particle.Location) : Particle.Location);
			Sprite.Width = std::abs(Particle.Size.X * (bLocal ? Data.WorldScale.Y : 1.0f));
			Sprite.Height = std::abs(Particle.Size.Y * (bLocal ? Data.WorldScale.Z : 1.0f));
			Sprite.Rotation = Particle.Rotation;
			Sprite.Material = EmitterMaterial;
			float CurrentFrame = 0.0f;
			if (SubUVModule && SubUVOffset > 0)
			{
				const FSubUVPayload& SubUVPayload = *reinterpret_cast<const FSubUVPayload*>(
					reinterpret_cast<const uint8*>(&Particle) + SubUVOffset);
				CurrentFrame = SubUVPayload.ImageIndex;
			}
			Sprite.Constants.CurrentFrame = CurrentFrame;
			Sprite.Constants.AtlasRowSize = AtlasRowSize;
			Sprite.Constants.AtlasColSize = AtlasColSize;
			Sprite.Constants.Alpha = Particle.Color.A / 255.0f;
			Sprite.Constants.Tint = FVector4(Particle.Color.R / 255.0f, Particle.Color.G / 255.0f,
				Particle.Color.B / 255.0f, 1.0f);
			Sprite.Constants.DecodeSRGB = 0.0f;
			if (!Data.bFixedBounds)
			{
				float Radius = 0.5f * std::sqrt(Sprite.Width * Sprite.Width + Sprite.Height * Sprite.Height);
				FVector BoundsCenter = Sprite.Location;
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
	}
	Data.bLocalBoundsOnly = !Data.bFixedBounds && !bHasWorldSpaceParticles;
	if (!bHasBounds)
		Data.Bounds = CalcLocalBounds().GetWorldAABB(Data.LocalToWorld);
	return Data;
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
	Super::Activate(bReset);
	if (bReset) ResetParticles();
}

void UParticleSystemComponent::Deactivate()
{
	Super::Deactivate();
	MarkRenderStateDirty();
}

bool UParticleSystemComponent::HasCompleted() const
{
	if (!Template || EmitterInstances.IsEmpty()) return true;
	for (const FParticleEmitterInstance& Instance : EmitterInstances)
	{
		if (!Instance.HasCompleted()) return false;
	}
	return true;
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
		Instance.UpdateStride();
}

void UParticleSystemComponent::UpdateLOD(float Distance)
{
	if (!Template || EmitterInstances.IsEmpty()) return;
	const TArray<float>& Distances = Template->LODDistances;
	int32 TargetLOD = 0;
	for (int32 Index = 0; Index < static_cast<int32>(Distances.Num()); ++Index)
	{
		if (Distance >= Distances[Index]) TargetLOD = Index;
	}
	for (FParticleEmitterInstance& Instance : EmitterInstances)
		Instance.SetLODLevel(TargetLOD);
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
	if (bIsGame && !IsActive()) return;
	const float ScaledDeltaTime = DeltaTime * std::max(0.0f, Template->PlayRate) * std::max(0.0f, CustomTimeDilation);
	if (ScaledDeltaTime <= 0.0f)
	{
		for (FParticleEmitterInstance& Instance : EmitterInstances)
			Instance.LastGpuDeltaTime = 0.0f;
		return;
	}
	const FVector Origin = GetWorldLocation();
	for (FParticleEmitterInstance& Instance : EmitterInstances) Instance.Tick(ScaledDeltaTime, Origin);
	MarkRenderStateDirty();
	if (HasCompleted())
	{
		if (IsActive()) Deactivate();
		if (bAutoDestroy && bIsGame && GetOwner())
			GetOwner()->Destroy();
	}
}
