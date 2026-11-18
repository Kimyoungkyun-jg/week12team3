#pragma once

#include "UObject/Object.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Color.h"
#include "Containers/Array.h"
#include "Curves/CurveFloat.h"
#include "Particles/ParticleDistribution.h"
#include "Render/ParticleCurveTexture.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "ParticleSystem.generated.h"

class UClass;
class UWorld;
class UParticleSystemComponent;

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
	float BaseRotationRate = 0.0f;
	FQuat MeshRotation = FQuat::Identity();
	uint8 BaseAlpha = 255;
};

struct FSubUVPayload
{
	float ImageIndex = 0.0f;
};

struct FBeamTypeDataPayload
{
	FVector SourcePoint = FVector::ZeroVector;
	FVector TargetPoint = FVector(3.0f, 0.0f, 0.0f);
	FVector SourceTangent = FVector(1.0f, 0.0f, 0.0f);
	FVector TargetTangent = FVector(-1.0f, 0.0f, 0.0f);
	float TravelRatio = 1.0f;
	uint32 NoiseSeed = 1;
	float NoiseLockTimer = 0.0f;
};

struct FRibbonTypeDataPayload
{
	uint32 SpawnOrder = 0;
	int32 TrailIndex = 0;
	FVector SourceUp = FVector(0.0f, 0.0f, 1.0f);
	float WidthOverride = -1.0f;
	FVector EdgeHalfOffset = FVector::ZeroVector;
	bool bUseExplicitEdges = false;
};

struct FParticleCollisionPayload
{
	int32 CollisionCount = 0;
	float Restitution = 0.0f;
	float Friction = 0.0f;
	bool bInitialized = false;
};

struct FParticleEventCollideData
{
	FString EventName;
	FVector Location;
	FVector Normal;
	FVector Velocity;
	UPrimitiveComponent* Component = nullptr;
};

struct FParticleTickContext
{
	UWorld* World = nullptr;
	FMatrix ComponentToWorld;
	FQuat ComponentRotation = FQuat::Identity();
	UParticleSystemComponent * Component = nullptr;
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

// 빌드 단계에서 GPU 커브 채널별로 베이킹된 아틀라스 정보
struct FGpuDistributionBuildChannel
{
	int32 Mode = 0;
	FVector4 ConstantValue = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	FVector4 ValueScale = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	FVector4 ValueBias = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	FParticleCurveAtlasHandle AtlasHandle{};
};

// LOD 컴파일 시 모듈들로부터 수집해 캐싱하는 시뮬레이션 정보
struct FParticleEmitterBuildInfo
{
	~FParticleEmitterBuildInfo();

	void Reset();
	void ReleaseAtlasHandles();

	float MaxLifetime = 1.0f;
	FVector SpawnLocationMin = FVector::ZeroVector;
	FVector SpawnLocationMax = FVector::ZeroVector;
	FVector MaxVelocity = FVector::ZeroVector;
	FVector MaxStartSize = FVector::OneVector;
	FVector MaxLifeScale = FVector::OneVector;

	FVector4 InitialColorStart = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	FString ColorParameterName;

	float SubUVCols = 1.0f;
	float SubUVRows = 1.0f;
	float SubUVPlayRate = 1.0f;
	bool bHasSubUV = false;
	bool bSubUVLinear = true;

	FGpuDistributionBuildChannel SizeScale;
	FGpuDistributionBuildChannel ColorOverLife;
	FGpuDistributionBuildChannel SubUV;
	FGpuDistributionBuildChannel VelocityScale;
	FGpuDistributionBuildChannel RotationRateScale;

	bool bHasCollision = false;
	float CollisionRestitution = 0.5f;
	float CollisionFriction = 0.5f;
	int32 CollisionMaxCount = 10;
	float CollisionRadius = 1.0f;
	float DepthThickness = 1.0f;
	FVector Acceleration = FVector::ZeroVector;
};

UCLASS()
class UParticleModule : public UObject
{
	GENERATED_BODY()
public:
	virtual bool RunsOnSpawn() const { return false; }
	virtual bool RunsOnUpdate() const { return false; }
	virtual void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const {}
	virtual void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0, const FParticleInstanceParameters* InstanceParams = nullptr) const {}
	virtual uint32 RequiredBytes() const { return 0; }
	virtual const char* GetDisplayName() const { return "Module"; }
	virtual void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const {}
	virtual void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const {}
	virtual void InvalidateDistributions() const {}

	UPROPERTY()
	int32 LODSlotId = 0;

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
	UPROPERTY(EditAnywhere)
	UMaterialInterface* Material = nullptr;
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

enum class EParticleEmitterType : uint8 { Sprite, GpuSprite, Mesh, Beam, Ribbon, Afterimage, GpuMesh };
enum class EBeamMethod : uint8 { Distance = 0, Target = 1 };
enum class EBeamEndpointMethod : uint8 { Emitter = 0, UserSet = 1, Parameter = 2 };
enum class EBeamTaperMethod : uint8 { None = 0, Full = 1, Partial = 2 }; // 빔의 시작점에서 끝점으로 갈수록 굵기를 조절할 수 있는 옵션
enum class ERibbonFacingMode : uint8 { Camera = 0, SourceUp = 1, WorldUp = 2 };

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
	const char* GetDisplayName() const override { return "TypeData Sprite"; }
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::Sprite; }
};

UCLASS()
class UParticleModuleTypeDataGpuSprite : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	const char* GetDisplayName() const override { return "TypeData GPU Sprite"; }
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::GpuSprite; }
};

UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	const char* GetDisplayName() const override { return "TypeData Mesh"; }
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::Mesh; }
	UPROPERTY(EditAnywhere)
	UStaticMesh* Mesh = nullptr;
};

UCLASS()
class UParticleModuleTypeDataGpuMesh : public UParticleModuleTypeDataMesh
{
	GENERATED_BODY()
public:
	const char* GetDisplayName() const override { return "TypeData GPU Mesh"; }
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::GpuMesh; }
};

UCLASS()
class UParticleModuleTypeDataAfterimage : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	const char* GetDisplayName() const override { return "TypeData Afterimage"; }
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::Afterimage; }
};

UCLASS()
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	UParticleModuleTypeDataBeam();
	const char* GetDisplayName() const override { return "TypeData Beam"; }
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::Beam; }
	uint32 RequiredBytes() const override { return sizeof(FBeamTypeDataPayload); }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(EditAnywhere)
	int32 BeamMethod = 1;
	UPROPERTY(EditAnywhere)
	int32 MaxBeamCount = 1;
	UPROPERTY(EditAnywhere)
	int32 Segments = 16;
	UPROPERTY(EditAnywhere)
	int32 Sheets = 1;
	UPROPERTY(EditAnywhere)
	float Distance = 3.0f;
	UPROPERTY(EditAnywhere)
	float Speed = 0.0f;
	UPROPERTY(EditAnywhere)
	float TextureTile = 1.0f;
	UPROPERTY(EditAnywhere)
	float TextureTileDistance = 0.0f;
	UPROPERTY(EditAnywhere)
	int32 TaperMethod = 0;
	UPROPERTY(Instanced)
	UDistributionFloat* TaperFactor = nullptr;
	UPROPERTY(Instanced)
	UDistributionFloat* TaperScale = nullptr;
};

UCLASS()
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
	GENERATED_BODY()
public:
	const char* GetDisplayName() const override { return "TypeData Ribbon"; }
	EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::Ribbon; }
	uint32 RequiredBytes() const override { return sizeof(FRibbonTypeDataPayload); }

	UPROPERTY(EditAnywhere)
	int32 MaxTrailCount = 1;
	UPROPERTY(EditAnywhere)
	int32 MaxParticleInTrailCount = 256;
	UPROPERTY(EditAnywhere)
	int32 SheetsPerTrail = 1;
	UPROPERTY(EditAnywhere)
	int32 FacingMode = 0;
	UPROPERTY(EditAnywhere)
	float TilingDistance = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bClipSourceSegment = true;
	UPROPERTY(EditAnywhere)
	FString SourceParameterName = "RibbonBase";
	UPROPERTY(EditAnywhere)
	FString TargetParameterName = "RibbonTip";
};

UCLASS()
class UParticleModuleBeamSource : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleBeamSource();
	const char* GetDisplayName() const override { return "Beam Source"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(EditAnywhere)
	int32 SourceMethod = 0;
	UPROPERTY(Instanced)
	UDistributionVector* Source = nullptr;
	UPROPERTY(EditAnywhere)
	bool bUseSourceTangent = false;
	UPROPERTY(Instanced)
	UDistributionVector* SourceTangent = nullptr;
	UPROPERTY(Instanced)
	UDistributionFloat* SourceStrength = nullptr;
	UPROPERTY(EditAnywhere)
	FString SourceParameterName;
};

UCLASS()
class UParticleModuleBeamTarget : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleBeamTarget();
	const char* GetDisplayName() const override { return "Beam Target"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(EditAnywhere)
	int32 TargetMethod = 1;
	UPROPERTY(Instanced)
	UDistributionVector* Target = nullptr;
	UPROPERTY(EditAnywhere)
	bool bUseTargetTangent = false;
	UPROPERTY(Instanced)
	UDistributionVector* TargetTangent = nullptr;
	UPROPERTY(Instanced)
	UDistributionFloat* TargetStrength = nullptr;
	UPROPERTY(EditAnywhere)
	FString TargetParameterName;
};

UCLASS()
class UParticleModuleBeamNoise : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleBeamNoise();
	const char* GetDisplayName() const override { return "Beam Noise"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(EditAnywhere)
	int32 Frequency = 8;
	UPROPERTY(Instanced)
	UDistributionVector* NoiseRange = nullptr;
	UPROPERTY(EditAnywhere)
	float NoiseLockTime = 0.05f;
	UPROPERTY(EditAnywhere)
	bool bLowFreqEnabled = true;
	UPROPERTY(EditAnywhere)
	bool bSmooth = false;
	UPROPERTY(EditAnywhere)
	float NoiseTension = 0.0f;
	UPROPERTY(EditAnywhere)
	bool bLockEndpoints = true;
	UPROPERTY(EditAnywhere)
	float NoiseLockRadius = 0.0f;
};

UCLASS()
class UParticleModuleSpawnPerUnit : public UParticleModule
{
	GENERATED_BODY()
public:
	const char* GetDisplayName() const override { return "Spawn Per Unit"; }

	UPROPERTY(EditAnywhere)
	float SpawnPerUnit = 10.0f;
	UPROPERTY(EditAnywhere)
	float MovementTolerance = 0.01f;
	UPROPERTY(EditAnywhere)
	float MaxFrameDistance = 50.0f;
	UPROPERTY(EditAnywhere)
	bool bIgnoreSpawnRateWhenMoving = false;
};

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleLifetime();
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Lifetime"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionFloat* Lifetime = nullptr;
};

UCLASS()
class UParticleModuleLocation : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleLocation();
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Initial Location"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionVector* StartLocation = nullptr;
	UPROPERTY(EditAnywhere)
	bool bSampleSkeletalMesh = false;
};

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleVelocity();
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Initial Velocity"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionVector* StartVelocity = nullptr;
};

UCLASS()
class UParticleModuleColor : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleColor();
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Initial Color"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionVector* StartColor = nullptr;
	UPROPERTY(Instanced)
	UDistributionFloat* StartAlpha = nullptr;
};

UCLASS()
class UParticleModuleSize : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSize();
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Initial Size"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionVector* StartSize = nullptr;
};

UCLASS()
class UParticleModuleSizeByLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSizeByLife();
	bool RunsOnUpdate() const override { return true; }
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0, const FParticleInstanceParameters* InstanceParams = nullptr) const override;
	const char* GetDisplayName() const override { return "Size By Life"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionVector* LifeScale = nullptr;
};

UCLASS()
class UParticleModuleColorOverLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleColorOverLife();
	bool RunsOnUpdate() const override { return true; }
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0, const FParticleInstanceParameters* InstanceParams = nullptr) const override;
	const char* GetDisplayName() const override { return "Color Over Life"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;
	float EvaluateAlphaScale(float RelativeTime, const FParticleInstanceParameters* InstanceParams = nullptr) const;

	UPROPERTY(Instanced)
	UDistributionVector* ColorScale = nullptr;
	UPROPERTY(Instanced)
	UDistributionFloat* AlphaScale = nullptr;
	UPROPERTY(EditAnywhere)
	bool bUseFadeInOut = false;
	UPROPERTY(EditAnywhere)
	float FadeInEnd = 0.1f;
	UPROPERTY(EditAnywhere)
	float FadeOutStart = 0.7f;
};

UCLASS()
class UParticleModuleRotation : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleRotation();
	bool RunsOnSpawn() const override { return true; }
	void Spawn(FBaseParticle& Particle, FParticleRandom& Random) const override;
	const char* GetDisplayName() const override { return "Initial Rotation"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionFloat* StartRotation = nullptr;
	UPROPERTY(Instanced)
	UDistributionFloat* StartRotationRate = nullptr;
	UPROPERTY(EditAnywhere)
	bool bRandomAxis3D = false;
};

UCLASS()
class UParticleModuleAcceleration : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleAcceleration();
	bool RunsOnUpdate() const override { return true; }
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0, const FParticleInstanceParameters* InstanceParams = nullptr) const override;
	const char* GetDisplayName() const override { return "Acceleration"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionVector* Acceleration = nullptr;
};

UCLASS()
class UParticleModuleSubUV : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleSubUV();
	bool RunsOnUpdate() const override { return true; }
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0, const FParticleInstanceParameters* InstanceParams = nullptr) const override;
	uint32 RequiredBytes() const override { return sizeof(FSubUVPayload); }
	const char* GetDisplayName() const override { return "SubImage Index"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(EditAnywhere)
	int32 SubImagesHorizontal = 1;
	UPROPERTY(EditAnywhere)
	int32 SubImagesVertical = 1;
	UPROPERTY(EditAnywhere)
	bool bUseLinearOverLife = true;
	UPROPERTY(EditAnywhere)
	float PlayRate = 1.0f;
	UPROPERTY(Instanced)
	UDistributionFloat* SubImageIndex = nullptr;
};

UCLASS()
class UParticleModuleVelocityOverLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleVelocityOverLife();
	bool RunsOnUpdate() const override { return true; }
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0, const FParticleInstanceParameters* InstanceParams = nullptr) const override;
	const char* GetDisplayName() const override { return "Velocity Over Life"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionVector* VelocityScale = nullptr;
};

UCLASS()
class UParticleModuleRotationRateOverLife : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleRotationRateOverLife();
	bool RunsOnUpdate() const override { return true; }
	void Update(FBaseParticle& Particle, float DeltaTime, uint32 Offset = 0, const FParticleInstanceParameters* InstanceParams = nullptr) const override;
	const char* GetDisplayName() const override { return "Rotation Rate Over Life"; }
	void CollectCurves(TArray<FParticleCurveTrack>& OutCurves) const override;
	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;
	void InvalidateDistributions() const override;

	UPROPERTY(Instanced)
	UDistributionFloat* RotationRateScale = nullptr;
};

UCLASS()
class UParticleModuleEventGenerator : public UParticleModule
{
	GENERATED_BODY()
public:
	bool RunsOnUpdate() const override { return false; }
	const char* GetDisplayName() const override { return "Event Generator"; }


public:
	UPROPERTY(EditAnywhere)
	FString EventName = "Hit";
	UPROPERTY(EditAnywhere)
	int32 EventFrequency = 1;
};

UCLASS()
class UParticleModuleCollision : public UParticleModule
{
	GENERATED_BODY()
public:
	void TickCollision(FBaseParticle& Particle, float DeltaTime, uint32 Offset, 
		const FParticleTickContext& Context, const UParticleModuleEventGenerator* EventGenerator) const;
	
	bool RunsOnUpdate() const override { return true; }
	uint32 RequiredBytes() const override { return sizeof(FParticleCollisionPayload); }
	const char* GetDisplayName() const override { return "Collision"; }

	void CompileModules(FParticleEmitterBuildInfo& OutBuildInfo) const override;

public:
	UPROPERTY(EditAnywhere)
	float RestitutionMax = 0.5f;
	UPROPERTY(EditAnywhere)
	float RestitutionMin = 0.0f;

	UPROPERTY(EditAnywhere)
	float FrictionMax = 0.5f;
	UPROPERTY(EditAnywhere)
	float FrictionMin = 0.2f;

	UPROPERTY(EditAnywhere)
	int32 MaxCollisions = 10;

	UPROPERTY(EditAnywhere)
	float CollisionRadius = 1.0f;

	UPROPERTY(EditAnywhere)
	float DepthThickness = 1.0f;
};

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()
public:
	~UParticleLODLevel() override;
	UParticleModule* AddModule(UClass* ModuleClass);
	bool RemoveModule(UParticleModule* Module);
	bool MoveModule(int32 FromIndex, int32 ToIndex);
	UParticleModuleRequired* GetOrCreateRequired();
	UParticleModuleSpawn* GetOrCreateSpawn();
	UParticleModuleTypeDataBase* SetTypeData(UClass* TypeDataClass);
	UParticleModuleTypeDataBase* GetOrCreateTypeData();
	void MarkBuildDirty();
	const FParticleEmitterBuildInfo& EnsureBuilt();

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
	UParticleModuleRequired* EffectiveRequired = nullptr;
	UParticleModuleSpawn* EffectiveSpawn = nullptr;
	UParticleModuleTypeDataBase* EffectiveTypeData = nullptr;
	TArray<UParticleModule*> EffectiveModules;
	bool OwnsModule(const UParticleModule* Module) const;
	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;

private:
	FParticleEmitterBuildInfo BuildInfo;
	bool bBuildDirty = true;
};

UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()
public:
	UParticleLODLevel* AddLODLevel();
	bool UsesModuleInheritance() const { return bInheritLODModules; }
	void EnableModuleInheritance();
	UParticleModule* DuplicateLODModule(int32 TargetIndex, const UParticleModule* Slot, bool bFromHighest);
	bool InheritLODModule(int32 TargetIndex, UParticleModule* Module);
	bool RemoveBaseModule(UParticleModule* Module);
	void InsertLODLevel(int32 Index);
	void RemoveLODLevel(int32 Index);
	bool MoveLODLevel(int32 FromIndex, int32 ToIndex);
	void CacheEmitterModuleInfo();
	const TArray<UParticleLODLevel*>& GetLODLevels() const { return LODLevels; }

	UPROPERTY(EditAnywhere)
	FString EmitterName = "Particle Emitter";
	UPROPERTY(EditAnywhere)
	bool bIsSolo = false;
	UPROPERTY(EditAnywhere)
	float PlayRate = 1.0f;

private:
	UPROPERTY()
	bool bInheritLODModules = false;
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
	UParticleEmitter* DuplicateEmitter(const UParticleEmitter* Source);
	bool MoveEmitter(int32 FromIndex, int32 ToIndex);
	void PrepareLODModules();
	int32 GetLODCount() const;
	bool InsertLOD(int32 Index, bool bCopyPrevious = false);
	bool RemoveLOD(int32 Index);
	float GetLODDistance(int32 Index) const;
	bool SetLODDistance(int32 Index, float Distance);
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
	bool bUseFixedRelativeBoundingBox = false;
	UPROPERTY(EditAnywhere)
	FVector FixedBoundsMin = FVector(-100.0f, -100.0f, -100.0f);
	UPROPERTY(EditAnywhere)
	FVector FixedBoundsMax = FVector(100.0f, 100.0f, 100.0f);
	UPROPERTY(EditAnywhere)
	TArray<float> LODDistances;
private:
	void FillMissingLODDistances();
	UPROPERTY(Instanced)
	TArray<UParticleEmitter*> Emitters;
};

