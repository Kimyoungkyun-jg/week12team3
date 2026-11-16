// Resources/Shader/ShadowDepth.hlsl
#pragma pack_matrix(row_major)
#include "Skinning.hlsli"

cbuffer ViewConstants : register(b0)
{
	matrix VP;
};

cbuffer ShadowDepthPass : register(b3)
{
	float4 ShadowParams;
	float3 LightDirection;
	float ShadowNear;
	uint bPerspective;
	float3 ShadowPadding;
	float3 PreShadowTranslation;
	float PreShadowPadding;
};

cbuffer DrawBatchConstants : register(b5)
{
	uint StartInstance;
	uint3 DrawBatchPad;
};

struct FPrimitiveSceneData
{
	matrix LocalToWorld;
	matrix NormalMatrix;
	float4 Flags;
};

StructuredBuffer<FPrimitiveSceneData> PrimitiveSceneData : register(t17);
StructuredBuffer<uint> InstancePrimitiveIds : register(t18);

struct VS_INPUT
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
#if GPU_SKINNING
	float4 Tangent : TANGENT;
	int4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
#endif
	uint InstanceId : SV_InstanceID;
};
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float ShadowDepth : TEXCOORD0;
    float DepthBias : TEXCOORD1;
};

// UE ComputeDepthBiasDirectionalSpot
float ComputeDepthBias(float3 WorldNormal)
{
    const float NoL = abs(dot(LightDirection, WorldNormal));
    const float MaxSlope = ShadowParams.z;
    const float Slope = clamp(NoL > 0 ? sqrt(saturate(1 - NoL * NoL)) / NoL : MaxSlope, 0, MaxSlope);
    return ShadowParams.x + ShadowParams.y * Slope;
}

VS_OUTPUT mainVS(VS_INPUT Input)
{
#if GPU_SKINNING
	SkinVertex(Input.Position, Input.Normal, Input.Tangent, Input.BoneIndices, Input.BoneWeights);
#endif
	uint PrimitiveId = InstancePrimitiveIds[Input.InstanceId + StartInstance];
	FPrimitiveSceneData Prim = PrimitiveSceneData[PrimitiveId];
	VS_OUTPUT Output;
	float4 WorldPosition = mul(float4(Input.Position, 1.0f), Prim.LocalToWorld);
	WorldPosition.xyz += PreShadowTranslation;
	const float3 WorldNormal = normalize(mul(float4(Input.Normal, 0.0f), Prim.NormalMatrix).xyz);
	Output.Position = mul(WorldPosition, VP);

    if (bPerspective)
    {
        // Week9 Perspective는 w = 라이트 공간 X. near~far를 0~1 선형으로 (UE TransformShadowSpot + PS 선형 깊이)
        Output.ShadowDepth = (Output.Position.w - ShadowNear) * ShadowParams.w;
    }
    else
    {
        // 근평면보다 광원 쪽에 있는 Caster를 0에 붙여서 잘리지 않게 함 (UE bClampToNearPlane)
        Output.Position.z = max(Output.Position.z, 0.0f);
        Output.ShadowDepth = Output.Position.z; // Ortho는 이미 선형
    }
    Output.DepthBias = ComputeDepthBias(WorldNormal);
    return Output;
}

// Week9 깊이는 0 = 가까움. bias를 더해 Caster를 광원에서 멀리 민다.
float mainPS(VS_OUTPUT Input) : SV_Depth
{
    return saturate(Input.ShadowDepth + Input.DepthBias);
}
