#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"

cbuffer constants : register(b0)
{
    matrix VP;
    matrix View;
    matrix Projection;
    float4 ViewTimeParams;
};

cbuffer particleMaterialConstants : register(b2)
{
	float4 MaterialBaseColor;
	float HasTexture;
	float IsAdditive;
	float2 MaterialPad;
};

Texture2D AtlasTexture : register(t0);
SamplerState ColorSampler : register(s0);

#ifdef MATERIAL_GRAPH
#include MATERIAL_GRAPH_INCLUDE
#endif

struct VS_INPUT
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float4 tangent : TANGENT;
	float2 uv : TEXCOORD0;
	float4 color : COLOR;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR;
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
};

PS_INPUT mainVS(VS_INPUT input)
{
	PS_INPUT output;
    output.position = mul(float4(input.position, 1.0f), VP);
    output.worldPosition = input.position;
    output.worldNormal = input.normal;
    output.uv = input.uv;
    output.color = input.color;
	return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
	float4 color = MaterialBaseColor;
	if (HasTexture > 0.5f)
	{
		color = AtlasTexture.Sample(ColorSampler, input.uv);
		color.rgb = SRGBToLinear(color.rgb);
		color *= MaterialBaseColor;
	}
	color.rgb *= SRGBToLinear(input.color.rgb);
	color.a *= input.color.a;
#ifdef MATERIAL_GRAPH
	float3 CameraWorldPosition = ViewTimeParams.yzw;
	FMaterialGraphOutput GraphOutput = EvaluateMaterialGraph(input.uv, input.worldPosition, normalize(CameraWorldPosition - input.worldPosition), normalize(input.worldNormal), input.color, ViewTimeParams.x);

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
	if (IsAdditive > 0.5f)
	{
		color.rgb *= color.a;
	}
	return color;
}
