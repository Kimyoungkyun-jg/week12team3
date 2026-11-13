#include "EnginePCH.h"
#include "GameFramework/Actor/RifleActor.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Collision/HitResult.h"
#include "Collision/Ray.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"

ARifleActor::ARifleActor()
{
	SetActorEnableCollision(false);
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("RifleMeshComponent");
	SetRootComponent(SkeletalMeshComponent);
	USkeletalMesh* RifleMesh = UAssetManager::LoadFbxSkeletalMesh(
		"Assets/Models/UnrealModel/SK_SK_Rifle_Root.fbxbin");
	if (RifleMesh) SkeletalMeshComponent->SetSkeletalMesh(RifleMesh);
	else HTR_LOG(Error, "Failed to load SK_SK_Rifle_Root.fbxbin for ARifleActor");
}

AProjectileActor* ARifleActor::Fire(const FVector& CameraOrigin, const FVector& CameraDirection)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || MaxProjectiles <= 0 || ProjectileSpeed <= 0.0f || IsActorBeingDestroyed()) return nullptr;
	const FVector CameraForward = CameraDirection.GetSafeNormal();
	if (CameraForward.SizeSquared() <= SMALL_NUMBER) return nullptr;
	constexpr float AimTraceDistance = 500.0f;
	FVector Target = CameraOrigin + CameraForward * AimTraceDistance;
	for (AProjectileActor* ExistingProjectile : Projectiles)
	{
		if (IsValid(ExistingProjectile)) ExistingProjectile->SetActorEnableCollision(false);
	}
	FHitResult Hit;
	if (World->SweepSingle(FRay(CameraOrigin, CameraForward), AimTraceDistance, 0.0f,
		Hit, GetAttachParentActor()))
	{
		Target = Hit.ImpactPoint;
	}
	for (AProjectileActor* ExistingProjectile : Projectiles)
	{
		if (IsValid(ExistingProjectile)) ExistingProjectile->SetActorEnableCollision(true);
	}

	AProjectileActor* Projectile = nullptr;
	for (AProjectileActor* Candidate : Projectiles)
	{
		if (IsValid(Candidate) && !Candidate->IsActorBeingDestroyed() && !Candidate->IsInFlight())
		{
			Projectile = Candidate;
			break;
		}
	}
	if (!Projectile && Projectiles.Num() < static_cast<uint32>(MaxProjectiles))
	{
		Projectile = Cast<AProjectileActor>(World->SpawnActor(AProjectileActor::StaticClass()));
		if (!Projectile) return nullptr;
		Projectile->GetRootComponent()->SetCollisionRadiusOverride(0.035f);
		Projectile->GetStaticMeshComponent()->SetRelativeScale3D(FVector(0.14f, 0.045f, 0.045f));
		Projectiles.Add(Projectile);
	}
	if (!Projectile) return nullptr;

	UProjectileMovementComponent* Movement = Projectile->GetProjectileMovementComponent();
	Movement->InintialSpeed = ProjectileSpeed;
	Movement->MaxSpeed = ProjectileSpeed;
	Movement->ProjectileGravityScale = 0.0f;
	Movement->bShouldBounce = false;
	const FVector Muzzle = GetActorLocation() + GetActorQuat().GetForwardVector() * MuzzleDistance;
	FVector FireDirection = (Target - Muzzle).GetSafeNormal();
	if (FireDirection.SizeSquared() <= SMALL_NUMBER) FireDirection = CameraForward;
	Projectile->Launch(Muzzle, FireDirection, true);
	return Projectile;
}

void ARifleActor::EndPlay()
{
	for (AProjectileActor* Projectile : Projectiles)
	{
		if (IsValid(Projectile) && !Projectile->IsActorBeingDestroyed()) Projectile->Destroy();
	}
	Projectiles.Reset();
	Super::EndPlay();
}
