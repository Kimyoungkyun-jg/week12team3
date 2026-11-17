#include "EnginePCH.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "GameFramework/Actor/ZombieActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Particles/ParticleSystem.h"
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
	EnsureBloodParticleActor();
    if (bInFlight)
    {
        ProjectileMovementComponent->Activate(true);
        PrimaryActorTick.SetTickFunctionEnable(true);
    }
}

void AProjectileActor::EndPlay()
{
	if (IsValid(BloodParticleActor) && !BloodParticleActor->IsActorBeingDestroyed())
		BloodParticleActor->Destroy();
	BloodParticleActor = nullptr;
	Super::EndPlay();
}

void AProjectileActor::EnsureBloodParticleActor()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || IsValid(BloodParticleActor)) return;
	BloodParticleActor = Cast<AParticleSystemActor>(World->SpawnActor(AParticleSystemActor::StaticClass()));
	if (!BloodParticleActor) return;
	if (!BloodParticleActor->HasBegunPlay())
		BloodParticleActor->BeginPlay();
	if (UParticleSystemComponent* BloodComp = BloodParticleActor->GetParticleSystemComponent())
	{
		if (UParticleSystem* BloodTemplate = UParticleSystem::LoadFromFile("Assets/Particles/BloodSplatter.particle", BloodComp))
		{
			BloodComp->TemplatePath = "Assets/Particles/BloodSplatter.particle";
			BloodComp->SetTemplate(BloodTemplate);
		}
		// 매 타격마다 파괴하지 않고 완료 후 자동 비활성화된 뒤 재사용한다
		BloodComp->bAutoDestroy = false;
		BloodComp->bPreviewInEditor = false;
		BloodComp->SetVisibility(false);
		BloodComp->Deactivate();
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
			EnsureBloodParticleActor();
			if (IsValid(BloodParticleActor))
			{
				// 미리 만들어 둔 혈흔 액터를 타격 지점으로 옮겨 다시 활성화한다
				FVector OutNormal = Hit.ImpactNormal;
				if (OutNormal.SizeSquared() <= SMALL_NUMBER)
					OutNormal = -ProjectileMovementComponent->Velocity.GetSafeNormal();
				if (OutNormal.SizeSquared() <= SMALL_NUMBER)
					OutNormal = FVector(1.0f, 0.0f, 0.0f);
				FRotator BloodRotation;
				BloodRotation.LookDirection(OutNormal.GetSafeNormal());
				if (UParticleSystemComponent* BloodComp = BloodParticleActor->GetParticleSystemComponent())
				{
					BloodComp->SetWorldLocationAndRotation(Hit.ImpactPoint, BloodRotation, false);
					BloodComp->SetVisibility(true);
					BloodComp->Activate(true);
				}
			}
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
				// 비균일 스케일된 벽·바닥에 붙어 투영 박스가 찌그러지지 않도록 월드 좌표에 직접 배치한다
				Component->SetWorldLocationAndRotation(ImpactPoint, Rotation, false);
				Component->SetWorldScale3D(FVector(0.4f, 0.45f, 0.45f));
                Component->SetDecalMaterial(BulletMaterial);
                Decal->SetTemporaryLifetime(20.0f);
            }
        }
        ReturnToPool();
    }
}
