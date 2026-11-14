#pragma once

#include "UObject/Object.h"
#include "ParticleModule.generated.h"

class UMaterial;
struct FBaseParticle;

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()

public:
	virtual bool RunsOnSpawn() const { return false; }
	virtual bool RunsOnUpdate() const { return false; }
	virtual void Spawn(FBaseParticle& Particle) const {}
	virtual void Update(FBaseParticle& Particle) const {}
};

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
	GENERATED_BODY()

public:
	UMaterial* Material = nullptr;
};

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
	GENERATED_BODY()

public:
	float Rate = 10.0f;
};

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
	GENERATED_BODY()

public:
	virtual bool RunsOnSpawn() const { return true; }
	void Spawn(FBaseParticle& Particle) const override;

	float LifetimeSeconds = 2.0f;
};

UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
	GENERATED_BODY()
};

UCLASS()
class UParticleModuleColorOverLife : public UParticleModule
{
	GENERATED_BODY()

public:
	virtual bool RunsOnUpdate() const { return true; }
	void Update(FBaseParticle& Particle) const override;
};