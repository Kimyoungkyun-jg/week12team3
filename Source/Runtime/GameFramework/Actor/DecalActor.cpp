#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "GameFramework/Actor/DecalActor.h"

#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"

ADecalActor::ADecalActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	DecalComponent = CreateDefaultSubobject<UDecalComponent>(
		"DecalComponent"
	);

	SetRootComponent(DecalComponent);

	BillboardComponent =
		CreateDefaultSubobject<UBillboardComponent>(
			"DecalBillboardComponent"
		);

	// 데칼 중심을 따라간다.
	BillboardComponent->SetupAttachment(DecalComponent);

	BillboardComponent->SetEditorIcon(true);
	BillboardComponent->SetIconSize(2.0f);

	// 게임에서는 표시하지 않는다.
	BillboardComponent->SetHiddenInGame(true);

	// Details에서는 실제 데칼 컴포넌트를 편집한다.
	BillboardComponent->SetHiddenInDetails(true);

	BillboardComponent->bCastShadow = false;
	BillboardComponent->bReceiveShadow = false;

	BillboardComponent->bGenerateOverlapEvents = false;
	BillboardComponent->bBlockComponent = false;

	UMaterial* BaseMaterial =
		UAssetManager::GetAssetByPath<UMaterial>(
			"BillboardMaterial");

	UTexture2D* Icon =
		UAssetManager::Get().LoadTexture(
			ResourcePaths::DecalIcon);
	
	if (!BaseMaterial || !Icon)
	{
		HTR_LOG(
			Warning,
			"Decal billboard material or icon could not be loaded.");
		return;
	}

	// 공유 BillboardMaterial을 직접 수정하지 않는다.
	UMaterial* IconMaterial = UMaterial::CreateInstance(BaseMaterial);

	if (!IconMaterial)
		return;

	if (IconMaterial->Textures.IsEmpty())
	{
		IconMaterial->Textures.Add(Icon);
	}
	else
	{
		IconMaterial->Textures[0] = Icon;
	}

	IconMaterial->BaseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	BillboardComponent->SetMaterial(
		0, IconMaterial);
}

void ADecalActor::SetTemporaryLifetime(float Seconds)
{
	RemainingLifetime = Seconds;
	PrimaryActorTick.SetTickFunctionEnable(Seconds > 0.0f);
}

void ADecalActor::Tick(float DeltaTime)
{
	RemainingLifetime -= DeltaTime;
	if (RemainingLifetime <= 0.0f) Destroy();
}
