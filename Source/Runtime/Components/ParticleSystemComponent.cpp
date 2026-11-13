#include "EnginePCH.h"
#include "Components/ParticleSystemComponent.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Engine/ParticleSceneProxy.h"
#include <algorithm>
#include <cmath>

void FParticleEmitterInstance::Initialize(UParticleEmitter* Emitter, uint32 Seed)
{
	Reset();
	SpriteTemplate = Emitter;
	CurrentLODLevel = nullptr;
	CurrentLODLevelIndex = 0;
	MaxActiveParticles = 0;
	Random.State = Seed ? Seed : 1;
	if (!Emitter) return;
	const TArray<UParticleLODLevel*>& LODs = Emitter->GetLODLevels();
	if (LODs.IsEmpty()) return;
	CurrentLODLevel = LODs[0];
	if (!CurrentLODLevel) return;
	if (CurrentLODLevel->GetOrCreateTypeData()->GetEmitterType() != EParticleEmitterType::Sprite) return;
	UParticleModuleRequired* Required = CurrentLODLevel->GetOrCreateRequired();
	MaxActiveParticles = std::clamp(Required->MaxParticles, 0, 100000);
	Particles.Reserve(MaxActiveParticles);
}

void FParticleEmitterInstance::Reset()
{
	Particles.Reset();
	ActiveParticles = 0;
	ParticleCounter = 0;
	EmitterTime = 0.0;
	SpawnFraction = 0.0f;
}

void FParticleEmitterInstance::Tick(float DeltaTime, const FVector& Origin)
{
	if (!CurrentLODLevel || !CurrentLODLevel->bEnabled || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	if (CurrentLODLevel->GetOrCreateTypeData()->GetEmitterType() != EParticleEmitterType::Sprite) return;
	UParticleModuleRequired* Required = CurrentLODLevel->GetOrCreateRequired();
	MaxActiveParticles = std::clamp(Required->MaxParticles, 0, 100000);

	// 살아 있는 파티클은 방출이 끝난 뒤에도 수명이 다할 때까지 갱신한다.
	for (int32 Index = 0; Index < static_cast<int32>(Particles.Num());)
	{
		FBaseParticle& Particle = Particles[Index];
		Particle.RelativeTime += DeltaTime / Particle.Lifetime;
		if (Particle.RelativeTime >= 1.0f)
		{
			Particles.RemoveAtSwap(Index);
			continue;
		}
		Particle.Location += Particle.Velocity * DeltaTime;
		Particle.Rotation += Particle.RotationRate * DeltaTime;
		for (const UParticleModule* Module : CurrentLODLevel->Modules)
			if (Module) Module->Update(Particle, DeltaTime);
		++Index;
	}

	const double StartTime = EmitterTime;
	const double EndTime = StartTime + DeltaTime;
	EmitterTime = EndTime;
	const double Duration = Required->EmitterDuration;
	const double SpawnTime = Duration > 0.0 && !Required->bLooping
		? std::clamp(Duration - StartTime, 0.0, static_cast<double>(DeltaTime))
		: static_cast<double>(DeltaTime);
	const UParticleModuleSpawn* SpawnModule = CurrentLODLevel->GetOrCreateSpawn();
	const int32 Available = std::max(0, MaxActiveParticles - static_cast<int32>(Particles.Num()));
	int32 ToSpawn = 0;
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
	for (int32 Index = 0; Index < ToSpawn; ++Index)
	{
		FBaseParticle Particle;
		Particle.Location = Required->bUseLocalSpace ? FVector::ZeroVector : Origin;
		for (const UParticleModule* Module : CurrentLODLevel->Modules)
			if (Module) Module->Spawn(Particle, Random);
		Particle.Lifetime = std::max(Particle.Lifetime, 0.001f);
		Particles.Add(Particle);
		++ParticleCounter;
	}
	ActiveParticles = static_cast<int32>(Particles.Num());
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

void UParticleSystemComponent::ResetParticles()
{
	for (FParticleEmitterInstance& Instance : EmitterInstances) Instance.Reset();
	MarkRenderStateDirty();
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	LoadTemplateIfNeeded();
	if (!Template || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const FVector Origin = GetWorldLocation();
	for (FParticleEmitterInstance& Instance : EmitterInstances) Instance.Tick(DeltaTime, Origin);
	MarkRenderStateDirty();
}
