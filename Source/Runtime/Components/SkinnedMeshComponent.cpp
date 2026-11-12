#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/SkeletalMeshObject.h"
#include "Engine/SkeletalMeshSceneProxy.h"
#include "Engine/Scene.h"
#include "Collision/Ray.h"
#include <UObject/UObjectIterator.h>

#include "Render/SkinningStats.h"

#include <cfloat>

namespace
{
	TUniquePtr<FSkeletalMeshObject> MakeMeshObject(USkeletalMesh* Mesh)
	{
		if (USkinnedMeshComponent::bUseGPUSkinning)
		{
			TUniquePtr<FSkeletalMeshObjectGPUSkin> GPUObject = MakeUnique<FSkeletalMeshObjectGPUSkin>(Mesh);
			if (GPUObject->IsValid())
				return GPUObject;
			HTR_LOG(Warning, "GPU skinning unavailable; falling back to CPU skinning.");
		}
		return MakeUnique<FSkeletalMeshObjectCPUSkin>(Mesh);
	}
}

USkinnedMeshComponent::USkinnedMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (bBoneTransformsDirty || bForceSkinUpdate)
		UpdateBoneTransformsNow();
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	if (SkeletalMesh == InMesh) return;

	SkeletalMesh = InMesh;
	
	ClearOverrideMaterials();
	RecreateMeshObject();
}

FPrimitiveSceneProxy* USkinnedMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

FBox USkinnedMeshComponent::CalcLocalBounds() const
{
	return MeshObject ? MeshObject->GetBounds() : Super::CalcLocalBounds();
}

int32 USkinnedMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? static_cast<int32>(SkeletalMesh->GetMaterialSlots().Num()) : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!SkeletalMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
		return FString();
	return SkeletalMesh->GetMaterialSlots()[SlotIndex].Name;
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SkeletalMesh ? SkeletalMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

bool USkinnedMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const TArray<FVertex>* Vertices = MeshObject ? MeshObject->GetCPUSkinnedVertices() : nullptr;
	if (!Vertices || !SkeletalMesh) return false;

	const TArray<uint32>& Indices = SkeletalMesh->GetRenderData().LODRenderData[0].Indices;
	bool bHit = false;
	for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
	{
		float T = FLT_MAX;
		if (RayIntersectsTriangle(LocalRay, (*Vertices)[Indices[I]].Position,
			(*Vertices)[Indices[I + 1]].Position, (*Vertices)[Indices[I + 2]].Position, T) && T < OutT)
		{
			OutT = T;
			bHit = true;
		}
	}
	return bHit;
}

void USkinnedMeshComponent::UpdateBoneTransformsNow()
{
	bBoneTransformsDirty = false;
	if (!SkeletalMesh || !MeshObject) return;
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_BoneTransforms);
		RefreshBoneTransforms();      
	}
	SendRenderDynamicData();
}

void USkinnedMeshComponent::SetGPUSkinningEnabled(bool bEnable)
{
	if (bUseGPUSkinning == bEnable) return;
	bUseGPUSkinning = bEnable;
	for (TObjectIterator<USkinnedMeshComponent> It; It; ++It)
	{
		It->RefreshSkinningMode();
	}
	FStatRegistry::Reset();
}

void USkinnedMeshComponent::SetDebugWeightBone(int32 BoneIndex)
{
	if (DebugWeightBone == BoneIndex) return;
	DebugWeightBone = BoneIndex;
	if (MeshObject)
		MeshObject->SetDebugWeightBone(BoneIndex);
	// CPU 스키닝은 다시 돌아야 정점 색이 바뀐다. 틱이 없는 월드에서도 바로 보이도록 즉시 갱신한다
	UpdateBoneTransformsNow();
}

void USkinnedMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetRefSkeleton();
	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = Bones[i].LocalBindPose.GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkinnedMeshComponent::SendRenderDynamicData()
{
	MeshObject->Update(ComponentSpaceTransforms);
	if (MeshObject->UpdateBounds())
		OnTransformDirty();
	else if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		if (Proxy->GetScene() && Proxy->bCastsShadow)
		{
			Proxy->GetScene()->InvalidateShadowsInBounds(Proxy->GetBounds());
		}
	}
}

void USkinnedMeshComponent::RecreateMeshObject()
{
	const bool bValid = SkeletalMesh && !SkeletalMesh->GetRenderData().LODRenderData.IsEmpty();
	MeshObject = bValid ? MakeMeshObject(SkeletalMesh) : nullptr;
	if (MeshObject) MeshObject->SetDebugWeightBone(DebugWeightBone);
	ComponentSpaceTransforms.Reset();
	OnSkeletalMeshChanged();
	
	MarkRenderStateDirty();
	OnTransformDirty();
	bBoneTransformsDirty = true;
}

void USkinnedMeshComponent::RefreshSkinningMode()
{
	if (!MeshObject) return;
	MeshObject = MakeMeshObject(SkeletalMesh);
	if (MeshObject) MeshObject->SetDebugWeightBone(DebugWeightBone);
	UpdateBoneTransformsNow();   // 포즈는 그대로 두고 새 MeshObject에 다시 올린다
}

void USkinnedMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	if (Record.GetUnderlyingArchive().IsLoading())
		RecreateMeshObject();
}


