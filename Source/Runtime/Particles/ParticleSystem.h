
#pragma once

#include "Asset/RenderAsset.h"
#include "Particles/ParticleModule.h"
#include "ParticleSystem.generated.h"

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()

public:
	void CreateDefaultSetup();

	UParticleModuleRequired* GetRequiredModule() const { return RequiredModule; }
	UParticleModuleSpawn* GetSpawnModule() const { return SpawnModule; }
	const TArray<UParticleModule*>& GetModules() const { return Modules; }

private:
	UPROPERTY(EditAnywhere, Instanced)
	UParticleModuleRequired* RequiredModule = nullptr;

	UPROPERTY(EditAnywhere, Instanced)
	UParticleModuleSpawn* SpawnModule = nullptr;

	UPROPERTY(EditAnywhere, Instanced)
	TArray<UParticleModule*> Modules;
};

UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()

public:
	void CreateDefaultSetup();

	int32 GetMaxActiveParticles() const { return MaxActiveParticles; }
	UParticleLODLevel* GetLODLevel(int32 Index) const { return LODLevels.IsValidIndex(Index) ? LODLevels[Index] : nullptr; }

	UParticleModuleTypeDataMesh* GetMeshTypeDataModule() const { return MeshTypeDataModule; }

	UParticleModuleTypeDataMesh* CreateMeshTypeData()
	{
		if (!MeshTypeDataModule)
		{
			MeshTypeDataModule = CreateDefaultSubobject<UParticleModuleTypeDataMesh>(FName("MeshTypeData"));
		}
		return MeshTypeDataModule;
	}

	void SetMeshTypeDataModule(UParticleModuleTypeDataMesh* InModule) { MeshTypeDataModule = InModule; }

	const TArray<UParticleLODLevel*>& GetLODLevels() const { return LODLevels; }

private:
	UPROPERTY(EditAnywhere)
	int32 MaxActiveParticles = 1000;

	UPROPERTY(EditAnywhere, Instanced)
	UParticleModuleTypeDataMesh* MeshTypeDataModule = nullptr;

	UPROPERTY(EditAnywhere, Instanced)
	TArray<UParticleLODLevel*> LODLevels;
};

UCLASS()
class UParticleSystem : public URenderAsset
{
	GENERATED_BODY()

public:
	void CreateDefaultSetup();

	UParticleEmitter* GetEmitter(int32 Index) const { return Emitters.IsValidIndex(Index) ? Emitters[Index] : nullptr; }
	const TArray<UParticleEmitter*>& GetEmitters() const { return Emitters; }
	UParticleEmitter* CreateEmitter(const FName& InName)
	{
		UParticleEmitter* Emitter = CreateDefaultSubobject<UParticleEmitter>(InName);
		if (!Emitter)
		{
			return nullptr;
		}
		Emitter->CreateDefaultSetup();
		Emitters.Add(Emitter);
		return Emitter;
	}
private:
	UPROPERTY(EditAnywhere, Instanced)
	TArray<UParticleEmitter*> Emitters;
};
