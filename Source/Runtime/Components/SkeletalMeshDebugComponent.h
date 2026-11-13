#pragma once
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshDebugComponent.generated.h"
class FLineBatcher;

enum class EBoneDrawMode : uint8
{
	AllHierarchy,       // 모든 계층구조
	SelectedOnly,       // 선택된 본만
	SelectedAndParents, // 선택된 본과 부모
	None                // 없음
};

UCLASS()
class USkeletalMeshDebugComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()
public:
	USkeletalMeshDebugComponent();
	~USkeletalMeshDebugComponent() override;
	void SetSkeletalMesh(USkeletalMesh* Mesh) override;
	bool ShouldDrawDebug() const { return bDrawBones || bDrawBoneAxes || bDrawBoneBounds; }
	void DrawDebug(FLineBatcher* Lines) const;
	void SetSelectedBone(int32 Bone);
	int32 GetSelectedBone() const { return SelectedBoneIndex; }
	void SetHoveredBone(int32 Bone) { HoveredBoneIndex = Bone; }
	bool ShouldDrawBone(int32 Bone) const;
	void GetBoneSpheres(TArray<FVector>& Joints, TArray<float>& Radii) const;
	EBoneDrawMode BoneDrawMode = EBoneDrawMode::AllHierarchy;
	bool bDrawBoneBounds = false;
	UPROPERTY(EditAnywhere)
	bool bDrawBones = false;
	UPROPERTY(EditAnywhere)
	bool bDrawBoneAxes = false;
	UPROPERTY(EditAnywhere)
	float BoneAxisLength = 0.05f;
	UPROPERTY(EditAnywhere)
	float BoneRadiusScale = 0.005f;
private:
	int32 SelectedBoneIndex = -1;
	int32 HoveredBoneIndex = -1;
};
