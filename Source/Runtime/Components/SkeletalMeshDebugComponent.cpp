#include "EnginePCH.h"
#include "SkeletalMeshDebugComponent.h"

#include "Render/LineBatcher.h"
#include "Render/SkeletalMesh.h"

namespace
{
	constexpr int32 SphereSegments = 12;
	constexpr int32 TailLines = 4;
	constexpr float BoneLengthRadiusRatio = 0.2f;   // 구체 반지름 = 연결된 가장 짧은 뼈 길이 × 이 값
	constexpr float TailBaseCos = 0.70710678f;      // cos 45도
	const FVector4 BoneColor(1.0f, 0.85f, 0.2f, 1.0f);
	const FVector4 SelectedBoneColor(0.2f, 1.0f, 0.3f, 1.0f);
	const FVector4 HoveredBoneColor(1.0f, 1.0f, 1.0f, 1.0f);

	// 서로 수직인 원 3개로 구체를 표현한다
	void AddWireSphere(FLineBatcher& LineBatcher, const FVector& Center, const float Radius, const FVector4& Color)
	{
		const FVector Axes[3][2] = {
			{ FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f) },
			{ FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f) },
			{ FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f) },
		};

		for (const auto& Axis : Axes)
		{
			FVector Previous = Center + Axis[0] * Radius;
			for (int32 i = 1; i <= SphereSegments; i++)
			{
				const float Angle = 2.0f * PI * i / SphereSegments;
				const FVector Next = Center + (Axis[0] * std::cosf(Angle) + Axis[1] * std::sinf(Angle)) * Radius;
				LineBatcher.AddForegroundLine(Previous, Next, Color);
				Previous = Next;
			}
		}
	}

	// 부모 구체 표면에서 자식 관절로 모이는 뾰족한 선 묶음. 뼈의 방향이 보인다
	void AddBoneTail(FLineBatcher& LineBatcher, const FVector& ParentJoint, const FVector& ChildJoint, const float Radius, const FVector4& Color)
	{
		const FVector Offset = ChildJoint - ParentJoint;
		const float Length = Offset.Length();
		// 자식이 부모 구체 안에 있으면(Dummy 뼈) 표면에서 시작할 수 없고, 그릴 꼬리도 없다
		if (Length <= Radius)
			return;

		const FVector Direction = Offset * (1.0f / Length);
		const FVector UpGuide = std::abs(Direction.Z) > 0.99f ? FVector(0.f, 1.f, 0.f) : FVector(0.f, 0.f, 1.f);
		const FVector Right = FVector::Cross(Direction, UpGuide).GetSafeNormal();
		const FVector Up = FVector::Cross(Right, Direction).GetSafeNormal();

		// 뼈 방향에서 45도 기운 구체 표면 위의 점. 앞쪽 거리와 옆쪽 거리가 모두 반지름 × cos 45도라 중심에서 정확히 반지름만큼 떨어진다
		const float SurfaceOffset = Radius * TailBaseCos;
		const FVector BaseCenter = ParentJoint + Direction * SurfaceOffset;
		for (int32 i = 0; i < TailLines; i++)
		{
			const float Angle = 2.0f * PI * i / TailLines;
			const FVector BasePoint = BaseCenter + (Right * std::cosf(Angle) + Up * std::sinf(Angle)) * SurfaceOffset;
			LineBatcher.AddForegroundLine(BasePoint, ChildJoint, Color);
		}
	}
}

USkeletalMeshDebugComponent::USkeletalMeshDebugComponent()
{
}

USkeletalMeshDebugComponent::~USkeletalMeshDebugComponent()
{
}

void USkeletalMeshDebugComponent::SetSelectedBone(const int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex >= 0 && BoneIndex < GetBoneTransforms().Num() ? BoneIndex : -1;
}

void USkeletalMeshDebugComponent::SetSkeletalMesh(USkeletalMesh* Mesh)
{ SelectedBoneIndex = -1; HoveredBoneIndex = -1; Super::SetSkeletalMesh(Mesh); }

bool USkeletalMeshDebugComponent::ShouldDrawBone(const int32 Bone) const
{
	switch (BoneDrawMode)
	{
	case EBoneDrawMode::AllHierarchy:
		return true;
	case EBoneDrawMode::SelectedOnly:
		return Bone == SelectedBoneIndex;
	case EBoneDrawMode::SelectedAndParents:
		if (!SkeletalMesh) return false;
		for (int32 Current = SelectedBoneIndex; Current >= 0 && Current < SkeletalMesh->MeshData.Bones.Num(); Current = SkeletalMesh->MeshData.Bones[Current].ParentIndex)
			if (Current == Bone) return true;
		return false;
	default:
		return false;
	}
}

void USkeletalMeshDebugComponent::GetBoneSpheres(TArray<FVector>& OutJoints, TArray<float>& OutRadii) const
{
	OutJoints.Reset();
	OutRadii.Reset();
	if (!SkeletalMesh) return;

	const TArray<FBoneInfo>& Bones = SkeletalMesh->MeshData.Bones;
	if (GetGlobalBoneTransforms().Num() != Bones.Num()) return;

	// 뼈 기준 -> Component Space -> World (행 벡터라 왼쪽부터)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	OutJoints.Reserve(Bones.Num());
	for (const FMatrix& ComponentSpace : GetGlobalBoneTransforms())
	{
		OutJoints.Add((ComponentSpace * ComponentToWorld).TransformPosition(FVector(0.f, 0.f, 0.f)));
	}

	// 구체 크기를 부위에 맞춘다. 관절에 연결된 뼈 중 가장 짧은 길이가 그 부위의 크기를 대신한다
	const FBox& Bounds = SkeletalMesh->GetMeshData().AABB;
	const float MaxRadius = (Bounds.Max - Bounds.Min).Length() * BoneRadiusScale;
	OutRadii.Reserve(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); i++)
	{
		OutRadii.Add(MaxRadius);
	}
	for (int32 i = 0; i < Bones.Num(); i++)
	{
		const int32 Parent = Bones[i].ParentIndex;
		if (Parent < 0) continue;

		// 관절 자리를 맞추는 Dummy 뼈는 길이가 거의 0이라 부위 크기를 알려 주지 않는다
		const float Length = (OutJoints[i] - OutJoints[Parent]).Length();
		if (Length < MaxRadius) continue;

		const float FittedRadius = Length * BoneLengthRadiusRatio;
		OutRadii[i] = std::min(OutRadii[i], FittedRadius);
		OutRadii[Parent] = std::min(OutRadii[Parent], FittedRadius);
	}
}

void USkeletalMeshDebugComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!LineBatcher || !SkeletalMesh || !ShouldDrawDebug()) return;

	const TArray<FBoneInfo>& Bones = SkeletalMesh->MeshData.Bones;
	const TArray<FMatrix>& BoneTransforms = GetGlobalBoneTransforms();
	const FMatrix ComponentToWorld = GetWorldMatrix();
	if (bDrawBoneBounds && BoneTransforms.Num() == Bones.Num())
	{
		const FVector4 BoundsColor(0.2f, 0.8f, 1.0f, 1.0f);
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			if (const FBox* BindBounds = SkeletalMesh->GetBoneInfluenceBounds(Index))
			{
				const FMatrix SkinningMatrix = Bones[Index].InverseBindPose * BoneTransforms[Index];
				const FBox PoseBounds = BindBounds->GetWorldAABB(SkinningMatrix);
				LineBatcher->AddBox(PoseBounds.GetWorldAABB(ComponentToWorld),
					Index == SelectedBoneIndex ? SelectedBoneColor : BoundsColor);
			}
		}
	}
	if (!bDrawBones && !bDrawBoneAxes) return;

	TArray<FVector> Joints;
	TArray<float> Radii;
	GetBoneSpheres(Joints, Radii);
	if (Joints.Num() != Bones.Num()) return;

	// 뼈는 메시 안쪽에 있으므로 깊이 테스트 없이 항상 위에 그린다 (AddForegroundLine)
	for (int32 i = 0; i < Bones.Num(); i++)
	{
		if (!ShouldDrawBone(i)) continue;

		const FMatrix BoneToWorld = GetGlobalBoneTransforms()[i] * ComponentToWorld;
		const FVector& Joint = Joints[i];
		const FVector4& Color = i == SelectedBoneIndex ? SelectedBoneColor : i == HoveredBoneIndex ? HoveredBoneColor : BoneColor;

		if (bDrawBones)
		{
			AddWireSphere(*LineBatcher, Joint, Radii[i], Color);

			const int32 Parent = Bones[i].ParentIndex;
			if (Parent >= 0 && ShouldDrawBone(Parent))
			{
				AddBoneTail(*LineBatcher, Joints[Parent], Joint, Radii[Parent], Color);
			}
		}

		// 관절의 X/Y/Z 축 (빨강/초록/파랑). 뼈 행렬의 축 방향을 그대로 보여 준다
		if (bDrawBoneAxes)
		{
			const float L = BoneAxisLength;
			LineBatcher->AddForegroundLine(Joint, BoneToWorld.TransformPosition(FVector(L, 0.f, 0.f)), FVector4(1.f, 0.f, 0.f, 1.f));
			LineBatcher->AddForegroundLine(Joint, BoneToWorld.TransformPosition(FVector(0.f, L, 0.f)), FVector4(0.f, 1.f, 0.f, 1.f));
			LineBatcher->AddForegroundLine(Joint, BoneToWorld.TransformPosition(FVector(0.f, 0.f, L)), FVector4(0.f, 0.f, 1.f, 1.f));
		}
	}
}
