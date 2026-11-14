#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Engine/ParticleSceneProxy.h"
#include "GameFramework/Actor.h"
#include "UObject/Casts.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

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
	, GpuEmitterId(Other.GpuEmitterId)
	, PendingGpuSpawns(Other.PendingGpuSpawns)
	, GpuWriteCursor(Other.GpuWriteCursor)
	, LastGpuDeltaTime(Other.LastGpuDeltaTime)
	, bGpuResetRequested(Other.bGpuResetRequested)
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
	, GpuEmitterId(Other.GpuEmitterId)
	, PendingGpuSpawns(std::move(Other.PendingGpuSpawns))
	, GpuWriteCursor(Other.GpuWriteCursor)
	, LastGpuDeltaTime(Other.LastGpuDeltaTime)
	, bGpuResetRequested(Other.bGpuResetRequested)
{
	Other.ParticleData = nullptr;
	Other.ParticleIndices = nullptr;
	Other.ActiveParticles = 0;
	Other.MaxActiveParticles = 0;
	Other.GpuEmitterId = 0;
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
	GpuEmitterId = Other.GpuEmitterId;
	PendingGpuSpawns = std::move(Other.PendingGpuSpawns);
	GpuWriteCursor = Other.GpuWriteCursor;
	LastGpuDeltaTime = Other.LastGpuDeltaTime;
	bGpuResetRequested = Other.bGpuResetRequested;
	Other.ParticleData = nullptr;
	Other.ParticleIndices = nullptr;
	Other.ActiveParticles = 0;
	Other.MaxActiveParticles = 0;
	Other.GpuEmitterId = 0;
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
	const int32 NewStride = NewSize;
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
	static uint64 NextGpuEmitterId = 1;
	GpuEmitterId = NextGpuEmitterId++;
	PendingGpuSpawns.Reset();
	GpuWriteCursor = 0;
	LastGpuDeltaTime = 0.0f;
	bGpuResetRequested = true;
	ActiveParticles = 0;
	ParticleCounter = 0;
	EmitterTime = 0.0;
	SpawnFraction = 0.0f;
	SpriteTemplate = Emitter;
	CurrentLODLevel = nullptr;
	CurrentLODLevelIndex = 0;
	ParticleSize = sizeof(FBaseParticle);
	ParticleStride = ParticleSize;
	Random.State = Seed ? Seed : 1;
	Random.InstanceParams = InstanceParameters;
	if (!Emitter) { Resize(0); return; }
	const TArray<UParticleLODLevel*>& LODs = Emitter->GetLODLevels();
	if (LODs.IsEmpty()) { Resize(0); return; }
	CurrentLODLevel = LODs[0];
	if (!CurrentLODLevel) { Resize(0); return; }
	const EParticleEmitterType Type = CurrentLODLevel->GetOrCreateTypeData()->GetEmitterType();
	if (Type != EParticleEmitterType::Sprite && Type != EParticleEmitterType::GpuSprite) { Resize(0); return; }
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
	if (EmitterType != EParticleEmitterType::Sprite && EmitterType != EParticleEmitterType::GpuSprite) return;
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
}

void UParticleSystemComponent::CreateRenderState()
{
	Super::CreateRenderState();
	if (GetWorld()) GetWorld()->GetScene().AddParticle(this);
}

void UParticleSystemComponent::DestroyRenderState()
{
	if (GetWorld()) GetWorld()->GetScene().RemoveParticle(this);
	Super::DestroyRenderState();
}

void UParticleSystemComponent::MarkRenderStateDirty()
{
	if (SceneProxy) SceneProxy->UpdateFromComponent();
}

void UParticleSystemComponent::OnTransformDirty()
{
	MarkRenderStateDirty();
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
