#pragma once
#include "Core/Stats/LightweightStats.h"

namespace SkinningStats
{
    DECLARE_CYCLE_STAT("Skin: Bone Transforms", STAT_BoneTransforms);
    DECLARE_CYCLE_STAT("Skin: CPU Skinning", STAT_CpuSkinning);
    DECLARE_CYCLE_STAT("Skin: CPU Skinning (Picking)", STAT_CpuSkinningPicking);
    DECLARE_CYCLE_STAT("Skin: Vertex Upload", STAT_SkinUpload);
    DECLARE_CYCLE_STAT("Skin: Bone Upload", STAT_BoneUpload);
}
