#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"

cbuffer constants : register(b0)
{
	matrix VP;
};

cbuffer batchConstants : register(b1)
{
	uint StartInstance;
	uint3 BatchPad;
};

struct FParticleInstanceData
{
	matrix World;
	float4 TintAndAlpha;
	float4 SubUVParams;
};

StructuredBuffer<FParticleInstanceData> ParticleInstances : register(t1);
Texture2D AtlasTexture : register(t0);
SamplerState AtlasSampler : register(s0);

struct VS_INPUT
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float4 tangent : TANGENT;
	float2 uv : TEXCOORD0;
	float4 color : COLOR;
	uint instanceId : SV_InstanceID;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float3 worldNormal : NORMAL;
	float2 uv : TEXCOORD0;
	float4 tintAndAlpha : TEXCOORD1;
	float4 extraParams : TEXCOORD2;
};

PS_INPUT mainVS(VS_INPUT input)
{
	FParticleInstanceData inst = ParticleInstances[input.instanceId + StartInstance];
	PS_INPUT output;
	output.position = mul(mul(float4(input.position, 1.0f), inst.World), VP);
	output.worldNormal = mul(input.normal, (float3x3)inst.World);
	output.uv = input.uv;
	output.tintAndAlpha = inst.TintAndAlpha * input.color;
	output.extraParams = inst.SubUVParams;
	return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
	float hasTexture = input.extraParams.x;
	float4 color = input.tintAndAlpha;
	if (hasTexture > 0.5f)
	{
		color *= AtlasTexture.Sample(AtlasSampler, input.uv);
	}
	float normalLen = length(input.worldNormal);
	if (normalLen > 0.0001f)
	{
		float3 n = input.worldNormal / normalLen;
		float3 lightDir = normalize(float3(0.35f, 0.45f, 0.8f));
		float shade = 0.75f + 0.25f * saturate(dot(n, lightDir) * 0.5f + 0.5f);
		color.rgb *= shade;
	}
	return color;
}
