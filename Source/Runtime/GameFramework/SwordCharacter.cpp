#include "EnginePCH.h"
#include "GameFramework/SwordCharacter.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimNotify.h"
#include "Animation/Skeleton.h"
#include "Asset/AnimDataModel.h"
#include "Asset/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Camera/CameraComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Particles/ParticleSystem.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <filesystem>

namespace
{
	void ImportSwordAnimationsIfNeeded()
	{
		constexpr const char* Names[] = {
			"Idle",
			"strafe_walk_F", "strafe_walk_B", "strafe_walk_L", "strafe_walk_L45",
			"strafe_walk_L135", "strafe_walk_R", "strafe_walk_R45", "strafe_walk_R135",
			"strafe_run_F", "strafe_run_B", "strafe_run_L", "strafe_run_L45",
			"strafe_run_L135", "strafe_run_R", "strafe_run_R45", "strafe_run_R135",
			"combo02"
		};
		FFbxImportOptions Options;
		Options.TargetSkeletonKey = "Models/SwordModel/SKEL_SK_Mannequin_root.fbxbin";
		Options.bImportStaticMeshes = false;
		Options.bImportSkeletalMeshes = false;
		for (const char* Name : Names)
		{
			const FString Base = FString("Assets/Models/SwordModel/") + Name;
			if (std::filesystem::exists(FbxPath::FromString(
				FString("Assets/Models/SwordModel/AS_") + Name + ".fbxbin"))) continue;
			const FFbxImportResult Result = FFbxAssetLoader::Import(Base + ".FBX", Options);
			if (!Result.bSuccess)
				HTR_LOG(Error, "Failed to import sword animation {}: {}", Name, Result.Error);
		}
	}

	void EnsureSwordAnimationSettings()
	{
		// 이동 클립이 캡슐 밖으로 벗어나지 않도록 루트 본을 고정한다
	}

	void EnsureFallbackMaterialColor(UMaterialInterface* MatInterface, const FVector4& Color)
	{
		UMaterial* Mat = Cast<UMaterial>(MatInterface);
		if (!Mat) return;
		if (Mat->BaseColor.X <= 0.001f && Mat->BaseColor.Y <= 0.001f && Mat->BaseColor.Z <= 0.001f)
			Mat->BaseColor = Color;
	}

	void EnsureSwordMaterials(USkeletalMeshComponent* MeshComponent, UStaticMeshComponent* SwordMeshComponent)
	{
		if (MeshComponent && MeshComponent->GetSkeletalMesh())
		{
			USkeletalMesh* SkelMesh = MeshComponent->GetSkeletalMesh();
			if (SkelMesh->Materials.IsValidIndex(0))
				EnsureFallbackMaterialColor(SkelMesh->Materials[0], FVector4(0.72f, 0.75f, 0.80f, 1.0f));
			if (SkelMesh->Materials.IsValidIndex(1))
				EnsureFallbackMaterialColor(SkelMesh->Materials[1], FVector4(0.18f, 0.20f, 0.24f, 1.0f));
			MeshComponent->MarkRenderStateDirty();
		}
		if (SwordMeshComponent && SwordMeshComponent->GetStaticMesh())
		{
			UStaticMesh* SwordMesh = SwordMeshComponent->GetStaticMesh();
			for (UMaterialInterface* Mat : SwordMesh->Materials)
				EnsureFallbackMaterialColor(Mat, FVector4(0.85f, 0.88f, 0.94f, 1.0f));
			SwordMeshComponent->MarkRenderStateDirty();
		}
	}

	UStaticMesh* LoadOrImportSwordStaticMesh(bool bAllowImport)
	{
		if (std::filesystem::exists(FbxPath::FromString("Assets/Models/SwordModel/SM_Sword_Sword.fbxbin")))
		{
			return UAssetManager::LoadObjStaticMesh("Assets/Models/SwordModel/SM_Sword_Sword.fbxbin");
		}
		std::error_code Ec;
		for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(
			FbxPath::FromString("Assets/Models/SwordModel"), Ec))
		{
			if (!Entry.is_regular_file()) continue;
			const FString FileName = FbxPath::ToString(Entry.path().filename());
			if (FileName.rfind("SM_Sword", 0) == 0 && Entry.path().extension() == ".fbxbin")
			{
				return UAssetManager::LoadObjStaticMesh("Assets/Models/SwordModel/" + FileName);
			}
		}
		if (!bAllowImport) return nullptr;

		FFbxImportOptions Options;
		Options.bImportStaticMeshes = true;
		Options.bImportSkeletalMeshes = false;
		Options.bImportAnimations = false;
		const FFbxImportResult Result = FFbxAssetLoader::Import("Assets/Models/SwordModel/Sword.FBX", Options);
		if (!Result.bSuccess)
		{
			HTR_LOG(Error, "Failed to import Sword.FBX: {}", Result.Error);
			return nullptr;
		}
		for (const FString& Key : Result.AssetKeys)
		{
			if (UStaticMesh* Loaded = UAssetManager::GetAssetByPath<UStaticMesh>(Key))
				return Loaded;
		}
		return nullptr;
	}

	void EnsureSwordSockets(USkeleton* Skeleton)
	{
		if (!Skeleton) return;
		bool bModified = false;

		if (!Skeleton->HasSocket("SwordHandSocket"))
		{
			FSkeletalMeshSocket HandSocket;
			HandSocket.SocketName = "SwordHandSocket";
			HandSocket.BoneName = "hand_r";
			HandSocket.RelativeLocation = FVector(0.035f, 0.085f, -0.01f);
			HandSocket.RelativeRotation = FRotator(0.0f, 0.0f, 180.0f);
			HandSocket.RelativeScale = FVector(1.0f, 1.0f, 1.0f);
			bModified |= Skeleton->AddSocket(HandSocket);
		}

		if (!Skeleton->HasSocket("SwordBaseSocket"))
		{
			const FSkeletalMeshSocket* HandSocket = Skeleton->FindSocket("SwordHandSocket");
			if (HandSocket)
			{
				const FMatrix BaseLocal =
					FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, -0.22f)).GetLocalMatrix() *
					HandSocket->GetSocketLocalMatrix();
				FSkeletalMeshSocket BaseSocket;
				BaseSocket.SocketName = "SwordBaseSocket";
				BaseSocket.BoneName = HandSocket->BoneName;
				BaseSocket.RelativeLocation = FVector(BaseLocal[3][0], BaseLocal[3][1], BaseLocal[3][2]);
				BaseSocket.RelativeRotation = HandSocket->RelativeRotation;
				BaseSocket.RelativeScale = FVector(1.0f, 1.0f, 1.0f);
				bModified |= Skeleton->AddSocket(BaseSocket);
			}
		}

		if (!Skeleton->HasSocket("SwordTipSocket"))
		{
			const FSkeletalMeshSocket* HandSocket = Skeleton->FindSocket("SwordHandSocket");
			if (HandSocket)
			{
				const FMatrix TipLocal =
					FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, -1.36f)).GetLocalMatrix() *
					HandSocket->GetSocketLocalMatrix();
				FSkeletalMeshSocket TipSocket;
				TipSocket.SocketName = "SwordTipSocket";
				TipSocket.BoneName = HandSocket->BoneName;
				TipSocket.RelativeLocation = FVector(TipLocal[3][0], TipLocal[3][1], TipLocal[3][2]);
				TipSocket.RelativeRotation = HandSocket->RelativeRotation;
				TipSocket.RelativeScale = FVector(1.0f, 1.0f, 1.0f);
				bModified |= Skeleton->AddSocket(TipSocket);
			}
		}

		if (bModified && !UAssetManager::SaveSkeleton(*Skeleton))
			HTR_LOG(Warning, "Failed to save sword sockets to SKEL_SK_Mannequin_root.fbxbin");
	}

	struct FRibbonWindow
	{
		const char* Name;
		float Start;
		float End;
	};

	void InstallSwordRibbonNotify()
	{
	}
}

ASwordCharacter::ASwordCharacter()
{
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	if (USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(
		"Assets/Models/SwordModel/SK_SK_Mannequin_root.fbxbin"))
	{
		MeshComponent->SetSkeletalMesh(Mesh);
		EnsureSwordSockets(Mesh->Skeleton);
		MeshComponent->SetAnimGraphPath("Assets/Animations/SwordMannequin.animgraph");
	}

	SwordMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("SwordMeshComponent");
	SwordMeshComponent->SetupAttachmentToSocket(MeshComponent, "SwordHandSocket", EAttachmentRule::KeepRelative);
	SwordMeshComponent->bGenerateOverlapEvents = false;
	SwordMeshComponent->bBlockComponent = false;
	if (UStaticMesh* SwordMesh = LoadOrImportSwordStaticMesh(true))
		SwordMeshComponent->SetStaticMesh(SwordMesh);
	EnsureSwordMaterials(MeshComponent, SwordMeshComponent);

	SwordRibbonComponent = CreateDefaultSubobject<UParticleSystemComponent>("SwordRibbonComponent");
	SwordRibbonComponent->SetupAttachmentToSocket(MeshComponent, "SwordTipSocket", EAttachmentRule::KeepRelative);
	if (UParticleSystem* RibbonTemplate = UParticleSystem::LoadFromFile("Assets/Particles/SwordRibbon.particle", SwordRibbonComponent))
	{
		SwordRibbonComponent->TemplatePath = "Assets/Particles/SwordRibbon.particle";
		SwordRibbonComponent->SetTemplate(RibbonTemplate);
	}
	SwordRibbonComponent->bPreviewInEditor = false;
	SwordRibbonComponent->SetVisibility(false);
	SwordRibbonComponent->Deactivate();
	for (const auto& Effect : { std::pair{ "SwordWaveComponent", "Assets/Particles/BlueSwordWave.particle" },
		std::pair{ "BeamParticleComponent", "Assets/Particles/anit2beam.particle" } })
	{
		UParticleSystemComponent* Component = CreateDefaultSubobject<UParticleSystemComponent>(Effect.first);
		Component->SetupAttachment(MeshComponent);
		if (UParticleSystem* Template = UParticleSystem::LoadFromFile(Effect.second, Component))
		{
			Component->TemplatePath = Effect.second;
			Component->SetTemplate(Template);
		}
		Component->bPreviewInEditor = false;
		Component->SetVisibility(false);
		Component->Deactivate();
	}

	USpringArmComponent* SpringArm = GetSpringArmComponent();
	SpringArm->TargetArmLength = 3.0f;
	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 0.10f);
	SpringArm->SocketOffset = FVector(0.0f, 0.45f, 0.10f);
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.0f;
	SpringArm->bDrawDebug = false;
	GetCameraComponent()->SetFieldOfView(80.0f);

	GetCharacterMovement()->MaxWalkSpeed = 6.0f;
}

void ASwordCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetSpringArmComponent()->ResetCameraLag();
	ImportSwordAnimationsIfNeeded();
	EnsureSwordAnimationSettings();
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	if (USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr)
		EnsureSwordSockets(Mesh->Skeleton);
	if (SwordMeshComponent)
	{
		if (!SwordMeshComponent->GetStaticMesh())
		{
			if (UStaticMesh* SwordMesh = LoadOrImportSwordStaticMesh(true))
				SwordMeshComponent->SetStaticMesh(SwordMesh);
		}
		SwordMeshComponent->SetupAttachmentToSocket(MeshComponent, "SwordHandSocket", EAttachmentRule::KeepRelative);
		SwordMeshComponent->MarkTransformDirty();
	}
	EnsureSwordMaterials(MeshComponent, SwordMeshComponent);
	if (SwordRibbonComponent)
	{
		SwordRibbonComponent->SetupAttachmentToSocket(MeshComponent, "SwordTipSocket", EAttachmentRule::KeepRelative);
		SwordRibbonComponent->SetVisibility(false);
		SwordRibbonComponent->Deactivate();
		SwordRibbonComponent->MarkTransformDirty();
	}
	InstallSwordRibbonNotify();
	if (MeshComponent && MeshComponent->GetAnimGraphPath() != "Assets/Animations/SwordMannequin.animgraph")
	{
		if (!MeshComponent->SetAnimGraphPath("Assets/Animations/SwordMannequin.animgraph"))
			HTR_LOG(Error, "Failed to assign SwordMannequin.animgraph to ASwordCharacter");
	}
}

void ASwordCharacter::Attack()
{
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(MeshComponent ? MeshComponent->GetAnimInstance() : nullptr);
	if (AnimGraph && AnimGraph->IsPlayingMontage("FullBody")) return;
	PlayAnimMontage("Assets/Animations/combo02.animmontage");
}

void ASwordCharacter::CastBeam()
{
	USkeletalMeshComponent* MeshComponent = GetSkeletalMeshComponent();
	UAnimGraphInstance* AnimGraph = Cast<UAnimGraphInstance>(MeshComponent ? MeshComponent->GetAnimInstance() : nullptr);
	if (AnimGraph && AnimGraph->IsPlayingMontage("FullBody")) return;
	PlayAnimMontage("Assets/Animations/BeamMontage.animmontage");
}
