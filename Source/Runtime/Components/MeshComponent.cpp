#include "EnginePCH.h"
#include "MeshComponent.h"

#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Serialization/TypeSerializer.h"

UMeshComponent::UMeshComponent()
{
}

UMeshComponent::~UMeshComponent()
{
}

void UMeshComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
}

UMaterial* UMeshComponent::GetMaterial(int32 SlotIndex) const
{
	if (UMaterial* Override = GetOverrideMaterial(SlotIndex))
	{
		return Override;
	}
	return GetDefaultMaterial(SlotIndex);
}

UMaterial* UMeshComponent::GetOverrideMaterial(int32 SlotIndex) const
{
	if (SlotIndex < 0 || SlotIndex >= OverrideMaterials.Num())
	{
		return nullptr;
	}
	return OverrideMaterials[SlotIndex];
}

void UMeshComponent::SetMaterial(int32 SlotIndex, UMaterial* InMaterial)
{
	if (SlotIndex < 0)
	{
		return;
	}

	while (OverrideMaterials.Num() <= SlotIndex)
	{
		OverrideMaterials.Add(nullptr);
	}
	OverrideMaterials[SlotIndex] = InMaterial;
	MarkRenderStateDirty();
}

void UMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	// StaticMesh(리플렉션)가 먼저 정해져야 슬롯 수가 메시 기준으로 맞는다
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();
	if (Ar.IsLoading())
		ClearOverrideMaterials();

	int32 Num = OverrideMaterials.Num();
	FStructuredArchive::FArray SlotArray = Record.EnterArray("OverrideMaterials", Num);
	for (int32 i = 0; i < Num; ++i)
	{
		UMaterial* Material = Ar.IsSaving() ? OverrideMaterials[i] : nullptr;
		UMaterial::SerializeMaterialReference(SlotArray.EnterElement(), Material);

		if (Ar.IsLoading() && Material)
			SetMaterial(i, Material);   // null이면 덮어쓰기 없음 = 메시 기본값
	}
}
