#pragma pack_matrix(row_major)

#include "ProjectedDecalDepthCommon.hlsli"

struct VS_OUT
{
    float4 Position : SV_Position;
};

// 버텍스 버퍼 없이 전체 화면 삼각형 생성.
// TODO: 데칼 볼륨의 버텍스 버퍼를 사용
VS_OUT mainVS(uint VertexID : SV_VertexID)
{
    VS_OUT Output;

    float2 UV =
        float2(
            (VertexID << 1) & 2,
            VertexID & 2);

    Output.Position = float4(
        UV * float2(2.0f, -2.0f) +
            float2(-1.0f, 1.0f),
        0.0f,
        1.0f);

    return Output;
}

[earlydepthstencil]
FDecalOutput mainPS(VS_OUT Input) : SV_Target0
{
    return EvaluateDepthProjectedDecal(Input.Position);
}