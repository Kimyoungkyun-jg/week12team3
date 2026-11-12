#pragma once
#include "Render/RealtimeGPUProfiler.h"

namespace GPUStats
{
    DECLARE_GPU_STAT("GPU: Shadow Depths", STAT_GPU_ShadowDepths);
    DECLARE_GPU_STAT("GPU: Depth Prepass", STAT_GPU_DepthPrepass);
    DECLARE_GPU_STAT("GPU: Forward Opaque", STAT_GPU_ForwardOpaque);
    DECLARE_GPU_STAT("GPU: Decal", STAT_GPU_Decal);
}