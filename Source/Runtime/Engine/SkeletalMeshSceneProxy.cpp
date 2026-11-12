#include "EnginePCH.h"
#include "SkeletalMeshSceneProxy.h"

#include "Components/SkinnedMeshComponent.h"
#include "Render/SkeletalMeshObject.h"

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent)
	:FPrimitiveSceneProxy(InComponent)
{
	bSkinned = true;
}

void FSkeletalMeshSceneProxy::UpdateRenderState()
{
	RenderState.reset();
	UpdateCommonFlags();

	USkinnedMeshComponent* Skinned = static_cast<USkinnedMeshComponent*>(GetComponent());
	USkeletalMesh* Asset = Skinned->GetSkeletalMesh();
	if (!Asset)return;

	TArray<UMaterial*> Materials;
	Materials.Reserve(Skinned->GetNumMaterials());
	for (int32 Slot = 0; Slot < Skinned->GetNumMaterials(); ++Slot)
		Materials.Add(Skinned->GetMaterial(Slot));
	RenderState = Asset->GetRenderState(Materials);
}

void FSkeletalMeshSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	USkinnedMeshComponent* Skinned = static_cast<USkinnedMeshComponent*>(GetComponent());
	FSkeletalMeshObject* Object = Skinned->GetMeshObject();
	if (!Object || !RenderState) return;

	FVertexBuffer* VB = Object->GetVertexBuffer(0);
	FIndexBuffer* IB = Object->GetIndexBuffer(0);
	if (!VB || !IB) return;

	const FVector Delta = GetBounds().Center - View.CameraPosition;
	const float DistanceSq = Delta.Dot(Delta);

	const FCachedMeshLOD& LOD = GetLOD(0);
	for (uint32 i = 0; i < LOD.NumSections; ++i)
	{
		const FCachedMeshSection& Section = GetSection(LOD.FirstSection + i);
		FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
		Packet.Proxy = this;
		Packet.VertexBuffer = VB;
		Packet.IndexBuffer = IB;
		Packet.BoneBuffer = Object->GetBoneMatrixBuffer();
		Packet.Material = Section.Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.CameraToParticleDistance = DistanceSq;
		Packet.BoneBuffer = Object->GetBoneMatrixBuffer();
		Packet.DebugWeightBone = Skinned->GetDebugWeightBone();
	}
}
