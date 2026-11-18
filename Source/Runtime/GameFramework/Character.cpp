#include "EnginePCH.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimGraphInstance.h"
#include "Particles/ParticleSystem.h"
#include "Render/Material.h"
#include "Render/MaterialInstance.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>

ACharacter::ACharacter()
{
	SetActorEnableCollision(true);
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	SetRootComponent(CapsuleComponent);
	CapsuleComponent->bBlockComponent = true;
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMeshComponent->SetupAttachment(CapsuleComponent);
	SkeletalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -1.0f));
	SkeletalMeshComponent->PrimaryComponentTick.TickGroup = ETickingGroup::PrePhysics;
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(CapsuleComponent);
	SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.7f));
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->SetRelativeLocation(FVector::ZeroVector);
	CameraComponent->SetExternalInputManaged(true);
	CharacterMovement = CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovement");
	DeathParticleComponent = CreateDefaultSubobject<UParticleSystemComponent>("DeathParticleComponent");
	DeathParticleComponent->SetupAttachment(CapsuleComponent);
	DeathParticleComponent->TemplatePath = DeathParticlePath;
	DeathParticleComponent->bPreviewInEditor = false;
	DeathParticleComponent->SetAutoActivate(false);
	DeathParticleComponent->SetVisibility(false);
	DeathParticleComponent->Deactivate();
}

void ACharacter::BeginPlay()
{
	const FRotator SpawnRotation = GetActorRotation();
	CapsuleComponent->SetRelativeRotation(FRotator(0.0f, SpawnRotation.Yaw, 0.0f));
	SpringArmComponent->SetRelativeRotation(FRotator(SpawnRotation.Pitch, 0.0f, 0.0f));
	CameraComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	SpringArmComponent->ResetCameraLag();
	if (DeathParticleComponent && DeathParticleComponent->GetAttachParent() == SkeletalMeshComponent && CapsuleComponent)
		DeathParticleComponent->SetupAttachment(CapsuleComponent);
	Super::BeginPlay();
}

void ACharacter::AddMovementInput(const FVector& Direction, float ScaleValue)
{
	Super::AddMovementInput(FVector(Direction.X, Direction.Y, 0.0f), ScaleValue);
}

void ACharacter::AddControllerYawInput(float Value)
{
	if (std::isfinite(Value))
	{
		FRotator Rotation = GetActorRotation();
		Rotation.Yaw += Value;
		CapsuleComponent->SetRelativeRotation(Rotation);
	}
}

void ACharacter::AddControllerPitchInput(float Value)
{
	if (std::isfinite(Value))
	{
		FRotator Rotation = SpringArmComponent->GetRelativeRotation();
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
		SpringArmComponent->SetRelativeRotation(Rotation);
	}
}

void ACharacter::Jump() { if (CharacterMovement) CharacterMovement->RequestJump(); }
void ACharacter::StopJumping() { if (CharacterMovement) CharacterMovement->StopJumping(); }

void ACharacter::LaunchCharacter(const FVector& Direction, float Strength)
{
	if (!CharacterMovement || !std::isfinite(Strength) || Strength <= 0.0f ||
		!std::isfinite(Direction.X) || !std::isfinite(Direction.Y) || !std::isfinite(Direction.Z)) return;
	const FVector Normal = Direction.GetSafeNormal();
	if (Normal.SizeSquared() <= SMALL_NUMBER) return;
	CharacterMovement->Launch(Normal * Strength);
}

float ACharacter::PlayAnimMontage(const FString& MontagePath, float InPlayRate, const FString& StartSection)
{
	UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(SkeletalMeshComponent ? SkeletalMeshComponent->GetAnimInstance() : nullptr);
	return AnimGraph ? AnimGraph->PlayMontage(MontagePath, InPlayRate, StartSection) : 0.0f;
}

void ACharacter::SetDeathParticlePath(const FString& InPath)
{
	DeathParticlePath = InPath;
	if (DeathParticleComponent)
	{
		DeathParticleComponent->TemplatePath = InPath;
		DeathParticleComponent->SetTemplate(nullptr);
	}
}

void ACharacter::EnsureDeathDissolveSetup()
{
	if (!DeathParticleComponent)
	{
		for (UActorComponent* Comp : GetComponents())
		{
			UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Comp);
			if (ParticleComp && ParticleComp->TemplatePath == DeathParticlePath)
			{
				DeathParticleComponent = ParticleComp;
				break;
			}
		}
	}
	if (DeathParticleComponent && !DeathParticleComponent->GetTemplate() && !DeathParticlePath.empty())
	{
		if (UParticleSystem* DeathTemplate = UParticleSystem::LoadFromFile(DeathParticlePath, DeathParticleComponent))
		{
			DeathParticleComponent->TemplatePath = DeathParticlePath;
			DeathParticleComponent->SetTemplate(DeathTemplate);
		}
		DeathParticleComponent->bPreviewInEditor = false;
		DeathParticleComponent->SetVisibility(false);
		DeathParticleComponent->Deactivate();
	}

	USkeletalMesh* Mesh = SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMesh() : nullptr;
	if (!Mesh || !DissolveMaterials.IsEmpty())
		return;

	const int32 SlotCount = static_cast<int32>(Mesh->Materials.Num());
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		UMaterialInterface* SlotMat = SkeletalMeshComponent->GetMaterial(SlotIndex);
		UMaterial* BaseMat = SlotMat ? SlotMat->GetBaseMaterial() : nullptr;
		UMaterial* DissolveParent = UMaterial::GetOrCreateDissolveParent(BaseMat);
		UMaterialInstanceDynamic* DynMat = DissolveParent ? UMaterial::CreateDynamicInstance(DissolveParent) : nullptr;
		if (!DynMat)
			continue;

		DynMat->SetScalarParameterValue(FName("DissolveAmount"), -0.10f);
		DynMat->SetVectorParameterValue(FName("DissolveEdgeColor"), FVector4(0.0f, 0.0f, 0.0f, 1.0f));
		SkeletalMeshComponent->SetMaterial(SlotIndex, DynMat);
		DissolveMaterials.Add(DynMat);
	}
}

void ACharacter::SetDeathDissolveParams(float InDuration, const FVector4& InEdgeColor)
{
	if (std::isfinite(InDuration) && InDuration > 0.05f)
		DissolveDuration = InDuration;
	DissolveEdgeColor = InEdgeColor;
}

void ACharacter::StartDeathDissolve()
{
	if (bDissolveStarted)
		return;
	bDissolveStarted = true;
	bDissolveCompleted = false;
	DissolveElapsed = 0.0f;
	EnsureDeathDissolveSetup();

	if (SkeletalMeshComponent)
		SkeletalMeshComponent->bCastShadow = false;

	const FBox MeshBounds = SkeletalMeshComponent ? SkeletalMeshComponent->CalcBounds() : FBox{};
	const float MinZ = SkeletalMeshComponent ? MeshBounds.Min.Z : (GetActorLocation().Z - 1.0f);
	const float MaxZ = SkeletalMeshComponent ? std::max(MeshBounds.Max.Z, MinZ + 0.1f) : (MinZ + 1.8f);

	for (UMaterialInstanceDynamic* DynMat : DissolveMaterials)
	{
		if (!DynMat)
			continue;
		DynMat->SetScalarParameterValue(FName("DissolveAmount"), -0.05f);
		DynMat->SetScalarParameterValue(FName("DissolveMinZ"), MinZ);
		DynMat->SetScalarParameterValue(FName("DissolveMaxZ"), MaxZ);
		DynMat->SetVectorParameterValue(FName("DissolveEdgeColor"), DissolveEdgeColor);
	}
	if (DeathParticleComponent)
		DeathParticleComponent->SetFloatParameter("DissolveHeight", MaxZ);
}

void ACharacter::UpdateDeathDissolve(float DeltaTime)
{
	if (!bDissolveStarted || bDissolveCompleted)
		return;

	DissolveElapsed += DeltaTime;
	const float Alpha = std::clamp(DissolveElapsed / std::max(0.05f, DissolveDuration), 0.0f, 1.0f);
	const float DissolveAmount = -0.05f + Alpha * 1.13f;
	const FBox MeshBounds = SkeletalMeshComponent ? SkeletalMeshComponent->CalcBounds() : FBox{};
	const float MinZ = SkeletalMeshComponent ? MeshBounds.Min.Z : (GetActorLocation().Z - 1.0f);
	const float MaxZ = SkeletalMeshComponent ? std::max(MeshBounds.Max.Z, MinZ + 0.1f) : (MinZ + 1.8f);
	for (UMaterialInstanceDynamic* DynMat : DissolveMaterials)
	{
		if (DynMat)
		{
			DynMat->SetScalarParameterValue(FName("DissolveAmount"), DissolveAmount);
			DynMat->SetScalarParameterValue(FName("DissolveMinZ"), MinZ);
			DynMat->SetScalarParameterValue(FName("DissolveMaxZ"), MaxZ);
		}
	}
	if (DeathParticleComponent)
	{
		const float ActiveNormZ = std::clamp(1.0f - (DissolveAmount - 0.125f) / 0.80f, 0.0f, 1.0f);
		const float DissolveWorldZ = MinZ + ActiveNormZ * (MaxZ - MinZ);
		DeathParticleComponent->SetFloatParameter("DissolveHeight", DissolveWorldZ);
	}
	if (Alpha >= 1.0f)
	{
		bDissolveCompleted = true;
		if (SkeletalMeshComponent)
			SkeletalMeshComponent->SetDissolveHidden(true);
	}
}

bool ACharacter::IsDeathDissolveFinished() const
{
	if (!bDissolveStarted || !bDissolveCompleted)
		return false;
	return !DeathParticleComponent || !DeathParticleComponent->IsActive();
}
