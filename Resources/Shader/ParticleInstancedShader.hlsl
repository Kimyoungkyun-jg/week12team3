#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"

cbuffer constants : register(b0)
{
    matrix VP;
    matrix View;
    matrix Projection;
    float4 ViewTimeParams;
};

cbuffer batchConstants : register(b1)
{
	uint StartInstance;
	uint3 BatchPad;
};

cbuffer particleMaterialConstants : register(b2)
{
	float4 MaterialBaseColor;
	float HasTexture;
	float3 MaterialPad;
};

struct FParticleInstanceData
{
	matrix World;
	float4 TintAndAlpha;
	float4 SubUVParams;
};

StructuredBuffer<FParticleInstanceData> ParticleInstances : register(t1);
Texture2D AtlasTexture : register(t0);
SamplerState ColorSampler : register(s0);

#ifdef MATERIAL_GRAPH
#include MATERIAL_GRAPH_INCLUDE
#endif

struct VS_INPUT
{
	float3 position : POSITION;
	float2 uv : TEXCOORD0;
	uint instanceId : SV_InstanceID;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 tintAndAlpha : TEXCOORD1;
	float4 subUVParams : TEXCOORD2;
    float3 worldPosition : TEXCOORD3;
    float3 worldNormal : TEXCOORD4;
};

PS_INPUT mainVS(VS_INPUT input)
{
	FParticleInstanceData inst = ParticleInstances[input.instanceId + StartInstance];
	PS_INPUT output;
    float4 WorldPosition = mul(float4(input.position, 1.0f), inst.World);
    output.position = mul(WorldPosition, VP);
    output.worldPosition = WorldPosition.xyz;
    output.worldNormal = normalize(mul(float4(1.0f, 0.0f, 0.0f, 0.0f), inst.World).xyz);
    output.uv = input.uv;
    output.tintAndAlpha = inst.TintAndAlpha;
    output.subUVParams = inst.SubUVParams;
	return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
	float currentFrame = input.subUVParams.x;
	float atlasRowSize = max(input.subUVParams.y, 1.0f);
	float atlasColSize = max(input.subUVParams.z, 1.0f);
	float decodeSRGB = input.subUVParams.w;

	float atlasCol = fmod(currentFrame, atlasColSize);
	float atlasRow = floor(currentFrame / atlasColSize);

	float2 cellSize = float2(1.0f / atlasColSize, 1.0f / atlasRowSize);
	float2 cellUV = input.uv * cellSize + float2(atlasCol, atlasRow) * cellSize;

	// 텍스처가 없으면 머티리얼의 리니어 기본 색상을 사용한다
	float4 color = MaterialBaseColor;
	if (HasTexture > 0.5f)
	{
		color = AtlasTexture.Sample(ColorSampler, cellUV);
		if (decodeSRGB > 0.5f)
		{
			color.rgb = SRGBToLinear(color.rgb);
		}
		color *= MaterialBaseColor;
	}
	float3 tintRGB = (decodeSRGB > 0.5f) ? SRGBToLinear(input.tintAndAlpha.rgb) : input.tintAndAlpha.rgb;
	color.rgb *= tintRGB;
	color.a *= input.tintAndAlpha.a;
#ifdef MATERIAL_GRAPH
	float3 CameraWorldPosition = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), inverse(View)).xyz;
	FMaterialGraphOutput GraphOutput = EvaluateMaterialGraph(cellUV, input.worldPosition, normalize(CameraWorldPosition - input.worldPosition), normalize(input.worldNormal), input.tintAndAlpha, ViewTimeParams.x);

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
	return color;
}
