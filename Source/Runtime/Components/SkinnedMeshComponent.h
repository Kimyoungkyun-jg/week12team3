#pragma once

#include "MeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "SkinnedMeshComponent.generated.h"

class FSkeletalMeshObject;

UCLASS()
class USkinnedMeshComponent : public UMeshComponent
{
	GENERATED_BODY()
public:
	USkinnedMeshComponent();
	virtual ~USkinnedMeshComponent();

	virtual void TickComponent(float DeltaTime);

	void SetSkeletalMesh(USkeletalMesh* InMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	FSkeletalMeshObject* GetMeshObject() const { return MeshObject.get(); }

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBox CalcLocalBounds() const override;

	// 머티리얼 슬롯은 스켈레탈 에셋이 결정한다.
	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& Out) override;

	// 엔진 전체에 적용한다. 값을 바꾸면 RefreshSkinningMode로 기존 컴포넌트에 반영한다
	inline static bool bUseGPUSkinning = false;
	inline static bool bForceSkinUpdate = false;

	void RefreshSkinningMode();

	void MarkBoneTransformsDirty() { bBoneTransformsDirty = true; }
	void UpdateBoneTransformsNow();

	static void SetGPUSkinningEnabled(bool bEnable);

	const TArray<FMatrix>& GetComponentSpaceTransform() const { return ComponentSpaceTransforms; }

	void SetDebugWeightBone(int32 BoneIndex);
	int32 GetDebugWeightBone() const { return DebugWeightBone; }

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	virtual void RefreshBoneTransforms();
	void SendRenderDynamicData();
	virtual void OnSkeletalMeshChanged() {};

	TArray<FMatrix> ComponentSpaceTransforms;
private:
	void RecreateMeshObject();

	int32 DebugWeightBone = -1;

	UPROPERTY()
	USkeletalMesh* SkeletalMesh = nullptr;
	TUniquePtr<FSkeletalMeshObject> MeshObject;
	bool bBoneTransformsDirty = true;
};
