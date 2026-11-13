#ifndef PROJECTED_DECAL_DEPTH_COMMON_HLSLI
#define PROJECTED_DECAL_DEPTH_COMMON_HLSLI

#pragma pack_matrix(row_major)

#include "ProjectedDecalCommon.hlsli"

cbuffer DecalDepthViewConstants : register(b5)
{
    matrix InverseViewProjection;
    float4 ViewportSize;
};

Texture2D<float> SceneDepthTexture : register(t1);

FDecalOutput EvaluateDepthProjectedDecal(float4 ScreenPosition)
{
    int2 Pixel = int2(ScreenPosition.xy);
    
    float Depth =
    SceneDepthTexture.Load(int3(Pixel, 0));
    
    // 현재 엔진의 깊이 Clear 값은 1.
    // 배경은 데칼을 받지 않는다.
    if (Depth >= 1.0f)
        return MakeEmptyDecalOutput();
    
    // SV_POSITION은 이미 픽셀 중심 좌표.
    // 여기서 0.5를 다시 더하지 않는다.
    float2 ScreenUV =
        ScreenPosition.xy * ViewportSize.zw;
    
    // 화면 좌표 -> D3D NDC.
    // 현재 엔진의 깊이는 [0, 1]이므로 z는 그대로 사용한다.
    float4 ClipPosition = float4(
        ScreenUV.x * 2.0f - 1.0f,
        1.0f - ScreenUV.y * 2.0f,
        Depth,
        1.0f);

    float4 WorldPositionH =
        mul(ClipPosition, InverseViewProjection);
    
    if (abs(WorldPositionH.w) < 1.0e-8f)
        return MakeEmptyDecalOutput();
    
    float3 WorldPosition = WorldPositionH.xyz / WorldPositionH.w;
    
    return EvaluateProjectedDecalNoDiscard(WorldPosition);
}

#endif // PROJECTED_DECAL_DEPTH_COMMON_HLSLI