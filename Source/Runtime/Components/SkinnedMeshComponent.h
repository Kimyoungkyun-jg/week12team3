#pragma once

#include "MeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "SkinnedMeshComponent.generated.h"

UCLASS()
class USkinnedMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	USkinnedMeshComponent();
	virtual ~USkinnedMeshComponent() override = default;

	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	virtual FBox CalcLocalBounds() const override;
protected:
	UPROPERTY(EditAnywhere)
	USkeletalMesh* SkeletalMesh;
};