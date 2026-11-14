#include "EnginePCH.h"

#include "LightActorBase.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/MaterialInstance.h"

ALightActorBase::ALightActorBase() {}

void ALightActorBase::SetupLightRoot(ULightComponent* LightComponent, const FString& IconPath)
{
    SetRootComponent(LightComponent);

    BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
    BillboardComponent->SetEditorIcon(true);
	BillboardComponent->SetHiddenInGame(true);
    BillboardComponent->SetIconSize(2.0f);
    BillboardComponent->SetupAttachment(LightComponent);
    BillboardComponent->SetTintSource(LightComponent);
    BillboardComponent->bCastShadow = false;
    BillboardComponent->bReceiveShadow = false;
    BillboardComponent->SetHiddenInDetails(true);

    UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
    UTexture2D* Icon = UAssetManager::Get().LoadTexture(IconPath);
    if (Base && Icon)
    {
		UMaterialInstanceDynamic* IconMat = UMaterial::CreateDynamicInstance(Base);
		if (!IconMat)
		{
			return;
		}

		IconMat->SetTexture0Override(Icon);
		BillboardComponent->SetMaterial(0, IconMat);
    }
}
