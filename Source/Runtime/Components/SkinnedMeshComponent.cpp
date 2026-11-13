#include "EnginePCH.h"
#include "Components/SkinnedMeshComponent.h"

USkinnedMeshComponent::USkinnedMeshComponent()
{
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh != InSkeletalMesh)
	{
		SkeletalMesh = InSkeletalMesh;
		ClearOverrideMaterials();
		MarkRenderStateDirty();
	}
}

int32  USkinnedMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? SkeletalMesh->Materials.Num() : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if(SkeletalMesh && SlotIndex < SkeletalMesh->MeshData.MaterialSlots.Num())
	{
		return SkeletalMesh->MeshData.MaterialSlots[SlotIndex].Name;
	}
	return FString();
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SkeletalMesh ? SkeletalMesh->GetMaterial(SlotIndex) : nullptr;
}

FBox USkinnedMeshComponent::CalcLocalBounds() const
{
	if (SkeletalMesh)
	{
		return SkeletalMesh->GetRenderBounds();
	}
	return Super::CalcLocalBounds();
}