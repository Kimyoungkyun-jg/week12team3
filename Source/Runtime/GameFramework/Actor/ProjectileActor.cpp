#include "EnginePCH.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "GameFramework/Actor/ZombieActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Asset/AssetManager.h"
#include "Collision/HitResult.h"
#include "Collision/Ray.h"
#include "Render/Material.h"
#include "Render/MaterialInstance.h"
#include "Engine/World.h"

AProjectileActor::AProjectileActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot"));
    GetRootComponent()->SetCollisionRadiusOverride(0.5f);
    StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("ProjectileMeshComponent");
    // 이동용 루트는 표시 상태와 분리한다. 숨겨진 메쉬를 부모로 두면 FireBall 자식도 함께 숨겨진다.
    StaticMeshComponent->SetupAttachment(GetRootComponent());
    StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
    static UMaterial* VisibleBulletMaterial = nullptr;
    if (!VisibleBulletMaterial)
    {
		UMaterial* BaseMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (BaseMaterial)
		{
			VisibleBulletMaterial = NewObject<UMaterial>();
			if (VisibleBulletMaterial)
			{
				VisibleBulletMaterial->CopySettingsFrom(*BaseMaterial);
			}
		}
        if (VisibleBulletMaterial)
        {
            VisibleBulletMaterial->BaseColor = FVector4(1.0f, 0.35f, 0.02f, 1.0f);
            VisibleBulletMaterial->EmissiveColor = FVector(1.0f, 0.35f, 0.02f);
            VisibleBulletMaterial->EmissiveFactor = 4.0f;
        }
    }
    if (VisibleBulletMaterial) StaticMeshComponent->SetMaterial(0, VisibleBulletMaterial);
    StaticMeshComponent->SetRelativeScale3D(FVector(0.5f));
    StaticMeshComponent->SetVisible(false);
    StaticMeshComponent->SetHiddenInGame(true);
    StaticMeshComponent->SetAutoActivate(false);
    FireBallLightComponent = CreateDefaultSubobject<UPointLightComponent>("FireBallLightComponent");
    FireBallLightComponent->SetIntensity(1.8f);
    FireBallLightComponent->SetLightColor(FColor(255, 0, 204, 255));
    FireBallLightComponent->SetAttenuationRadius(6.5f);
    FireBallLightComponent->SetFalloff(3.0f);
    FireBallLightComponent->SetupAttachment(GetRootComponent());
    FireBallLightComponent->SetVisibility(false);
    FireBallLightComponent->SetHiddenInGame(true);
    FireBallLightComponent->SetAutoActivate(false);
    // 라이트는 눈에 보이지 않으므로 발광체 역할의 구체를 함께 움직인다.
    FireBallMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("FireBallMeshComponent");
    FireBallMeshComponent->SetupAttachment(GetRootComponent());
    FireBallMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
    FireBallMeshComponent->SetRelativeScale3D(FVector(FireBallLightComponent->GetAttenuationRadius() * 0.5f));
    FireBallMeshComponent->SetVisible(false);
    FireBallMeshComponent->SetHiddenInGame(true);
    FireBallMeshComponent->SetAutoActivate(false);
    ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovementComponent");
    ProjectileMovementComponent->SetAutoActivate(false);
    ProjectileMovementComponent->SetUpdatedComponent(GetRootComponent());
    ProjectileMovementComponent->InintialSpeed = 20.0f;
    ProjectileMovementComponent->MaxSpeed = 100.0f;
    ProjectileMovementComponent->ProjectileGravityScale = 1.0f;
    ProjectileMovementComponent->bShouldBounce = true;
    ProjectileMovementComponent->bRotationFollowsVelocity = true;
    ProjectileMovementComponent->bInitialVelocityInLocalSpace = false;
    ProjectileMovementComponent->bSimulationEnabled = false;
    ProjectileMovementComponent->PrimaryComponentTick.bStartWithTickEnabled = false;
}

void AProjectileActor::BeginPlay()
{
    Super::BeginPlay();
    ProjectileMovementComponent->OnProjectileImpact.AddDynamic(this, &AProjectileActor::HandleProjectileImpact);
    if (bInFlight)
    {
        ProjectileMovementComponent->Activate(true);
        PrimaryActorTick.SetTickFunctionEnable(true);
    }
}

void AProjectileActor::Launch(const FVector& Position, const FVector& Direction, bool bUseStaticMesh)
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || Direction.Dot(Direction) <= 0.0001f)
        return;
    ReturnToPool();
    const FVector Forward = Direction.Normalized();
    GetRootComponent()->SetWorldLocation(Position, false);
    if (bUseStaticMesh)
    {
        StaticMeshComponent->SetVisible(true);
        StaticMeshComponent->Activate();
        StaticMeshComponent->SetHiddenInGame(false);
    }
    else
    {
        FireBallMeshComponent->SetRelativeScale3D(FVector(FireBallLightComponent->GetAttenuationRadius() * 0.5f));
        FireBallMeshComponent->SetVisible(true);
        FireBallMeshComponent->Activate();
        FireBallMeshComponent->SetHiddenInGame(false);
        FireBallLightComponent->SetVisibility(true);
        FireBallLightComponent->Activate();
        FireBallLightComponent->SetHiddenInGame(false);
    }
    ProjectileMovementComponent->SetUpdatedComponent(GetRootComponent());
    ProjectileMovementComponent->Velocity = Forward * ProjectileMovementComponent->InintialSpeed;
    ProjectileMovementComponent->bSimulationEnabled = true;
    ProjectileMovementComponent->Activate(true);
    RemainingLife = LifeTime;
    bInFlight = true;
    PrimaryActorTick.SetTickFunctionEnable(true);
}

void AProjectileActor::ReturnToPool()
{
    bInFlight = false;
    RemainingLife = 0.0f;
    if (ProjectileMovementComponent)
    {
        ProjectileMovementComponent->Velocity = FVector::ZeroVector;
        ProjectileMovementComponent->bSimulationEnabled = false;
        ProjectileMovementComponent->Deactivate();
    }
    if (StaticMeshComponent)
    {
        StaticMeshComponent->Deactivate();
        StaticMeshComponent->SetHiddenInGame(true);
        StaticMeshComponent->SetVisible(false);
    }
    if (FireBallLightComponent)
    {
        FireBallLightComponent->Deactivate();
        FireBallLightComponent->SetHiddenInGame(true);
        FireBallLightComponent->SetVisibility(false);
    }
    if (FireBallMeshComponent)
    {
        FireBallMeshComponent->Deactivate();
        FireBallMeshComponent->SetHiddenInGame(true);
        FireBallMeshComponent->SetVisible(false);
    }
    PrimaryActorTick.SetTickFunctionEnable(false);
}

void AProjectileActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bInFlight) return;
    RemainingLife -= DeltaTime;
    if (RemainingLife <= 0.0f || !ProjectileMovementComponent->bSimulationEnabled) ReturnToPool();
}

void AProjectileActor::HandleProjectileImpact(AActor* OtherActor, const FHitResult& Hit)
{
    if (!bInFlight) return;
    if (AZombieActor* Zombie = Cast<AZombieActor>(OtherActor))
    {
        if (!Zombie->IsDead())
        {
            Zombie->ReactToHit();
            if (!Zombie->IsDead()) Zombie->LaunchCharacter(-Hit.ImpactNormal, 10.0f);
        }
        ReturnToPool();
    }
    else if (AStaticMeshActor* StaticMesh = Cast<AStaticMeshActor>(OtherActor))
    {
		static UMaterialInstanceDynamic* BulletMaterial = nullptr;
        if (!BulletMaterial)
        {
            UMaterial* BaseMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
            UTexture2D* BulletTexture = UAssetManager::Get().LoadTexture("Assets/Textures/bulletdecal.png");
            if (BaseMaterial && BulletTexture)
            {
				BulletMaterial = UMaterial::CreateDynamicInstance(BaseMaterial);
                if (BulletMaterial)
                {
					BulletMaterial->SetTexture0Override(BulletTexture);
					BulletMaterial->SetBaseColorOverride(FVector4(1.0f, 1.0f, 1.0f, 1.0f));
                }
            }
        }
        if (BulletMaterial)
        {
            const FVector Direction = ProjectileMovementComponent->Velocity.GetSafeNormal();
            FVector ImpactPoint = Hit.ImpactPoint;
            FVector SurfaceNormal = Hit.ImpactNormal;
            FHitResult SurfaceHit;
            if (Direction.SizeSquared() > SMALL_NUMBER && StaticMesh->GetStaticMeshComponent() &&
                StaticMesh->GetStaticMeshComponent()->LineTraceComponent(
                    FRay(Hit.ImpactPoint - Direction * 0.1f, Direction), SurfaceHit) &&
                SurfaceHit.Distance <= 2.0f && SurfaceHit.ImpactNormal.SizeSquared() > SMALL_NUMBER)
            {
                ImpactPoint = SurfaceHit.ImpactPoint;
                SurfaceNormal = SurfaceHit.ImpactNormal;
            }

            if (ADecalActor* Decal = Cast<ADecalActor>(GetWorld()->SpawnActor(ADecalActor::StaticClass())))
            {
                UDecalComponent* Component = Decal->GetDecalComponent();
                FRotator Rotation;
                Rotation.LookDirection(-SurfaceNormal);
                Component->SetWorldLocationAndRotation(
                    ImpactPoint - SurfaceNormal * 0.02f, Rotation, false);
                Component->SetWorldScale3D(FVector(0.4f, 0.45f, 0.3f));
                Component->SetDecalMaterial(BulletMaterial);
                Decal->AttachToActor(StaticMesh, EAttachmentRule::KeepWorld);
                Decal->SetTemporaryLifetime(20.0f);
            }
        }
        ReturnToPool();
    }
}
