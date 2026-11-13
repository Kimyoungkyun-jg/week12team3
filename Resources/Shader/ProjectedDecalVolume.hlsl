#include "ProjectedDecalDepthCommon.hlsli"

cbuffer DecalVolumeConstants : register(b6)
{
    matrix DecalToClip;
};

struct VS_IN
{
    float3 Position : POSITION;
};

struct VS_OUT
{
    float4 Position : SV_Position;
};

VS_OUT mainVS(VS_IN Input)
{
    VS_OUT Output;
    
    Output.Position =
        mul(float4(Input.Position, 1.0f), DecalToClip);
    
    return Output;
}

[earlydepthstencil]
FDecalOutput mainPS(VS_OUT Input) : SV_Target0
{
    return EvaluateDepthProjectedDecal(Input.Position);
}