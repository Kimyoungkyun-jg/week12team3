#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Matrix.h"

class USkeletalMesh;
class USkeletalMeshComponent;

// TargetBoneToSourceBone is indexed by the human mesh's bones; -1 leaves a bone unmapped.
bool ApplyStaticRigPose(const USkeletalMesh& SourceRig, USkeletalMeshComponent& TargetMesh,
	const TArray<int32>& TargetBoneToSourceBone, const TArray<int32>& TargetAimChild,
	int32 SourceYawQuarterTurns, FString& OutError);

bool ApplyAnimatedRigPose(const USkeletalMesh& SourceRig, const TArray<FMatrix>& SourceGlobalPose,
	USkeletalMeshComponent& TargetMesh, const TArray<int32>& TargetBoneToSourceBone,
	const TArray<int32>& TargetAimChild, int32 SourceYawQuarterTurns, FString& OutError);
