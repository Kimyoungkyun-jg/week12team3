#pragma once

#include "Components/SceneComponent.h"
#include "Particles/ParticleSystem.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "ParticleSystemComponent.generated.h"

class FScene;
class FParticleSceneProxy;
struct FGpuParticleEmitterResources;

#define DECLARE_PARTICLE(Name, Address) \
	FBaseParticle& Name = *((FBaseParticle*)(Address));

#define DECLARE_PARTICLE_CONST(Name, Address) \
	const FBaseParticle& Name = *((const FBaseParticle*)(Address));

#define DECLARE_PARTICLE_PAYLOAD(Type, Name, Particle, Offset) \
	Type& Name = *reinterpret_cast<Type*>(reinterpret_cast<uint8*>(&(Particle)) + (Offset));

struct FGpuParticle
{
	FVector Position = FVector::ZeroVector;
	float RelativeTime = 0.0f;
	FVector Velocity = FVector::ZeroVector;
	float Lifetime = 1.0f;
	FVector BaseSize = FVector::OneVector;
	float Rotation = 0.0f;
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	FVector Size = FVector::OneVector;
	float RotationRate = 0.0f;
	FVector4 BaseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	float SubImageIndex = 0.0f;
	uint32 bAlive = 0;
	float Pad[2] = {};
};

struct FParticleEmitterInstance
{
	UParticleEmitter* SpriteTemplate = nullptr;
	UParticleLODLevel* CurrentLODLevel = nullptr;
	int32 CurrentLODLevelIndex = 0;
	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
	int32 ParticleSize = sizeof(FBaseParticle);
	int32 ParticleStride = (sizeof(FBaseParticle) + 15) & ~15;
	int32 PayloadOffset = sizeof(FBaseParticle);
	TMap<const UParticleModule*, uint32> ModuleOffsetMap;
	int32 ActiveParticles = 0;
	uint32 ParticleCounter = 0;
	int32 MaxActiveParticles = 0;
	double EmitterTime = 0.0;
	float SpawnFraction = 0.0f;
	FParticleRandom Random;
	const FParticleInstanceParameters* InstanceParameters = nullptr;

	TSharedPtr<FGpuParticleEmitterResources> GpuResources;
	TArray<FGpuParticle> PendingGpuSpawns;
	uint32 GpuWriteCursor = 0;
	float LastGpuDeltaTime = 0.0f;
	bool bGpuResetRequested = false;

	FParticleEmitterInstance() = default;
	~FParticleEmitterInstance();
	FParticleEmitterInstance(const FParticleEmitterInstance& Other);
	FParticleEmitterInstance& operator=(const FParticleEmitterInstance& Other);
	FParticleEmitterInstance(FParticleEmitterInstance&& Other) noexcept;
	FParticleEmitterInstance& operator=(FParticleEmitterInstance&& Other) noexcept;

	void Initialize(UParticleEmitter* Emitter, uint32 Seed);
	void UpdateStride();
	uint32 GetModuleDataOffset(const UParticleModule* Module) const;
	void Resize(int32 NewMaxActiveParticles);
	void SetLODLevel(int32 NewLODIndex);
	void Tick(float DeltaTime, const FVector& Origin);
	void Reset();
	bool HasCompleted() const;
	bool IsGpuEmitter() const;
};

UCLASS()
class UParticleSystemComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UParticleSystemComponent();
	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime) override;
	void Activate(bool bReset = false) override;
	void Deactivate() override;
	bool HasCompleted() const;
	void SetTemplate(UParticleSystem* InTemplate);
	UParticleSystem* GetTemplate() const { return Template; }
	const TArray<FParticleEmitterInstance>& GetEmitterInstances() const { return EmitterInstances; }
	TArray<FParticleEmitterInstance>& GetEmitterInstancesMutable() { return EmitterInstances; }
	void UpdateLOD(float Distance);
	void UpdateEmitterStrides();
	void ResetParticles();
	void SetColorParameter(const FString& Name, const FColor& Value);
	void SetVectorParameter(const FString& Name, const FVector& Value);
	void SetFloatParameter(const FString& Name, float Value);
	FColor GetColorParameter(const FString& Name, const FColor& Default) const;
	FVector GetVectorParameter(const FString& Name, const FVector& Default) const;
	float GetFloatParameter(const FString& Name, float Default) const;
	const FParticleInstanceParameters& GetInstanceParameters() const { return InstanceParameters; }
	TArray<FParticleSysParam>& GetInstanceParamList() { return InstanceParamList; }
	void SyncInstanceParameters();

	using USceneComponent::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
	void MarkRenderStateDirty() override;
	void OnTransformDirty() override;

	UPROPERTY()
	FString TemplatePath;
	UPROPERTY(EditAnywhere)
	bool bPreviewInEditor = true;
	UPROPERTY(EditAnywhere)
	bool bAutoDestroy = false;
	UPROPERTY(EditAnywhere)
	float CustomTimeDilation = 1.0f;

private:
	friend class FScene;
	bool ShouldCreateRenderState() const override { return true; }
	void CreateRenderState() override;
	void DestroyRenderState() override;
	void LoadTemplateIfNeeded();
	FParticleSceneProxy* SceneProxy = nullptr;
	UParticleSystem* Template = nullptr;
	TArray<FParticleEmitterInstance> EmitterInstances;
	TArray<FParticleSysParam> InstanceParamList;
	FParticleInstanceParameters InstanceParameters;
	FString LoadedTemplatePath;
};
