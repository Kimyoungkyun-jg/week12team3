#include "EnginePCH.h"
#include "Core/ResourcePaths.h"
#include "DirectionalLightActor.h"
#include "Asset/AssetManager.h"

ADirectionalLightActor::ADirectionalLightActor()
{
    DirectionalLightComponent = CreateDefaultSubobject<UDirectionalLightComponent>("UDirectionalLightComponent");
	SetupLightRoot(DirectionalLightComponent, ResourcePaths::DirectionalLightIcon);
    DirectionalLightComponent->SetRelativeRotation(FRotator(45.0f, 0.0f, 0.0f));

    // Icon 옆에 붙어서 빛의 방향을 알려주는 기즈모 arrow (로컬 x축)
    ArrowComponent = CreateDefaultSubobject<UStaticMeshComponent>("UArrowComponent");
    ArrowComponent->SetEditorIcon(true);
    ArrowComponent->SetupAttachment(DirectionalLightComponent);
    ArrowComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
    ArrowComponent->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
    ArrowComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
    // 이전 크기는 (0.3, 0.3, 0.5)였다. 길이(Z)와 굵기(XY)를 같은 비율로 3배 키웠다. 게임 뷰에서는 에디터 아이콘 규칙으로 숨겨진다.
    // 컴포넌트 값은 씬 파일에 저장되므로, 이미 저장된 씬은 씬 파일의 값이 우선한다.
    ArrowComponent->SetRelativeScale3D(FVector(0.9f, 0.9f, 1.5f));
    ArrowComponent->SetHiddenInDetails(true);
    ArrowComponent->SetCastShadow(false);
    ArrowComponent->SetReceiveShadow(false);
    ArrowComponent->SetReceiveDecal(false);
}