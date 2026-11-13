#pragma once

#include "GameFramework/Actor.h"
#include "ProjectileActor.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UProjectileMovementComponent;
struct FHitResult;

// Reusable PIE projectile launched by the default pawn.
UCLASS()
class AProjectileActor : public AActor
{
    GENERATED_BODY()
public:
    AProjectileActor();
    void BeginPlay() override;
    void Tick(float DeltaTime) override;
    void Launch(const FVector& Position, const FVector& Direction, bool bUseStaticMesh = false);
    void ReturnToPool();
    bool IsInFlight() const { return bInFlight; }
    UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
    UStaticMeshComponent* GetFireBallMeshComponent() const { return FireBallMeshComponent; }
    UPointLightComponent* GetFireBallLightComponent() const { return FireBallLightComponent; }
    UProjectileMovementComponent* GetProjectileMovementComponent() const { return ProjectileMovementComponent; }
private:
	void HandleProjectileImpact(AActor* OtherActor, const FHitResult& Hit);
    UStaticMeshComponent* StaticMeshComponent = nullptr;
    UStaticMeshComponent* FireBallMeshComponent = nullptr;
    UPointLightComponent* FireBallLightComponent = nullptr;
    UProjectileMovementComponent* ProjectileMovementComponent = nullptr;
    bool bInFlight = false;
    float RemainingLife = 0.0f;
    float LifeTime = 5.0f;
};
