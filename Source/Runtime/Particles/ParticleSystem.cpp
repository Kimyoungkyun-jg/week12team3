#include "EnginePCH.h"
#include "Particles/ParticleSystem.h"
#include "UObject/Class.h"
#include "UObject/Casts.h"
#include "Serialization/JsonArchive.h"
#include <algorithm>
#include <cmath>

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
	Particle.Location += FVector(Random.Range(Min.X, Max.X), Random.Range(Min.Y, Max.Y), Random.Range(Min.Z, Max.Z));
}

void UParticleModuleVelocity::Spawn(FBaseParticle& Particle, FParticleRandom& Random) const
{
	Particle.Velocity += FVector(Random.Range(Min.X, Max.X), Random.Range(Min.Y, Max.Y), Random.Range(Min.Z, Max.Z));
	Particle.BaseVelocity = Particle.Velocity;
}

void UParticleModuleColor::Spawn(FBaseParticle& Particle, FParticleRandom&) const
{
	Particle.Color = Start;
}

void UParticleModuleColor::Update(FBaseParticle& Particle, float) const
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

void UParticleModuleSize::Update(FBaseParticle& Particle, float) const
{
	const float T = std::clamp(Particle.RelativeTime, 0.0f, 1.0f);
	Particle.Size = Start + (End - Start) * T;
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

bool UParticleSystem::SaveToFile(const FString& Path)
{
	return FJsonArchive::SaveObject(this, Path);
}

UParticleSystem* UParticleSystem::LoadFromFile(const FString& Path, UObject* Outer)
{
	UParticleSystem* System = NewObject<UParticleSystem>(Outer);
	return FJsonArchive::LoadObject(System, Path) ? System : nullptr;
}
