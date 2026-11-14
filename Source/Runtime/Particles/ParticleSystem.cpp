#include "EnginePCH.h"
#include "Particles/ParticleSystem.h"
#include "UObject/Class.h"
#include "UObject/Casts.h"
#include "Serialization/JsonArchive.h"
#include <algorithm>
#include <cmath>

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
	const float A = Alpha ? Alpha->GetFloatValue(T) : 1.0f;
	Particle.Color = FColor(ToByte(R), ToByte(G), ToByte(B), ToByte(A));
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
	if (Module) Modules.Add(Module);
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

UParticleLODLevel* UParticleEmitter::AddLODLevel()
{
	UParticleLODLevel* LOD = NewObject<UParticleLODLevel>(this);
	LOD->Level = static_cast<int32>(LODLevels.Num());
	LOD->GetOrCreateRequired();
	LOD->GetOrCreateSpawn();
	LOD->GetOrCreateTypeData();
	LODLevels.Add(LOD);
	return LOD;
}

UParticleEmitter* UParticleSystem::AddEmitter()
{
	UParticleEmitter* Emitter = NewObject<UParticleEmitter>(this);
	Emitters.Add(Emitter);
	return Emitter;
}

bool UParticleSystem::RemoveEmitter(UParticleEmitter* Emitter)
{
	if (!Emitter) return false;
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

bool UParticleSystem::SaveToFile(const FString& Path)
{
	return FJsonArchive::SaveObject(this, Path);
}

UParticleSystem* UParticleSystem::LoadFromFile(const FString& Path, UObject* Outer)
{
	UParticleSystem* System = NewObject<UParticleSystem>(Outer);
	return FJsonArchive::LoadObject(System, Path) ? System : nullptr;
}
