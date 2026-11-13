#define GPU_SKINNING 1
#include "Skinning.hlsli"

struct FSourceVertex
{
    float3 Position;
    float3 Normal;
    float4 Tangent;
    float2 UV;
    float4 Color;
    int4 BoneIndices;
    float4 BoneWeights;
};

StructuredBuffer<FSourceVertex> SourceVertices : register(t0);
RWByteAddressBuffer SkinnedVertices : register(u0);

[numthreads(64, 1, 1)]
void mainCS(uint3 Id : SV_DispatchThreadID)
{
    // Large meshes use multiple rows of 32768 groups.
    uint Index = Id.x + Id.y * (32768 * 64);
    uint VertexCount, Stride;
    SourceVertices.GetDimensions(VertexCount, Stride);
    if (Index >= VertexCount) return;

    FSourceVertex Vertex = SourceVertices[Index];
    SkinVertex(Vertex.Position, Vertex.Normal, Vertex.Tangent, Vertex.BoneIndices, Vertex.BoneWeights);

    // Byte layout of FVertex in Render/Vertex.h.
    uint Offset = Index * 64;
    SkinnedVertices.Store3(Offset, asuint(Vertex.Position));
    SkinnedVertices.Store3(Offset + 12, asuint(Vertex.Normal));
    SkinnedVertices.Store4(Offset + 24, asuint(Vertex.Tangent));
    SkinnedVertices.Store2(Offset + 40, asuint(Vertex.UV));
    SkinnedVertices.Store4(Offset + 48, asuint(Vertex.Color));
}
