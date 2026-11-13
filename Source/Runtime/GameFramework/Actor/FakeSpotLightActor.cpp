#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "FakeSpotLightActor.h"

#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"

AFakeSpotLightActor::AFakeSpotLightActor()
{
	// 데칼과 빛 줄기의 상대 위치/크기를 유지할 공통 기준이다.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot"));
	// 페이크 스포트라이트 데칼
	{
		DecalComponent = CreateDefaultSubobject<UDecalComponent>("DecalComponent");
		DecalComponent->SetupAttachment(GetRootComponent());

		DecalComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -2.5f));
		DecalComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
		DecalComponent->SetRelativeScale3D(FVector(5.0f, 5.0f, 5.0f));
		DecalComponent->SetUnlit(true);

		UMaterial* DecalMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		UTexture2D* DecalTexture = UAssetManager::Get().LoadTexture(
			ResourcePaths::FakeSpotLightDecal);
		if (!DecalMaterial || !DecalTexture)
		{
			HTR_LOG(
				Warning,
				"FakeSpotLightActor decal material or texture could not be loaded.");
			return;
		}

		UMaterial* DecalMaterialInstance = UMaterial::CreateInstance(DecalMaterial);
		if (!DecalMaterialInstance)
		{
			HTR_LOG(
				Warning,
				"Failed to create instance of decal material for FakeSpotLightActor.");
			return;
		}
		if (DecalMaterialInstance->Textures.IsEmpty())
		{
			DecalMaterialInstance->Textures.Add(DecalTexture);
		}
		else
		{
			DecalMaterialInstance->Textures[0] = DecalTexture;
		}
		DecalMaterialInstance->BaseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		DecalComponent->SetDecalMaterial(DecalMaterialInstance);
	}

	// 페이크 빛 줄기
	{
		FakeLightBillboardComponent = CreateDefaultSubobject<UBillboardComponent>("FakeLightBillboardComponent");
		FakeLightBillboardComponent->SetupAttachment(GetRootComponent());

		FakeLightBillboardComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -2.5f));
		FakeLightBillboardComponent->SetRelativeScale3D(FVector(1.0f, 5.0f, 5.0f));	

		FakeLightBillboardComponent->bCastShadow = false;
		FakeLightBillboardComponent->bBlockComponent = false;
		FakeLightBillboardComponent->bGenerateOverlapEvents = false;
		FakeLightBillboardComponent->bBlockComponent = false;

		UMaterial* BillboardMaterial = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
		UTexture2D* BillboardTexture = UAssetManager::Get().LoadTexture(
			ResourcePaths::FakeSpotLightBillboard);
		if (!BillboardMaterial || !BillboardTexture)
		{
			HTR_LOG(
				Warning,
				"FakeSpotLightActor billboard material or texture could not be loaded.");
			return;
		}

		UMaterial* BillboardMaterialInstance = UMaterial::CreateInstance(BillboardMaterial);
		if (!BillboardMaterial)
		{
			HTR_LOG(
				Warning,
				"Failed to create instance of billboard material for FakeSpotLightActor."
			);
		}
		if (BillboardMaterialInstance->Textures.IsEmpty())
		{
			BillboardMaterialInstance->Textures.Add(BillboardTexture);
		}
		else
		{
			BillboardMaterialInstance->Textures[0] = BillboardTexture;
		}
		FakeLightBillboardComponent->SetMaterial(0, BillboardMaterialInstance);
	}

	// 아이콘
	{
		IconBillboardComponent = CreateDefaultSubobject<UBillboardComponent>("IconBillboardComponent");
		IconBillboardComponent->SetupAttachment(GetRootComponent());
		IconBillboardComponent->SetEditorIcon(true);
		IconBillboardComponent->SetIconSize(2.0f);
		IconBillboardComponent->SetHiddenInGame(true);
		IconBillboardComponent->SetHiddenInDetails(true);
		IconBillboardComponent->bCastShadow = false;
		IconBillboardComponent->bBlockComponent = false;
		IconBillboardComponent->bGenerateOverlapEvents = false;
		IconBillboardComponent->bBlockComponent = false;
		UMaterial* BillboardMaterial = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
		UTexture2D* BillboardTexture = UAssetManager::Get().LoadTexture(
			ResourcePaths::SpotLightIcon);
		if (!BillboardMaterial || !BillboardTexture)
		{
			HTR_LOG(
				Warning,
				"FakeSpotLightActor icon billboard material or texture could not be loaded.");
			return;
		}
		UMaterial* BillboardMaterialInstance = UMaterial::CreateInstance(BillboardMaterial);
		if (!BillboardMaterial)
		{
			HTR_LOG(
				Warning,
				"Failed to create instance of icon billboard material for FakeSpotLightActor."
			);
		}
		if (BillboardMaterialInstance->Textures.IsEmpty())
		{
			BillboardMaterialInstance->Textures.Add(BillboardTexture);
		}
		else
		{
			BillboardMaterialInstance->Textures[0] = BillboardTexture;
		}
		IconBillboardComponent->SetMaterial(0, BillboardMaterialInstance);
	}
}
