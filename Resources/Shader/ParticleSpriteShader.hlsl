#pragma pack_matrix(row_major)

#include "ColorSpace.hlsli"

cbuffer constants : register(b0)
{
    matrix VP;
};

cbuffer SpriteWorldConstants : register(b2)
{
    matrix World;
};


cbuffer SpriteParams : register(b1)
{
    float CurrentFrame;
    float AtlasRowSize;
    float AtlasColSize;
    float ParticleAlpha;
    float4 Tint;
    float DecodeSRGB;
    float3 Padding;
};

Texture2D SpriteTexture : register(t0);
SamplerState ColorSampler : register(s0);

#ifdef MATERIAL_GRAPH
#include MATERIAL_GRAPH_INCLUDE
#endif

struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 particleColor : COLOR;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(mul(float4(input.position, 1.0f), World), VP);
    output.uv = input.uv;
    output.particleColor = input.color;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 color = SpriteTexture.Sample(ColorSampler, input.uv);

#ifdef MATERIAL_GRAPH
    FMaterialGraphOutput GraphOutput = EvaluateMaterialGraph(input.uv);

#ifdef MATERIAL_GRAPH_HAS_BASE_COLOR
    color *= GraphOutput.BaseColor;
#endif

    float3 GraphEmissive = float3(0, 0, 0);

#ifdef MATERIAL_GRAPH_HAS_EMISSIVE_COLOR
    GraphEmissive = GraphOutput.EmissiveColor;
#endif

#ifdef MATERIAL_GRAPH_HAS_EMISSIVE_FACTOR
    GraphEmissive *= GraphOutput.EmissiveFactor;
#endif

    color.rgb += GraphEmissive;
#endif
    if(DecodeSRGB > 0.5f)
    {
        color.rgb = SRGBToLinear(color.rgb) * SRGBToLinear(Tint.rgb) * input.particleColor.rgb;
    }
    else
    {
        color.rgb *= Tint.rgb * input.particleColor.rgb;
    }
    
    color.a *= ParticleAlpha * input.particleColor.a;
    return color;
}