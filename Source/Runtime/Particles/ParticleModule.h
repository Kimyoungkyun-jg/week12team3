#pragma once

#include "UObject/Object.h"
#include "ParticleModule.generated.h"

class UMaterial;
class UStaticMesh;
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

	float LifetimeSeconds = 1.0f;
};

UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
	GENERATED_BODY()
};

UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()

public:
	UStaticMesh* Mesh = nullptr;
};

UCLASS()
class UParticleModuleColorOverLife : public UParticleModule
{
	GENERATED_BODY()

public:
	virtual bool RunsOnUpdate() const { return true; }
	void Update(FBaseParticle& Particle) const override;
};

UCLASS()
class UParticleModuleInitialSize : public UParticleModule
{
	GENERATED_BODY()

public:
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle) const override;

	float MinSize = 0.1f;
	float MaxSize = 0.25f;
};

UCLASS()
class UParticleModuleInitialVelocity : public UParticleModule
{
	GENERATED_BODY()

public:
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle) const override;

	FVector MinVelocity{ -2.0f, -2.0f, 1.0f };
	FVector MaxVelocity{ 2.0f, 2.0f, 3.0f };
};