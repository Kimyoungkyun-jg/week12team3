#pragma pack_matrix(row_major)

#include "ProjectedDecalCommon.hlsli"

cbuffer ViewConstants : register(b0)
{
	matrix VP;
	matrix View;
	matrix Projection;
	float4 ViewTimeParams;
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

struct VS_IN
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 UV : TEXCOORD;
	float4 Color : COLOR;
	uint InstanceId : SV_InstanceID;
};

struct VS_OUT
{
	float4 Position : SV_POSITION;
	float3 WorldPosition : TEXCOORD1;
};

VS_OUT mainVS(VS_IN Input)
{
	uint PrimitiveId = InstancePrimitiveIds[Input.InstanceId + StartInstance];
	FPrimitiveSceneData Prim = PrimitiveSceneData[PrimitiveId];
	VS_OUT Output;

	float4 WorldPosition = mul(float4(Input.Position, 1.0f), Prim.LocalToWorld);
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