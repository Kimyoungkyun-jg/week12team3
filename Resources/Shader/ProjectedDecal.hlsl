#pragma pack_matrix(row_major)

#include "ProjectedDecalCommon.hlsli"

cbuffer PerObject : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    matrix NormalMatrix;
    float4 ShadowFlags;
}

struct VS_IN
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

struct VS_OUT
{
    float4 Position : SV_POSITION;
    
    float3 WorldPosition : TEXCOORD1;
};

// 실제 데칼 패스에서는 기존 lambert vs를 재사용한다.
VS_OUT mainVS(VS_IN Input)
{
    VS_OUT Output;
    
    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    
    float4 ViewPosition = mul(WorldPosition, View);
    
    Output.Position = mul(ViewPosition, Projection);
    
    Output.WorldPosition = WorldPosition.xyz;
    
    return Output;
}

[earlydepthstencil]
FDecalOutput mainPS(VS_OUT Input) : SV_Target0
{
    return EvaluateProjectedDecal(Input.WorldPosition);
}