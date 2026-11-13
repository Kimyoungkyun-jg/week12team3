#include "EnginePCH.h"
#include "GameFramework/TestCharacter.h"
#include "Animation/AnimGraphInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ScriptComponent.h"
#include "GameFramework/Actor/RifleActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "UI/UserWidget.h"
#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"
#include "Engine/World.h"

namespace
{
	void SyncRifleToHandBones(USkeletalMeshComponent* MeshComponent, ARifleActor* EquippedRifle,
		int32 RightHandBoneIndex, int32 LeftHandBoneIndex, const FVector& GripOffset, const FRotator& GripRotation)
	{
		if (!MeshComponent || !IsValid(EquippedRifle) || EquippedRifle->IsActorBeingDestroyed()) return;
		const TArray<FMatrix>& Bones = MeshComponent->GetGlobalBoneTransforms();
		if (!Bones.IsValidIndex(RightHandBoneIndex) || !Bones.IsValidIndex(LeftHandBoneIndex)) return;
		const FMatrix RightHand = Bones[RightHandBoneIndex] * MeshComponent->GetWorldMatrix();
		const FMatrix LeftHand = Bones[LeftHandBoneIndex] * MeshComponent->GetWorldMatrix();
		const FVector RightPosition(RightHand[3][0], RightHand[3][1], RightHand[3][2]);
		const FVector LeftPosition(LeftHand[3][0], LeftHand[3][1], LeftHand[3][2]);
		const FVector Forward = (LeftPosition - RightPosition).GetSafeNormal();
		if (Forward.SizeSquared() < SMALL_NUMBER) return;
		const FVector HandUp(RightHand[2][0], RightHand[2][1], RightHand[2][2]);
		const FVector Up = (HandUp - Forward * HandUp.Dot(Forward)).GetSafeNormal();
		if (Up.SizeSquared() < SMALL_NUMBER) return;
		const FVector Right = Up.Cross(Forward).GetSafeNormal();
		FMatrix RifleWorld = FMatrix::Identity;
		RifleWorld.SetAxis(0, Forward);
		RifleWorld.SetAxis(1, Right);
		RifleWorld.SetAxis(2, Forward.Cross(Right));
		RifleWorld = FTransform(GripRotation, FVector::ZeroVector).GetLocalMatrix() * RifleWorld;
		const FVector RifleForward(RifleWorld[0][0], RifleWorld[0][1], RifleWorld[0][2]);
		const FVector RifleRight(RifleWorld[1][0], RifleWorld[1][1], RifleWorld[1][2]);
		const FVector RifleUp(RifleWorld[2][0], RifleWorld[2][1], RifleWorld[2][2]);
		RifleWorld.SetOrigin(RightPosition + RifleForward * GripOffset.X +
			RifleRight * GripOffset.Y + RifleUp * GripOffset.Z);
		EquippedRifle->GetRootComponent()->SetWorldTransform(RifleWorld, false);
	}
}

ATestCharacter::ATestCharacter()
{
	PrimaryActorTick.TickGroup = ETickingGroup::PostPhysics;
	USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(
		"Assets/Models/UnrealModel/SK_SKM_Quinn_Simple_root.fbxbin");
	if (Mesh)
	{
		SkeletalMeshComponent->SetSkeletalMesh(Mesh);
		if (!SkeletalMeshComponent->SetAnimGraphPath(GetAnimationGraphPath()))
			HTR_LOG(Error, "Failed to assign rifleanimgraph.animgraph to ATestCharacter");
	}
	else HTR_LOG(Error, "Failed to load SK_SKM_Quinn_Simple_root.fbxbin for ATestCharacter");
	GetCharacterMovement()->MaxWalkSpeed = 6.0f;
	USpringArmComponent* SpringArm = GetSpringArmComponent();
	SpringArm->TargetArmLength = 2.8f;
	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 0.10f);
	SpringArm->SocketOffset = FVector(0.0f, 0.55f, 0.10f);
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.0f;
	SpringArm->bDrawDebug = false;
	GetCameraComponent()->SetFieldOfView(NormalFOV);
	UScriptComponent* TuningScript = CreateDefaultSubobject<UScriptComponent>("CameraTuningScript");
	TuningScript->SetScriptPath("Assets/Scripts/TestCharacterTuning.lua");
}

void ATestCharacter::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	if (World && World->IsGameWorld() && MeshComponent &&
		!MeshComponent->SetAnimGraphPath(GetAnimationGraphPath()))
		HTR_LOG(Error, "Failed to assign rifleanimgraph.animgraph to ATestCharacter in BeginPlay");
	if (World && World->IsGameWorld())
	{
		USpringArmComponent* SpringArm = GetSpringArmComponent();
		SpringArm->TargetArmLength = NormalArmLength;
		SpringArm->TargetOffset = FVector(0.0f, 0.0f, 0.10f);
		SpringArm->SocketOffset = FVector(0.0f, 0.55f, 0.10f);
		SpringArm->bEnableCameraLag = true;
		SpringArm->CameraLagSpeed = 12.0f;
		SpringArm->bDrawDebug = false;
		SpringArm->ResetCameraLag();
	}
	USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;
	if (!World || !World->IsGameWorld() || !Mesh) return;
	RightHandBoneIndex = Mesh->GetRefSkeleton().FindBoneIndex("hand_r");
	LeftHandBoneIndex = Mesh->GetRefSkeleton().FindBoneIndex("hand_l");
	if (RightHandBoneIndex < 0 || LeftHandBoneIndex < 0) { HTR_LOG(Error, "Quinn hand bone is missing"); return; }
	ARifleActor* Rifle = Cast<ARifleActor>(World->SpawnActor(ARifleActor::StaticClass()));
	if (!Rifle) return;
	if (!Rifle->AttachToActor(this, EAttachmentRule::KeepWorld, MeshComponent))
	{
		Rifle->Destroy();
		return;
	}
	EquippedRifle = Rifle;
	if (UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(MeshComponent->GetAnimInstance()))
	{
		AnimGraph->SetOnMontageNotify([this](const FString& NotifyName)
		{
			if (NotifyName == "Fire") FireRifle();
		});
	}
}

void ATestCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bAimBlendAlphaOverridden && std::isfinite(DeltaTime) && DeltaTime > 0.0f)
		AimBlendAlpha = std::clamp(AimBlendAlpha + (bIsAiming ? 1.0f : -1.0f) * DeltaTime / 0.18f, 0.0f, 1.0f);
	GetSpringArmComponent()->TargetArmLength = FMath::Lerp(NormalArmLength, AimArmLength, AimBlendAlpha);
	GetCameraComponent()->SetFieldOfView(FMath::Lerp(NormalFOV, AimFOV, AimBlendAlpha));
	if (UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(GetSkeletalMeshComponent()->GetAnimInstance()))
	{
		AnimGraph->SetIsAiming(bIsAiming);
		if (AnimGraph->IsStateActiveOrPending("Hit"))
			GetCharacterMovement()->StopMovementImmediately();
	}
	SyncRifleToHandBones(GetSkeletalMeshComponent(), EquippedRifle,
		RightHandBoneIndex, LeftHandBoneIndex, RifleGripOffset, RifleGripRotation);
}

bool ATestCharacter::SetCameraTuning(float InNormalArmLength, float InNormalFOV,
	float InAimArmLength, float InAimFOV)
{
	if (!std::isfinite(InNormalArmLength) || InNormalArmLength < 0.0f ||
		!std::isfinite(InAimArmLength) || InAimArmLength < 0.0f ||
		!std::isfinite(InNormalFOV) || InNormalFOV <= 1.0f || InNormalFOV >= 179.0f ||
		!std::isfinite(InAimFOV) || InAimFOV <= 1.0f || InAimFOV >= 179.0f)
		return false;
	NormalArmLength = InNormalArmLength;
	NormalFOV = InNormalFOV;
	AimArmLength = InAimArmLength;
	AimFOV = InAimFOV;
	return true;
}

bool ATestCharacter::SetAimBlendAlphaOverride(float Alpha)
{
	if (!std::isfinite(Alpha) || Alpha < 0.0f || Alpha > 1.0f) return false;
	AimBlendAlpha = Alpha;
	bAimBlendAlphaOverridden = true;
	return true;
}

void ATestCharacter::EndPlay()
{
	StopAim();
	if (IsValid(EquippedRifle) && !EquippedRifle->IsActorBeingDestroyed()) EquippedRifle->Destroy();
	EquippedRifle = nullptr;
	Super::EndPlay();
}

void ATestCharacter::StartAim()
{
	if (bIsAiming) return;
	bIsAiming = true;
	if (UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(GetSkeletalMeshComponent()->GetAnimInstance()))
		AnimGraph->SetIsAiming(true);
	if (!AimWidget && GetWorld() && GetWorld()->IsGameWorld())
	{
		AimWidget = CreateWidget(GetWorld(), "Assets/Textures/Aim.widget");
		if (AimWidget)
		{
			AimWidget->AddToViewport(100);
			if (!AimWidget->IsInViewport()) { AimWidget->Destroy(); AimWidget = nullptr; }
		}
	}
}

void ATestCharacter::StopAim()
{
	bIsAiming = false;
	if (UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(GetSkeletalMeshComponent()->GetAnimInstance()))
		AnimGraph->SetIsAiming(false);
	if (AimWidget) { AimWidget->Destroy(); AimWidget = nullptr; }
}

void ATestCharacter::Attack()
{
	if (bAttackLocked || !IsValid(EquippedRifle)) return;
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	if (UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(MeshComponent ? MeshComponent->GetAnimInstance() : nullptr))
	{
		// 발사 노티파이에서 총알을 발사하도록 콜백을 연결하고 발사 몽타주를 재생한다
		AnimGraph->SetOnMontageNotify([this](const FString& NotifyName)
		{
			if (NotifyName == "Fire") FireRifle();
		});
		PlayAnimMontage("Assets/Animations/RifleFire.animmontage");
	}
}

void ATestCharacter::FireRifle()
{
	if (!IsValid(EquippedRifle) || EquippedRifle->IsActorBeingDestroyed()) return;
	UCameraComponent* Camera = GetCameraComponent();
	if (!Camera) return;
	SyncRifleToHandBones(GetSkeletalMeshComponent(), EquippedRifle,
		RightHandBoneIndex, LeftHandBoneIndex, RifleGripOffset, RifleGripRotation);
	if (!EquippedRifle->Fire(Camera->GetWorldLocation(), Camera->GetWorldRotationQuat().GetForwardVector())) return;
	APlayerController* Controller = GetController();
	APlayerCameraManager* CameraManager = Controller ? Controller->GetCameraManager() : nullptr;
	if (CameraManager)
	{
		if (UCameraShakeBase* Shake = CameraManager->StartCameraShake(UCameraShakeBase::StaticClass()))
		{
			Shake->Duration = 0.16f;
			Shake->BlendInTime = 0.0f;
			Shake->BlendOutTime = 0.13f;
			Shake->RotationPitch.Amplitude = 0.65f;
			Shake->RotationPitch.Frequency = 12.0f;
			Shake->RotationPitch.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;
			Shake->RotationYaw.Amplitude = 0.25f;
			Shake->RotationYaw.Frequency = 16.0f;
			Shake->StartShake();
		}
	}
}

void ATestCharacter::Jump()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!bJumpLocked && !bAttackLocked && Movement && Movement->CanJump()) ACharacter::Jump();
}
