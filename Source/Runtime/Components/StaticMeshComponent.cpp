#include "EnginePCH.h"
#include "StaticMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Asset/LOD/StaticMeshLODSelector.h"


// StaticMesh 컴포넌트를 초기화한다.
UStaticMeshComponent::UStaticMeshComponent()
{
	StaticMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
}

// StaticMesh 컴포넌트의 소멸을 처리한다.
UStaticMeshComponent::~UStaticMeshComponent()
{
}

// 부모 컴포넌트의 시작 처리를 호출한다.
void UStaticMeshComponent::BeginPlay()
{
	Super::BeginPlay();
}

// 부모 컴포넌트의 프레임 갱신을 호출한다.
void UStaticMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
}

FPrimitiveSceneProxy* UStaticMeshComponent::CreateSceneProxy()
{
	return new FStaticMeshSceneProxy(BuildProxyData(), BuildTransformData());
}

void UStaticMeshComponent::PushRenderStateToProxy()
{
	if (auto* Proxy = static_cast<FStaticMeshSceneProxy*>(GetSceneProxy()))
		Proxy->SetStaticMeshData(BuildProxyData());
}

FStaticMeshProxyData UStaticMeshComponent::BuildProxyData() const
{
	FStaticMeshProxyData Data;
	Data.BaseState = BuildBaseRenderState();
	Data.StaticMesh = StaticMesh;
	Data.bEditorIcon = bEditorIcon;
	if (StaticMesh)
	{
		const int32 SlotCount = GetNumMaterials();
		std::vector<UMaterial*> ResolvedMaterials(SlotCount);
		for (int32 i = 0; i < SlotCount; ++i)
			ResolvedMaterials[i] = GetMaterial(i);
		Data.RenderState = StaticMesh->GetRenderState(ResolvedMaterials);
	}
	return Data;
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
	if (StaticMesh == InStaticMesh)
		return;

	StaticMesh = InStaticMesh;
	ClearOverrideMaterials();
	MarkRenderStateDirty();
	OnTransformDirty();
}

int32 UStaticMeshComponent::GetNumMaterials() const
{
	return StaticMesh ? static_cast<int32>(StaticMesh->GetMeshData().MaterialSlots.Num()) : 0;
}

FString UStaticMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!StaticMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
		return FString();
	return StaticMesh->GetMeshData().MaterialSlots[SlotIndex].Name;
}

UMaterial* UStaticMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return StaticMesh ? StaticMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}
