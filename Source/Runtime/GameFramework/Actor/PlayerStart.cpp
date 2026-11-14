#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "GameFramework/Actor/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/MaterialInstance.h"

APlayerStart::APlayerStart()
{
	SetActorEnableCollision(false);
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	SetRootComponent(CapsuleComponent);
	CapsuleComponent->CapsuleRadius = 0.5f;
	CapsuleComponent->CapsuleHalfHeight = 1.0f;
	CapsuleComponent->ShapeColor = FColor(80, 190, 255, 255);
	CapsuleComponent->bDrawOnlyIfSelected = true;
	CapsuleComponent->bGenerateOverlapEvents = false;
	CapsuleComponent->bBlockComponent = false;

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("BillboardComponent");
	BillboardComponent->SetupAttachment(CapsuleComponent);
	BillboardComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 1.25f));
	BillboardComponent->SetEditorIcon(true);
	BillboardComponent->SetIconSize(1.5f);
	BillboardComponent->SetHiddenInGame(true);
	BillboardComponent->SetHiddenInDetails(true);
	BillboardComponent->bCastShadow = false;
	BillboardComponent->bReceiveShadow = false;

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Icon = UAssetManager::Get().LoadTexture(ResourcePaths::PlayerStartIcon);
	if (Base && Icon)
	{
		UMaterialInstanceDynamic* IconMaterial = UMaterial::CreateDynamicInstance(Base);
		if (!IconMaterial)
		{
			return;
		}

		IconMaterial->SetTexture0Override(Icon);
		BillboardComponent->SetMaterial(0, IconMaterial);
	}

	ArrowComponent = CreateDefaultSubobject<UStaticMeshComponent>("ForwardArrowComponent");
	ArrowComponent->SetupAttachment(CapsuleComponent);
	ArrowComponent->SetEditorIcon(true);
	ArrowComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
	ArrowComponent->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
	// Arrow 메시는 로컬 +Z 방향이므로 +X Forward에 맞춘다.
	ArrowComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	ArrowComponent->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.7f));
	ArrowComponent->SetHiddenInGame(true);
	ArrowComponent->SetHiddenInDetails(true);
	ArrowComponent->SetCastShadow(false);
	ArrowComponent->SetReceiveShadow(false);
}
