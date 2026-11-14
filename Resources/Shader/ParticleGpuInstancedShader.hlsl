#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"

struct FGpuParticle
{
	float3 Position;
	float RelativeTime;
	float3 Velocity;
	float Lifetime;
	float3 BaseSize;
	float Rotation;
	float4 Color;
	float3 Size;
	float RotationRate;
	float4 BaseColor;
	float SubImageIndex;
	uint bAlive;
	float2 Pad;
};

cbuffer constants : register(b0)
{
	matrix VP;
};

cbuffer gpuDrawConstants : register(b1)
{
	matrix LocalToWorld;
	float4 CameraFacing;
	float4 CameraRight;
	float4 CameraUp;
	float4 WorldScaleAndLocal;
	float4 AtlasParams;
};

StructuredBuffer<FGpuParticle> GpuParticles : register(t1);
Texture2D AtlasTexture : register(t0);
SamplerState AtlasSampler : register(s0);

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
};

PS_INPUT mainVS(VS_INPUT input)
{
	PS_INPUT output;
	FGpuParticle p = GpuParticles[input.instanceId];
	if (p.bAlive == 0)
	{
		output.position = float4(0.0f, 0.0f, 0.0f, 0.0f);
		output.uv = float2(0.0f, 0.0f);
		output.tintAndAlpha = float4(0.0f, 0.0f, 0.0f, 0.0f);
		output.subUVParams = float4(0.0f, 1.0f, 1.0f, 0.0f);
		return output;
	}

	bool bLocal = WorldScaleAndLocal.z > 0.5f;
	float3 WorldPos = bLocal ? mul(float4(p.Position, 1.0f), LocalToWorld).xyz : p.Position;
	float Width = abs(p.Size.x * (bLocal ? WorldScaleAndLocal.x : 1.0f));
	float Height = abs(p.Size.y * (bLocal ? WorldScaleAndLocal.y : 1.0f));

	float C = cos(p.Rotation);
	float S = sin(p.Rotation);
	float3 RightAxis = (C * CameraRight.xyz + S * CameraUp.xyz) * Width;
	float3 UpAxis = (C * CameraUp.xyz - S * CameraRight.xyz) * Height;

	float3 VertexWorld = WorldPos
		+ input.position.x * CameraFacing.xyz
		+ input.position.y * RightAxis
		+ input.position.z * UpAxis;

	output.position = mul(float4(VertexWorld, 1.0f), VP);
	output.uv = input.uv;
	output.tintAndAlpha = p.Color;
	float Frame = (AtlasParams.w > 0.5f) ? p.SubImageIndex : 0.0f;
	output.subUVParams = float4(Frame, AtlasParams.x, AtlasParams.y, AtlasParams.z);
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

	float4 color = AtlasTexture.Sample(AtlasSampler, cellUV);
	color.a *= input.tintAndAlpha.a;
	if (decodeSRGB > 0.5f)
	{
		color.rgb = SRGBToLinear(color.rgb) * SRGBToLinear(input.tintAndAlpha.rgb);
	}
	else
	{
		color.rgb *= input.tintAndAlpha.rgb;
	}
	return color;
}
