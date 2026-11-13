#pragma once

#include "UObject/Object.h"
#include "Particles/Particle.h"
#include "Render/MaterialInterface.h"
#include "ParticleModule.generated.h"

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()

public:
	virtual void Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const;
	virtual void Update(FBaseParticle& Particle, float DeltaTime) const;
};

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	UMaterialInterface* Material = nullptr;

	UPROPERTY(EditAnywhere)
	bool bUseLocalSpace = false;
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	float SpawnRate = 10.0f;

	UPROPERTY(EditAnywhere)
	int32 BurstCount = 0;
};

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	float MinLifetime = 1.0f;

	UPROPERTY(EditAnywhere)
	float MaxLifetime = 1.0f;

	virtual void Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const override;
};

UCLASS()
class UParticleModuleLocation : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FVector MinOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere)
	FVector MaxOffset = FVector::ZeroVector;

	virtual void Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const override;
};

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FVector MinVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere)
	FVector MaxVelocity = FVector(0.0f, 0.0f, 100.0f);

	virtual void Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const override;
};

UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FLinearColor StartColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere)
	FLinearColor EndColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.0f);

	UPROPERTY(EditAnywhere)
	bool bUseColorOverLife = false;

	virtual void Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const override;
	virtual void Update(FBaseParticle& Particle, float DeltaTime) const override;
};

UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere)
	FVector StartSize = FVector(40.0f, 40.0f, 1.0f);

	UPROPERTY(EditAnywhere)
	FVector EndSize = FVector(40.0f, 40.0f, 1.0f);

	UPROPERTY(EditAnywhere)
	bool bUseSizeOverLife = false;

	virtual void Spawn(FBaseParticle& Particle, const FVector& EmitterOrigin) const override;
	virtual void Update(FBaseParticle& Particle, float DeltaTime) const override;
};