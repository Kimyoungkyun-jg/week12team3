#pragma once

#include "GameFramework/Actor.h"
#include "RifleActor.generated.h"

class AProjectileActor;
class USkeletalMeshComponent;

UCLASS()
class ARifleActor : public AActor
{
	GENERATED_BODY()
public:
	ARifleActor();
	void EndPlay() override;

	AProjectileActor* Fire(const FVector& CameraOrigin, const FVector& CameraDirection);
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }
	const TArray<AProjectileActor*>& GetProjectiles() const { return Projectiles; }

private:
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
	TArray<AProjectileActor*> Projectiles;

	UPROPERTY(EditAnywhere)
	float MuzzleDistance = 0.52f;
	UPROPERTY(EditAnywhere)
	float ProjectileSpeed = 80.0f;
	UPROPERTY(EditAnywhere)
	int32 MaxProjectiles = 16;
};
