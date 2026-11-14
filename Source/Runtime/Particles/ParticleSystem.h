#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Curves/CurveFloat.h"
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

struct FSubUVPayload
{
	float ImageIndex = 0.0f;
};

enum class EParticleSysParamType : uint8
{
	Scalar,
	Vector,
	Color
};

struct FParticleSysParam
{
	FString Name;
	EParticleSysParamType ParamType = EParticleSysParamType::Color;
	float Scalar = 0.0f;
	FVector Vector = FVector::ZeroVector;
	FColor Color = FColor(255, 255, 255, 255);
};

struct FParticleInstanceParameters
{
	TMap<FString, FColor> ColorParams;
	TMap<FString, FVector> VectorParams;
	TMap<FString, float> FloatParams;

	FColor GetColor(const FString& Name, const FColor& Default) const;
	FVector GetVector(const FString& Name, const FVector& Default) const;
	float GetFloat(const FString& Name, float Default) const;
};

struct FParticleRandom
{
	uint32 State = 1;
	const FParticleInstanceParameters* InstanceParams = nullptr;
	float Range(float Min, float Max);
};

struct FParticleCurveTrack
{
	FString Name;
	UCurveFloat* Curve = nullptr;
	FColor Color = FColor(255, 255, 255, 255);
};

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()
public:
	virtual void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const {}
	virtual void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0) const {}
	virtual uint32 RequiredBytes() const { return 0; }
	virtual const char* GetDisplayName() const { return "Module"; }
	virtual void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const {}

	UPROPERTY(EditAnywhere)
	bool bEnabled = true;
};

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()
public:
	const char* GetDisplayName() const override { return "Required"; }
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
	const char* GetDisplayName() const override { return "Spawn"; }
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
	const char* GetDisplayName() const override { return "Lifetime"; }
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
	const char* GetDisplayName() const override { return "Initial Location"; }
	UPROPERTY(EditAnywhere)
	FVector Min = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector Max = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FString ParameterName;
};

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Initial Velocity"; }
	UPROPERTY(EditAnywhere)
	FVector Min = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FVector Max = FVector::ZeroVector;
	UPROPERTY(EditAnywhere)
	FString ParameterName;
};

UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0) const override;
	const char* GetDisplayName() const override { return "Initial Color"; }
	UPROPERTY(EditAnywhere)
	FColor Start = FColor(255, 255, 255, 255);
	UPROPERTY(EditAnywhere)
	FColor End = FColor(255, 255, 255, 255);
	UPROPERTY(EditAnywhere)
	FString ParameterName;
};

UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0) const override;
	const char* GetDisplayName() const override { return "Initial Size"; }
	UPROPERTY(EditAnywhere)
	FVector Start = FVector::OneVector;
	UPROPERTY(EditAnywhere)
	FVector End = FVector::OneVector;
};

UCLASS()
class UParticleModuleSizeByLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSizeByLife();
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0) const override;
	const char* GetDisplayName() const override { return "Size By Life"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;

	UPROPERTY(Instanced)
	UCurveFloat* ScaleX = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* ScaleY = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* ScaleZ = nullptr;
};

UCLASS()
class UParticleModuleColorOverLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleColorOverLife();
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0) const override;
	const char* GetDisplayName() const override { return "Color Over Life"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;

	UPROPERTY(Instanced)
	UCurveFloat* ColorR = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* ColorG = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* ColorB = nullptr;
	UPROPERTY(Instanced)
	UCurveFloat* Alpha = nullptr;
	UPROPERTY(EditAnywhere)
	FString ParameterName;
};

UCLASS()
class UParticleModuleRotation : public UParticleModule
{
	GENERATED_BODY()
public:
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Initial Rotation"; }

	UPROPERTY(EditAnywhere)
	float Min = 0.0f;
	UPROPERTY(EditAnywhere)
	float Max = 0.0f;
	UPROPERTY(EditAnywhere)
	float RateMin = 0.0f;
	UPROPERTY(EditAnywhere)
	float RateMax = 0.0f;
};

UCLASS()
class UParticleModuleSubUV : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSubUV();
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0) const override;
	uint32 RequiredBytes() const override { return sizeof(FSubUVPayload); }
	const char* GetDisplayName() const override { return "SubImage Index"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;

	UPROPERTY(EditAnywhere)
	int32 SubImagesHorizontal = 1;
	UPROPERTY(EditAnywhere)
	int32 SubImagesVertical = 1;
	UPROPERTY(EditAnywhere)
	bool bUseLinearOverLife = true;
	UPROPERTY(EditAnywhere)
	float PlayRate = 1.0f;
	UPROPERTY(Instanced)
	UCurveFloat* SubImageIndex = nullptr;
};

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()
public:
	UParticleModule* AddModule(UClass* ModuleClass);
	bool RemoveModule(UParticleModule* Module);
	bool MoveModule(int32 FromIndex, int32 ToIndex);
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

	UPROPERTY(EditAnywhere)
	FString EmitterName = "Particle Emitter";
	UPROPERTY(EditAnywhere)
	bool bIsSolo = false;
	UPROPERTY(EditAnywhere)
	float PlayRate = 1.0f;
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
	bool RemoveEmitter(UParticleEmitter* Emitter);
	const TArray<UParticleEmitter*>& GetEmitters() const { return Emitters; }
	bool SaveToFile(const FString& Path);
	static UParticleSystem* LoadFromFile(const FString& Path, UObject* Outer = nullptr);

	UPROPERTY(EditAnywhere)
	float UpdateTimeFPS = 60.0f;
	UPROPERTY(EditAnywhere)
	float WarmupTime = 0.0f;
	UPROPERTY(EditAnywhere)
	float WarmupTickRate = 0.0f;
	UPROPERTY(EditAnywhere)
	float SecondsBeforeInactive = 1.0f;
	UPROPERTY(EditAnywhere)
	float PlayRate = 1.0f;
	UPROPERTY(EditAnywhere)
	TArray<float> LODDistances;
private:
	UPROPERTY(Instanced)
	TArray<UParticleEmitter*> Emitters;
};
