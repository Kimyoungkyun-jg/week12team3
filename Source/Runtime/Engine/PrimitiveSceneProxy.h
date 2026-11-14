#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Math/Box.h"
#include "Math/Frustum.h"
#include "Math/Sphere.h"
#include "Math/Vector.h"
#include "Render/MeshRenderData.h"

class FScene;
class UStaticMesh;
class USkeletalMesh;
class UMaterial;
class FVertexBuffer;
class FIndexBuffer;
class FStructuredBuffer;
class FRenderQueue;
struct FViewContext;
enum class ESkinningMode : uint8;

struct alignas(16) FBillboardSubUVConstants
{
	float CurrentFrame = 0.0f;
	float RowSize = 1.0f;
	float ColSize = 1.0f;
	float Alpha = 1.0f;
	FVector4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
	float DecodeSRGB = 0.0f;
	float Padding[3]{};
};

// Transform 스냅샷
struct FPrimitiveTransformData
{
	FMatrix LocalToWorld = FMatrix::Identity;
	FMatrix WorldToLocal = FMatrix::Identity;
	FAABB WorldBounds{};
	FVector WorldScale3D = FVector::OneVector;
};

// 기본 가시성 및 그림자 플래그 스냅샷
struct FPrimitiveRenderStateData
{
	bool bVisible = true;
	bool bShownInGame = true;
	bool bShownInEditor = true;
	bool bCastsShadow = true;
	bool bReceiveShadow = true;
	bool bReceiveDecal = true;
};

// StaticMesh 프록시 상태 스냅샷
struct FStaticMeshProxyData
{
	FPrimitiveRenderStateData BaseState;
	UStaticMesh* StaticMesh = nullptr;
	TSharedPtr<const FMeshRenderState> RenderState;
	bool bEditorIcon = false;
};

// SkeletalMesh 프록시 상태 스냅샷
struct FSkeletalMeshProxyData
{
	FPrimitiveRenderStateData BaseState;
	USkeletalMesh* SkeletalMesh = nullptr;
	TSharedPtr<const FMeshRenderState> RenderState;
	uint8 AppliedMode = 0;
	FVertexBuffer* CPUSkinnedVertexBuffer = nullptr;
	FStructuredBuffer* SkinningMatricesBuffer = nullptr;
	bool bBoneWeightHeatmap = false;
};

// SubUV 파티클 단일 인스턴스 스냅샷
struct FBillboardSubUVParticleData
{
	FVector Location = FVector::ZeroVector;
	float Scale = 1.0f;
	float Alpha = 1.0f;
	uint32 SubUVFrame = 0;
	bool bAlive = false;
};

// Billboard 및 SubUV 프록시 상태 스냅샷
struct FBillboardProxyData
{
	FPrimitiveRenderStateData BaseState;
	UStaticMesh* QuadMesh = nullptr;
	UMaterial* Material = nullptr;
	FBillboardSubUVConstants IconParams{};
	float IconSize = 1.0f;
	bool bEditorIcon = false;
	bool bSubUVEmitter = false;
	bool bActive = true;
	bool bOpaqueMaterial = false;
	uint32 ColSize = 1;
	uint32 RowSize = 1;
	TArray<FBillboardSubUVParticleData> SubUVParticles;
};

class FPrimitiveSceneProxy
{
public:
	FPrimitiveSceneProxy() = default;
	virtual ~FPrimitiveSceneProxy() = default;

	void ApplyTransformData(const FPrimitiveTransformData& Data);
	void ApplyBaseRenderState(const FPrimitiveRenderStateData& Data);

	virtual void OnMeshRenderDataChanged() {}
	virtual void OnMeshDestroyed() {}
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext, uint32 LODIndex) const {}

	FScene* GetScene() const { return Scene; }

	const FMatrix& GetLocalToWorld() const { return LocalToWorld; }
	const FMatrix& GetWorldToLocal() const { return WorldToLocal; }
	const FAABB& GetBounds() const { return Bounds; }
	const FLODSphere& GetLODSphere() const { return LODSphere; }

	bool IsVisible() const { return bVisible; }
	bool IsShown(bool bGameView, bool bShowSkeletalMeshes = true) const;

	void MarkRendered() const { bRenderedThisFrame = true; }
	bool ConsumeRenderedFlag()
	{
		const bool bResult = bRenderedThisFrame;
		bRenderedThisFrame = false;
		return bResult;
	}

	virtual UStaticMesh* GetMesh() const { return nullptr; }
	virtual UStaticMesh* GetStaticMesh() const { return GetMesh(); }
	virtual USkeletalMesh* GetSkeletalMesh() const { return nullptr; }
	virtual const FMeshRenderState* GetRenderState() const { return nullptr; }
	virtual uint32 GetLODCount() const { return 0; }
	virtual const FCachedMeshLOD& GetLOD(uint32 LODIndex) const;
	virtual const FCachedMeshSection& GetSection(uint32 SectionIndex) const;
	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const { return nullptr; }
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const { return nullptr; }
	virtual bool IsComputeSkinned() const { return false; }
	virtual FStructuredBuffer* GetSkinningMatricesBuffer() const { return nullptr; }
	virtual const class FParticleSceneProxy* AsParticleSceneProxy() const { return nullptr; }

	FScene* Scene = nullptr;
	int32 PackedIndex = -1;
	bool bQueuedForUpdate = false;
	bool bRenderStateQueued = false;
	bool bCastsShadow = true;
	bool bReceiveShadow = true;
	bool bReceiveDecal = true;

protected:
	void UpdateLODSphere();

	FMatrix LocalToWorld = FMatrix::Identity;
	FMatrix WorldToLocal = FMatrix::Identity;
	FAABB Bounds{};
	FLODSphere LocalLODSphere{};
	FLODSphere LODSphere{};
	FVector WorldScale3D = FVector::OneVector;

	bool bVisible = true;
	bool bShownInGame = true;
	bool bShownInEditor = true;
	bool bIsSkeletalMesh = false;
	mutable bool bRenderedThisFrame = false;
};

class FStaticMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FStaticMeshSceneProxy(const FStaticMeshProxyData& InitialData, const FPrimitiveTransformData& InitialTransform);
	virtual ~FStaticMeshSceneProxy() override;

	void SetStaticMeshData(const FStaticMeshProxyData& Data);

	virtual void OnMeshRenderDataChanged() override;
	virtual void OnMeshDestroyed() override;
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext, uint32 LODIndex) const override;

	virtual UStaticMesh* GetMesh() const override { return StaticMesh; }
	virtual const FMeshRenderState* GetRenderState() const override { return RenderState.get(); }
	virtual uint32 GetLODCount() const override { return RenderState ? RenderState->LODCount : 0u; }
	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const override;
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const override;

private:
	UStaticMesh* StaticMesh = nullptr;
	TSharedPtr<const FMeshRenderState> RenderState;
	bool bEditorIcon = false;
};

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(const FSkeletalMeshProxyData& InitialData, const FPrimitiveTransformData& InitialTransform);
	virtual ~FSkeletalMeshSceneProxy() override;

	void SetSkeletalMeshData(const FSkeletalMeshProxyData& Data);
	void SetSkinningData(uint8 InAppliedMode, FVertexBuffer* InCPUVertexBuffer, FStructuredBuffer* InMatricesBuffer, bool bInHeatmap);

	virtual void OnMeshRenderDataChanged() override;
	virtual void OnMeshDestroyed() override;
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext, uint32 LODIndex) const override;

	virtual USkeletalMesh* GetSkeletalMesh() const override { return SkeletalMesh; }
	virtual const FMeshRenderState* GetRenderState() const override { return RenderState.get(); }
	virtual uint32 GetLODCount() const override { return RenderState ? RenderState->LODCount : 0u; }
	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const override;
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const override;
	virtual bool IsComputeSkinned() const override;
	virtual FStructuredBuffer* GetSkinningMatricesBuffer() const override { return SkinningMatricesBuffer; }

private:
	USkeletalMesh* SkeletalMesh = nullptr;
	TSharedPtr<const FMeshRenderState> RenderState;
	uint8 AppliedMode = 0;
	FVertexBuffer* CPUSkinnedVertexBuffer = nullptr;
	FStructuredBuffer* SkinningMatricesBuffer = nullptr;
	bool bBoneWeightHeatmap = false;
};

class FBillboardSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FBillboardSceneProxy(const FBillboardProxyData& InitialData, const FPrimitiveTransformData& InitialTransform);
	virtual ~FBillboardSceneProxy() override = default;

	void SetBillboardData(const FBillboardProxyData& Data);

	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext, uint32 LODIndex) const override;

	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const override;
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const override;

private:
	UStaticMesh* QuadMesh = nullptr;
	UMaterial* Material = nullptr;
	FBillboardSubUVConstants IconParams{};
	float IconSize = 1.0f;
	bool bEditorIcon = false;
	bool bSubUVEmitter = false;
	bool bActive = true;
	bool bOpaqueMaterial = false;
	uint32 ColSize = 1;
	uint32 RowSize = 1;
	TArray<FBillboardSubUVParticleData> SubUVParticles;
	mutable TArray<FBillboardSubUVConstants> CachedSubUVConstants;
};
