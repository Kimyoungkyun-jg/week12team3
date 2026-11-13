#pragma once

#include "Components/SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Asset/AnimSequence.h"
#include "Render/RenderEnums.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "SkeletalMeshComponent.generated.h"

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;
	void MarkVisibleForAnimationTick() { bVisibleSinceLastTick = true; }
	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh) override;
	bool SetAnimInstanceClass(UClass* InAnimClass);
	bool SetAnimGraphPath(const FString& InPath);
	const FString& GetAnimGraphPath() const { return AnimGraphPath; }
	UClass* GetAnimInstanceClass() const { return AnimClass; }
	UAnimInstance* GetAnimInstance() const { return AnimScriptInstance; }
	bool SetAnimationSequence(UAnimSequence* InSequence);
	UAnimSequence* GetAnimationSequence() const { return AnimSequence; }

	void InitBoneTransforms();
	void SetBoneTransform(int32 BoneIndex, const FMatrix& LocalTransform);
	void SetGlobalBoneTransforms(const TArray<FMatrix>& InGlobalTransforms);
	const TArray<FMatrix>& GetBoneTransforms() const { return LocalBoneTransforms; }

	void PerformCPUSkinning();
	void UpdateSkinning();
	static void SetSkinningMode(ESkinningMode Mode) { SkinningMode = Mode; }
	static ESkinningMode GetSkinningMode() { return SkinningMode; }
	ESkinningMode GetAppliedSkinningMode() const { return AppliedMode; }
	FStructuredBuffer* GetSkinningMatricesBuffer() const
	{ return AppliedMode == ESkinningMode::GPU ? SkinningMatricesBuffer.get() : nullptr; }
	// stat skinning: 등록된 컴포넌트를 세어 프레임 스탯을 마감한다. 프레임 끝에 한 번 부른다.
	static void EndSkinningStatsFrame();
	// r.Skinning.ForceUpdate: 포즈가 바뀌지 않아도 매 프레임 스키닝을 다시 한다 (애니메이션 재생과 같은 부하로 CPU/GPU 비교).
	static void SetForceSkinningUpdate(bool bForce) { bForceSkinningUpdate = bForce; }
	static bool IsForceSkinningUpdate() { return bForceSkinningUpdate; }
	const TArray<FMatrix>& GetGlobalBoneTransforms() const { return GlobalBoneTransforms; }
	virtual FBox CalcLocalBounds() const override;

	// Bone Weight Heatmap (SkeletalMesh Viewer 전용). 켜면 BoneIndex가 주는 가중치를 정점 색으로 칠한다.
	// 정점 색은 CPU 스키닝 루프에서 쓰므로 켜져 있는 동안 이 컴포넌트는 전역 모드와 상관없이 CPU로 스키닝한다.
	void SetBoneWeightHeatmap(bool bEnable, int32 BoneIndex);
	bool IsBoneWeightHeatmapEnabled() const { return bBoneWeightHeatmap; }

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	FVertexBuffer* GetSkinnedVertexBuffer() const { return SkinnedVertexBuffer.get(); }
	FVertexBuffer* GetSkinnedVertexBuffer(uint32 LOD);
	const TArray<FVertex>& GetSkinnedVertices() const { return SkinnedVertices; }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit) override;
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;
private:
	struct FSkinnedLOD
	{
		TArray<FVertex> Vertices;
		TUniquePtr<FVertexBuffer> Buffer;
		uint64 Revision = 0;
	};
	TArray<FSkinnedLOD> SkinnedLODs;
	struct FComputeSkinnedLOD
	{
		TUniquePtr<FVertexBuffer> Output;
		ComPtr<ID3D11UnorderedAccessView> OutputUAV;
		uint64 Revision = 0;
	};
	TArray<FComputeSkinnedLOD> ComputeSkinnedLODs;
	uint64 ComputeMeshRevision = 0;
	FVertexBuffer* DispatchComputeSkinning(uint32 LOD);
	uint64 PoseRevision = 1;
	void UpdateBoneMatrices();
	// 이 컴포넌트가 실제로 써야 하는 모드. 히트맵은 CPU 경로에만 있으므로 CPU로 고정한다.
	ESkinningMode GetDesiredSkinningMode() const { return bBoneWeightHeatmap ? ESkinningMode::CPU : SkinningMode; }
	// 포즈, 히트맵 색, 모드 중 하나라도 바뀌어 스키닝을 다시 해야 하는가
	bool NeedsSkinningUpdate() const
	{
		return !SkinningMatrices.IsEmpty() && (bSkinningMatricesDirty || bVertexColorsDirty || AppliedMode != GetDesiredSkinningMode());
	}
	bool bBoneWeightHeatmap = false;
	int32 HeatmapBone = -1;
	bool bVertexColorsDirty = false;   // 히트맵 on/off나 선택 본이 바뀌었다. 포즈는 그대로라 본 행렬은 다시 구하지 않는다
	// 씬 로드와 복제는 프로퍼티로 메시 포인터만 채운다. 처음 쓸 때 본 상태를 만든다.
	void EnsureBoneState();
	USkeletalMesh* BoneStateMesh = nullptr;
	// 피킹용 스킨 위치. GPU 스키닝에서도 CPU에서 계산하며 포즈가 바뀐 뒤 처음 피킹할 때만 다시 만든다.
	void UpdatePickPositions();
	TArray<FVector> PickPositions;
	FBox PickBounds;
	uint64 PickRevision = 0;
	UPROPERTY(EditAnywhere)
	bool bOnlyTickPoseWhenRendered = false;
	bool bVisibleSinceLastTick = true;
	UPROPERTY(EditAnywhere, Transient)
	UClass* AnimClass = nullptr;
	UPROPERTY(EditAnywhere, Transient)
	FString AnimGraphPath;
	UPROPERTY(Transient)
	UAnimInstance* AnimScriptInstance = nullptr;
	UPROPERTY(EditAnywhere, Transient)
	UAnimSequence* AnimSequence = nullptr;
	inline static ESkinningMode SkinningMode = ESkinningMode::CPU;
	inline static bool bForceSkinningUpdate = false;
	ESkinningMode AppliedMode = ESkinningMode::CPU;
	TArray<FMatrix> LocalBoneTransforms;
	TArray<FMatrix> GlobalBoneTransforms;
	TArray<FMatrix> SkinningMatrices;
	TArray<FVertex> SkinnedVertices;
	TUniquePtr<FVertexBuffer> SkinnedVertexBuffer;
	TUniquePtr<FStructuredBuffer> SkinningMatricesBuffer;
	FBox SkinnedBounds;
	bool bSkinningMatricesDirty = true;
	bool bGlobalBoneTransformsOverridden = false;
};
