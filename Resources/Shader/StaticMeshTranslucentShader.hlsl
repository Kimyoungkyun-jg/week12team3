#pragma pack_matrix(row_major)
#include "Skinning.hlsli"
#include "MaterialUV.hlsli"
cbuffer Viewconstants : register(b0)
{
    matrix VP;
};

cbuffer Worldconstants : register(b2)
{
    matrix World;
};

cbuffer MaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVOffset;
    float2 Padding;
    float2 UVTiling;
    float2 WorldUVTileSize;
    float4 Emissive;
    float4 SpecularColorShininess;
    float4 WorldUVParams;
};

cbuffer TranslucentFogConstants : register(b3)
{
    float4x4 FogInverseViewProjection;
    float4 FogCameraPosition;
    float4 FogColor;
    float4 FogDensityHeight;
    float4 FogDistanceViewport;
};

struct VS_INPUT
{
	float3 p : POSITION;
	float3 n : NORMAL;
	float4 tangent : TANGENT;
	float2 t : TEXCOORD;
	float4 c : COLOR;
#if GPU_SKINNING
	int4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
#endif
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float3 localPosition : TEXCOORD2;
    float3 localNormal : TEXCOORD3;
};

Texture2D g_txColor : register(t0);
SamplerState g_Sample : register(s0);

float FogTransmittance(float3 Position)
{
    if (FogDensityHeight.x <= 0.0f || FogDistanceViewport.x <= 0.0f)
        return 1.0f;

    float3 Ray = Position - FogCameraPosition.xyz;
    float Distance = length(Ray);
    float Start = FogDensityHeight.w;
    if (Distance <= Start ||
        (FogDistanceViewport.y > 0.0f && Distance > FogDistanceViewport.y))
        return 1.0f;

    float Length = Distance - Start;
    float StartHeight = FogCameraPosition.z + Ray.z * (Start / Distance);
    float Falloff = FogDensityHeight.y;
    float A = abs(Falloff * (Position.z - StartHeight));
    float HeightExponent = -Falloff *
        (min(StartHeight, Position.z) - FogDensityHeight.z);
    float G;
    if (A < 0.01f)
        G = 1.0f - A * 0.5f + A * A / 6.0f;
    else
        G = (1.0f - exp(-A)) / A;
    float LogTau = log(FogDensityHeight.x) + log(Length) +
        HeightExponent + log(G);
    if (!isfinite(LogTau))
        return 1.0f;

    float T = LogTau >= 4.382026635f ? 0.0f
            : LogTau <= -80.0f ? 1.0f
            : exp(-exp(LogTau));
    return max(saturate(T), 1.0f - FogDistanceViewport.x);
}

PS_INPUT mainVS(VS_INPUT input)
{
#if GPU_SKINNING
    SkinVertex(input.p, input.n, input.tangent, input.BoneIndices, input.BoneWeights);
#endif
    PS_INPUT output;

    float4 WorldPosition = mul(float4(input.p, 1.0f), World);
    output.position = mul(WorldPosition, VP);
    output.color = input.c;
    output.uv = input.t;
    output.normal = mul(float4(input.n, 0.0f), World).xyz;
    output.localPosition = input.p;
    output.localNormal = input.n;
    output.worldPosition = WorldPosition.xyz;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float3 ProjectedT, ProjectedB;
    float2 UV = ResolveMaterialUV(input.uv, input.worldPosition, input.normal,
        input.localPosition, input.localNormal, World, UVTiling, UVOffset, WorldUVTileSize,
        WorldUVParams.x > 0.5f, ProjectedT, ProjectedB);
    float4 texColor = g_txColor.Sample(g_Sample, UV);
    texColor.rgb = pow(max(texColor.rgb, 0.0f), 2.2f) * BaseColor.rgb + Emissive.rgb;
    texColor.a *= BaseColor.a;
    texColor.a *= FogTransmittance(input.worldPosition);
    return texColor;
}
