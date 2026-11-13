#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Containers/Array.h"
#include "ParticleSystem.generated.h"

class UClass;

struct FBaseParticle
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector BaseVelocity = FVector::ZeroVector;
	FVector Size = FVector::OneVector;
	FVector BaseSize = FVector::OneVector;
	FColor Color = FColor(255, 255, 255, 255);
	float RelativeTime = 0.0f;
	float Lifetime = 1.0f;
	float Rotation = 0.0f;
	float RotationRate = 0.0f;
};

struct FParticleRandom
{
	uint32 State = 1;
	float Range(float Min, float Max);
};

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()
public:
	virtual void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const {}
	virtual void Update(FBaseParticle& Particle, float DeltaTime) const {}
};

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	int32 MaxParticles = 1024;
	UPROPERTY(EditAnywhere)
	float EmitterDuration = 0.0f; // 0이면 시간 제한 없이 방출한다.
	UPROPERTY(EditAnywhere)
	bool bLooping = true;
	UPROPERTY(EditAnywhere)
	bool bUseLocalSpace = false;
};

USTRUCT()
struct FParticleBurst
{
	GENERATED_BODY()
	UPROPERTY()
	float Time = 0.0f;
	UPROPERTY()
	int32 Count = 1;
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	float Rate = 10.0f;
	UPROPERTY(EditAnywhere)
	TArray<FParticleBurst> Bursts;
};

enum class EParticleEmitterType : uint8 { Sprite, GpuSprite, Mesh };

UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
	GENERATED_BODY()
public:
	virtual EParticleEmitterType GetEmitterType() const = 0;
};

UCLASS()
class UParticleModuleTypeDataSprite : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::Sprite; }
};

UCLASS()
class UParticleModuleTypeDataGpuSprite : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::GpuSprite; }
};

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	UPROPERTY(EditAnywhere)
	float Min = 1.0f;
	UPROPERTY(EditAnywhere)
	float Max = 1.0f;
};

UCLASS()
class UParticleModuleLocation : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	UPROPERTY(EditAnywhere)
	FVector Min = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector Max = FVector::ZeroVector;
};

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	UPROPERTY(EditAnywhere)
	FVector Min = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector Max = FVector::ZeroVector;
};

UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	void Update(FBaseParticle& Particle, float DeltaTime) const override;
	UPROPERTY(EditAnywhere)
	FColor Start = FColor(255, 255, 255, 255);
	UPROPERTY(EditAnywhere)
	FColor End = FColor(255, 255, 255, 255);
};

UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	void Update(FBaseParticle& Particle, float DeltaTime) const override;
	UPROPERTY(EditAnywhere)
	FVector Start = FVector::OneVector;
	UPROPERTY(EditAnywhere)
	FVector End = FVector::OneVector;
};

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()
public:
	UParticleModule* AddModule(UClass* ModuleClass);
	UParticleModuleRequired* GetOrCreateRequired();
	UParticleModuleSpawn* GetOrCreateSpawn();
	UParticleModuleTypeDataBase* SetTypeData(UClass* TypeDataClass);
	UParticleModuleTypeDataBase* GetOrCreateTypeData();
	UPROPERTY(EditAnywhere)
	int32 Level = 0;
	UPROPERTY(EditAnywhere)
	bool bEnabled = true;
	UPROPERTY(Instanced)
	UParticleModuleRequired* RequiredModule = nullptr;
	UPROPERTY(Instanced)
	UParticleModuleSpawn* SpawnModule = nullptr;
	UPROPERTY(Instanced)
	TArray<UParticleModule*> Modules;
	UPROPERTY(Instanced)
	UParticleModuleTypeDataBase* TypeDataModule = nullptr;
};

UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()
public:
	UParticleLODLevel* AddLODLevel();
	const TArray<UParticleLODLevel*>& GetLODLevels() const { return LODLevels; }
private:
	UPROPERTY(Instanced)
	TArray<UParticleLODLevel*> LODLevels;
};

UCLASS()
class UParticleSystem : public UObject
{
	GENERATED_BODY()
public:
	UParticleEmitter* AddEmitter();
	const TArray<UParticleEmitter*>& GetEmitters() const { return Emitters; }
	bool SaveToFile(const FString& Path);
	static UParticleSystem* LoadFromFile(const FString& Path, UObject* Outer = nullptr);
private:
	UPROPERTY(Instanced)
	TArray<UParticleEmitter*> Emitters;
};
