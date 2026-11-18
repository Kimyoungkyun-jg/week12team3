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
	float3 BaseVelocity;
	float BaseRotationRate;
	float SubImageIndex;
	uint bAlive;
	uint CollisionCount;
	float Pad;
	float4 MeshRotation;
	float3 AngularVelocity;
	float SimPad;
};

cbuffer constants : register(b0)
{
	matrix VP;
	matrix View;
	matrix Projection;
	float4 ViewTimeParams;
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

cbuffer particleMaterialConstants : register(b2)
{
	float4 MaterialBaseColor;
	float HasTexture;
	float3 MaterialPad;
};

StructuredBuffer<FGpuParticle> GpuParticles : register(t1);
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
	uint instanceId : SV_InstanceID;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float3 worldNormal : NORMAL;
	float2 uv : TEXCOORD0;
	float4 tintAndAlpha : TEXCOORD1;
	float3 worldPosition : TEXCOORD2;
};

float3 RotateVectorByQuat(float3 V, float4 Q)
{
	float3 T = 2.0f * cross(Q.xyz, V);
	return V + Q.w * T + cross(Q.xyz, T);
}

PS_INPUT mainVS(VS_INPUT input)
{
	PS_INPUT output;
	FGpuParticle p = GpuParticles[input.instanceId];
	if (p.bAlive == 0)
	{
		output.position = float4(0.0f, 0.0f, 0.0f, 0.0f);
		output.worldNormal = float3(0.0f, 0.0f, 1.0f);
		output.uv = float2(0.0f, 0.0f);
		output.tintAndAlpha = float4(0.0f, 0.0f, 0.0f, 0.0f);
		output.worldPosition = float3(0.0f, 0.0f, 0.0f);
		return output;
	}

	float3 Scaled = input.position * p.Size;
	float4 Q = length(p.MeshRotation) > 1e-5f ? normalize(p.MeshRotation) : float4(0.0f, 0.0f, 0.0f, 1.0f);
	float3 RotatedPos = RotateVectorByQuat(Scaled, Q);
	float3 RotatedNormal = RotateVectorByQuat(input.normal, Q);

	bool bLocal = WorldScaleAndLocal.z > 0.5f;
	float3 LocalPos = RotatedPos + p.Position;
	float3 WorldPos = bLocal ? mul(float4(LocalPos, 1.0f), LocalToWorld).xyz : LocalPos;
	float3 WorldNormal = bLocal ? mul(RotatedNormal, (float3x3) LocalToWorld) : RotatedNormal;

	output.position = mul(float4(WorldPos, 1.0f), VP);
	output.worldPosition = WorldPos;
	output.worldNormal = WorldNormal;
	output.uv = input.uv;
	// 기본 큐브 정점 색이 파티클 색상을 덮지 않도록 파티클 컬러만 전달
	output.tintAndAlpha = p.Color;
	return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
	float4 color = MaterialBaseColor;
	if (HasTexture > 0.5f)
	{
		float4 tex = AtlasTexture.Sample(ColorSampler, input.uv);
		tex.rgb = SRGBToLinear(tex.rgb);
		color *= tex;
	}
	color *= input.tintAndAlpha;

#ifdef MATERIAL_GRAPH
	float3 CameraWorldPosition = ViewTimeParams.yzw;
	FMaterialGraphOutput GraphOutput = EvaluateMaterialGraph(input.uv, input.worldPosition, normalize(CameraWorldPosition - input.worldPosition), normalize(input.worldNormal), input.tintAndAlpha, ViewTimeParams.x);

#ifdef MATERIAL_GRAPH_HAS_OPACITY_MASK
	clip(GraphOutput.OpacityMask - 0.001f);
#endif

#ifdef MATERIAL_GRAPH_HAS_BASE_COLOR
	color *= GraphOutput.BaseColor;
#endif
#endif

	float normalLen = length(input.worldNormal);
	if (normalLen > 0.0001f)
	{
		float3 n = input.worldNormal / normalLen;
		float3 lightDir = normalize(float3(0.35f, 0.45f, 0.8f));
		float shade = 0.75f + 0.25f * saturate(dot(n, lightDir) * 0.5f + 0.5f);
		color.rgb *= shade;
	}

#ifdef MATERIAL_GRAPH
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
