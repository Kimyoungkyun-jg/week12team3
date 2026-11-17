#include "EnginePCH.h"
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

void UParticleModuleLifetime::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	Particle.Lifetime = std::max(0.001f, Random.Range(Min, Max));
}

void UParticleModuleLocation::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	if (!ParameterName.empty() && Random.InstanceParams)
	{
		if (const FVector* Override = Random.InstanceParams->VectorParams.Find(ParameterName))
		{
			Particle.Location += *Override;
			return;
		}
	}
	Particle.Location += FVector(Random.Range(Min.X, Max.X), Random.Range(Min.Y, Max.Y), Random.Range(Min.Z, Max.Z));
}

void UParticleModuleVelocity::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	if (!ParameterName.empty() && Random.InstanceParams)
	{
		if (const FVector* Override = Random.InstanceParams->VectorParams.Find(ParameterName))
		{
			Particle.Velocity += *Override;
			Particle.BaseVelocity = Particle.Velocity;
			return;
		}
	}
	Particle.Velocity += FVector(Random.Range(Min.X, Max.X), Random.Range(Min.Y, Max.Y), Random.Range(Min.Z, Max.Z));
	Particle.BaseVelocity = Particle.Velocity;
}

void UParticleModuleColor::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	if (!ParameterName.empty() && Random.InstanceParams)
	{
		Particle.Color = Random.InstanceParams->GetColor(ParameterName, Start);
		return;
	}
	Particle.Color = Start;
}

void UParticleModuleColor::Update(FBaseParticle& Particle, float, uint32) const
{
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	const auto Lerp = [T](uint8 A, uint8 B) { return static_cast<uint8>(A + (B - A) * T); };
	Particle.Color = FColor(Lerp(Start.R, End.R), Lerp(Start.G, End.G), Lerp(Start.B, End.B), Lerp(Start.A, End.A));
}

void UParticleModuleSize::Spawn(FBaseParticle& Particle, FParticleRandom&) const
{
	Particle.BaseSize = Start;
	Particle.Size = Start;
}

void UParticleModuleSize::Update(FBaseParticle& Particle, float, uint32) const
{
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	Particle.Size = Start + (End - Start) * T;
}

void UParticleModuleInitialSize::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	if (bUniformScale)
	{
		const float Scale = Random.Range(MinSize.X, MaxSize.X);
		Particle.BaseSize = FVector(Scale, Scale, Scale);
	}
	else
	{
		Particle.BaseSize = FVector(
			Random.Range(MinSize.X, MaxSize.X),
			Random.Range(MinSize.Y, MaxSize.Y),
			Random.Range(MinSize.Z, MaxSize.Z));
	}
	Particle.Size = Particle.BaseSize;
}

UParticleModuleSizeByLife::UParticleModuleSizeByLife()
{
	ScaleX = NewObject<UCurveFloat>(this);
	ScaleY = NewObject<UCurveFloat>(this);
	ScaleZ = NewObject<UCurveFloat>(this);
	ScaleX->AddKey(0.0f, 1.0f);
	ScaleX->AddKey(1.0f, 1.0f);
	ScaleY->AddKey(0.0f, 1.0f);
	ScaleY->AddKey(1.0f, 1.0f);
	ScaleZ->AddKey(0.0f, 1.0f);
	ScaleZ->AddKey(1.0f, 1.0f);
}

void UParticleModuleSizeByLife::Update(FBaseParticle& Particle, float, uint32) const
{
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	const float SX = ScaleX ? ScaleX->GetFloatValue(T) : 1.0f;
	const float SY = ScaleY ? ScaleY->GetFloatValue(T) : 1.0f;
	const float SZ = ScaleZ ? ScaleZ->GetFloatValue(T) : 1.0f;
	Particle.Size = FVector(Particle.BaseSize.X * SX, Particle.BaseSize.Y * SY, Particle.BaseSize.Z * SZ);
}

void UParticleModuleSizeByLife::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (ScaleX) OutCurves.Add({ "Scale X", ScaleX, FColor(255, 80, 80, 255) });
	if (ScaleY) OutCurves.Add({ "Scale Y", ScaleY, FColor(80, 255, 80, 255) });
	if (ScaleZ) OutCurves.Add({ "Scale Z", ScaleZ, FColor(80, 140, 255, 255) });
}

UParticleModuleColorOverLife::UParticleModuleColorOverLife()
{
	ColorR = NewObject<UCurveFloat>(this);
	ColorG = NewObject<UCurveFloat>(this);
	ColorB = NewObject<UCurveFloat>(this);
	Alpha = NewObject<UCurveFloat>(this);
	ColorR->AddKey(0.0f, 1.0f);
	ColorR->AddKey(1.0f, 1.0f);
	ColorG->AddKey(0.0f, 1.0f);
	ColorG->AddKey(1.0f, 1.0f);
	ColorB->AddKey(0.0f, 1.0f);
	ColorB->AddKey(1.0f, 1.0f);
	Alpha->AddKey(0.0f, 1.0f);
	Alpha->AddKey(1.0f, 0.0f);
}

float UParticleModuleColorOverLife::EvaluateAlphaScale(float RelativeTime) const
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
	return Alpha ? std::clamp(Alpha->GetFloatValue(T), 0.0f, 1.0f) : 1.0f;
}

void UParticleModuleColorOverLife::Update(FBaseParticle& Particle, float, uint32) const
{
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	const auto ToByte = [](float V) -> uint8
	{
		return static_cast<uint8>(std::clamp(V, 0.0f, 1.0f) * 255.0f);
	};
	const float R = ColorR ? ColorR->GetFloatValue(T) : 1.0f;
	const float G = ColorG ? ColorG->GetFloatValue(T) : 1.0f;
	const float B = ColorB ? ColorB->GetFloatValue(T) : 1.0f;
	const float AlphaScale = EvaluateAlphaScale(T);
	const uint8 FinalAlpha = static_cast<uint8>(std::lround(static_cast<float>(Particle.BaseAlpha) * AlphaScale));
	Particle.Color = FColor(ToByte(R), ToByte(G), ToByte(B), FinalAlpha);
}

void UParticleModuleColorOverLife::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (ColorR) OutCurves.Add({ "Color R", ColorR, FColor(255, 80, 80, 255) });
	if (ColorG) OutCurves.Add({ "Color G", ColorG, FColor(80, 255, 80, 255) });
	if (ColorB) OutCurves.Add({ "Color B", ColorB, FColor(80, 140, 255, 255) });
	if (Alpha) OutCurves.Add({ "Alpha", Alpha, FColor(220, 220, 220, 255) });
}

void UParticleModuleRotation::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	Particle.Rotation = Random.Range(Min, Max);
	Particle.RotationRate = Random.Range(RateMin, RateMax);
}

UParticleModuleSubUV::UParticleModuleSubUV()
{
	SubImageIndex = NewObject<UCurveFloat>(this);
	SubImageIndex->AddKey(0.0f, 0.0f);
	SubImageIndex->AddKey(1.0f, 0.0f);
}

void UParticleModuleSubUV::Update(FBaseParticle& Particle, float, uint32 Offset) const
{
	if (Offset == 0) return;
	const int32 Cols = std::max(1, SubImagesHorizontal);
	const int32 Rows = std::max(1, SubImagesVertical);
	const int32 TotalFrames = Cols * Rows;
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 0.9999f);
	const float Rate = std::max(0.0f, PlayRate);
	const float ScaledT = T * Rate;
	const float RawIndex = bUseLinearOverLife
		? (Rate > 1.0f ? std::fmod(ScaledT * static_cast<float>(TotalFrames), static_cast<float>(TotalFrames)) : (ScaledT * static_cast<float>(TotalFrames)))
		: (SubImageIndex ? SubImageIndex->GetFloatValue(Rate > 1.0f ? std::fmod(ScaledT, 1.0f) : std::clamp(ScaledT, 0.0f, 1.0f)) : 0.0f);
	FSubUVPayload& Payload = *reinterpret_cast<FSubUVPayload*>(reinterpret_cast<uint8*>(&Particle) + Offset);
	Payload.ImageIndex = std::floor(std::clamp(RawIndex, 0.0f, static_cast<float>(TotalFrames - 1)));
}

void UParticleModuleSubUV::CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const
{
	if (SubImageIndex) OutCurves.Add({ "SubImage Index", SubImageIndex, FColor(255, 200, 80, 255) });
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
	return TypeDataModule;
}

UParticleModuleTypeDataBase* UParticleLODLevel::GetOrCreateTypeData()
{
	if (!TypeDataModule) SetTypeData(UParticleModuleTypeDataSprite::StaticClass());
	return TypeDataModule;
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
	}
	return Module;
}

bool UParticleLODLevel::RemoveModule(UParticleModule* Module)
{
	if (!Module) return false;
	for (uint32 Index = 0; Index < Modules.Num(); ++Index)
	{
		if (Modules[Index] == Module)
		{
			Modules.RemoveAt(Index);
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
		LOD->SpawnModules.Reset();
		LOD->UpdateModules.Reset();
		for (UParticleModule* Module : LOD->EffectiveModules)
		{
			if (!Module || !Module->bEnabled)
			{
				continue;
			}

			if (Module->RunsOnSpawn())
			{
				LOD->SpawnModules.Add(Module);
			}
			if (Module->RunsOnUpdate())
			{
				LOD->UpdateModules.Add(Module);
			}
		}
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
	if (!Emitter)
	{
		return false;
	}

	for (uint32 Index = 0; Index < Emitters.Num(); ++Index)
	{
		if (Emitters[Index] == Emitter)
		{
			Emitters.RemoveAt(Index);
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
	{
		return nullptr;
	}
	
	System->PrepareLODModules();
	return System;
}
