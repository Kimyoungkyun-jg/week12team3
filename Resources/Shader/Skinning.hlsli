#ifndef SKINNING_HLSLI
#define SKINNING_HLSLI

#if GPU_SKINNING
struct FSkinningMatrix
{
    row_major float4x4 Matrix;
};

StructuredBuffer<FSkinningMatrix> SkinningMatrices : register(t16);

void SkinVertex(inout float3 Position, inout float3 Normal, inout float4 Tangent,
    int4 BoneIndices, float4 BoneWeights)
{
    uint BoneCount, MatrixStride;
    SkinningMatrices.GetDimensions(BoneCount, MatrixStride);
    float3 SkinnedPosition = 0.0f;
    float3 SkinnedNormal = 0.0f;
    float3 SkinnedTangent = 0.0f;
    float TotalWeight = 0.0f;

    [unroll]
    for (uint Influence = 0; Influence < 4; ++Influence)
    {
        const int BoneIndex = BoneIndices[Influence];
        const float Weight = BoneWeights[Influence];
        if (BoneIndex < 0 || BoneIndex >= (int)BoneCount || Weight <= 0.0f) continue;
        const float4x4 Matrix = SkinningMatrices[BoneIndex].Matrix;
        SkinnedPosition += mul(float4(Position, 1.0f), Matrix).xyz * Weight;
        SkinnedNormal += mul(float4(Normal, 0.0f), Matrix).xyz * Weight;
        SkinnedTangent += mul(float4(Tangent.xyz, 0.0f), Matrix).xyz * Weight;
        TotalWeight += Weight;
    }

    if (TotalWeight > 0.0f) Position = SkinnedPosition;
    const float3 ResultNormal = TotalWeight > 0.0f ? SkinnedNormal : Normal;
    const float3 ResultTangent = TotalWeight > 0.0f ? SkinnedTangent : Tangent.xyz;
    const float NormalLengthSquared = dot(ResultNormal, ResultNormal);
    const float TangentLengthSquared = dot(ResultTangent, ResultTangent);
    Normal = NormalLengthSquared < 1.0e-8f ? 0.0f : ResultNormal * rsqrt(NormalLengthSquared);
    Tangent.xyz = TangentLengthSquared < 1.0e-8f ? 0.0f : ResultTangent * rsqrt(TangentLengthSquared);
}
#endif

#endif
