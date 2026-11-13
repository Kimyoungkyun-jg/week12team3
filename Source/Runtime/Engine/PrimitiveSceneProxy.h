#pragma once

#include "Math/Frustum.h"
#include "Render/MeshRenderData.h"

class FScene;
class FSceneRenderer;
class UPrimitiveComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UBillboardComponent;
class UStaticMesh;
class USkeletalMesh;
class UMaterial;
class FVertexBuffer;
class FIndexBuffer;

class FPrimitiveSceneProxy
{
public:
	explicit FPrimitiveSceneProxy(UPrimitiveComponent* InComponent);
	virtual ~FPrimitiveSceneProxy();

	virtual void UpdateTransform();
	virtual void UpdateRenderState() = 0;
	virtual void OnMeshRenderDataChanged() {}
	virtual void OnMeshDestroyed() {}

	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const = 0;
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const = 0;

	virtual UStaticMesh* GetMesh() const { return nullptr; }
	virtual USkeletalMesh* GetSkeletalMesh() const { return nullptr; }

	UPrimitiveComponent* GetComponent() const { return Component; }
	const FAABB& GetBounds() const { return Bounds; }
	const FMatrix& GetLocalToWorld() const { return LocalToWorld; }
	const FMatrix& GetWorldToLocal() const { return WorldToLocal; }
	const FLODSphere& GetLODSphere() const { return LODSphere; }
	bool IsVisible() const { return bVisible; }
	bool IsShown(bool bGameView) const { return bVisible && !(bGameView && bHiddenInGame); }
	// 뷰 Show 플래그까지 반영한 표시 여부 (Skeletal Meshes를 끈 뷰는 스켈레탈 프록시를 빼고 수집한다)
	bool IsShown(bool bGameView, bool bShowSkeletalMeshes) const
	{
		return IsShown(bGameView) && (bShowSkeletalMeshes || !bSkeletalMesh);
	}
	bool IsSkeletalMesh() const { return bSkeletalMesh; }
	uint8 GetLODCount() const { return RenderState ? RenderState->LODCount : 1; }
	const FMeshRenderState* GetRenderState() const { return RenderState.get(); }
	const float* GetLODThresholdsSq() const { return RenderState->LODThresholdSq; }
	const FCachedMeshLOD& GetLOD(uint32 Index) const { return RenderState->LODs[Index]; }
	const FCachedMeshSection& GetSection(uint32 Index) const { return RenderState->Sections[Index]; }
	FScene* GetScene() const { return Scene; }

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	bool bCastsShadow = true;
	bool bReceiveShadow = true;
	bool bReceiveDecal = true;

protected:
	friend class FScene;
	friend class FSceneRenderer;
	FScene* Scene = nullptr;
	UPrimitiveComponent* Component = nullptr;
	TSharedPtr<const FMeshRenderState> RenderState;
	FMatrix LocalToWorld;
	FMatrix WorldToLocal;
	FAABB Bounds;
	FLODSphere LODSphere;
	int32 PackedIndex = INDEX_NONE;
	bool bQueuedForUpdate = false;
	bool bRenderStateQueued = false;
	bool bVisible = true;
	bool bHiddenInGame = false;
	bool bSkeletalMesh = false;   // FSkeletalMeshSceneProxy 생성자에서 켠다
};

class FStaticMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FStaticMeshSceneProxy(UStaticMeshComponent* InComponent);
	virtual ~FStaticMeshSceneProxy() override;

	virtual void UpdateTransform() override;
	virtual void UpdateRenderState() override;
	virtual void OnMeshRenderDataChanged() override;
	virtual void OnMeshDestroyed() override;

	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const override;
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const override;
	virtual UStaticMesh* GetMesh() const override { return Mesh; }

private:
	UStaticMesh* Mesh = nullptr;
};

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	virtual ~FSkeletalMeshSceneProxy() override;

	virtual void UpdateTransform() override;
	virtual void UpdateRenderState() override;
	virtual void OnMeshRenderDataChanged() override;
	virtual void OnMeshDestroyed() override;

	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const override;
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const override;
	virtual USkeletalMesh* GetSkeletalMesh() const override { return SkeletalMesh; }

private:
	USkeletalMesh* SkeletalMesh = nullptr;
};

class FBillboardSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FBillboardSceneProxy(UBillboardComponent* InComponent);
	virtual ~FBillboardSceneProxy() override = default;
	virtual void UpdateRenderState() override;
	virtual FVertexBuffer* GetVertexBuffer(uint8 LOD = 0) const override;
	virtual FIndexBuffer* GetIndexBuffer(uint8 LOD = 0) const override;
};