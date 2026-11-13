#ifndef HITORI_MATERIAL_UV
#define HITORI_MATERIAL_UV
// TEAM5 world-size projection, shared by Forward, GBuffer and translucency.
float2 ResolveMaterialUV(float2 MeshUV, float3 WorldPos, float3 WorldNormal,
    float3 LocalPos, float3 LocalNormal, float4x4 ObjectWorld,
    float2 Tiling, float2 Offset, float2 TileSize, bool LocalSpace,
    out float3 ProjectedT, out float3 ProjectedB)
{
    ProjectedT = ProjectedB = 0.0f;
    if (TileSize.x <= 0.0f) return MeshUV * Tiling + Offset;
    TileSize.y = TileSize.y > 0.0f ? TileSize.y : TileSize.x;
    float3 Scale = float3(length(ObjectWorld[0].xyz), length(ObjectWorld[1].xyz), length(ObjectWorld[2].xyz));
    float3 P = LocalSpace ? LocalPos * Scale : WorldPos;
    float3 Weight = abs(LocalSpace ? LocalNormal : WorldNormal);
    float3 X = LocalSpace ? ObjectWorld[0].xyz / max(Scale.x, 1e-6f) : float3(1,0,0);
    float3 Y = LocalSpace ? ObjectWorld[1].xyz / max(Scale.y, 1e-6f) : float3(0,1,0);
    float3 Z = LocalSpace ? ObjectWorld[2].xyz / max(Scale.z, 1e-6f) : float3(0,0,1);
    if (Weight.z >= Weight.x && Weight.z >= Weight.y)
    {
        ProjectedT = Y; ProjectedB = X;
        return float2(P.y, P.x) / TileSize + Offset;
    }
    if (Weight.x >= Weight.y)
    {
        ProjectedT = Y; ProjectedB = -Z;
        return float2(P.y, -P.z) / TileSize + Offset;
    }
    ProjectedT = X; ProjectedB = -Z;
    return float2(P.x, -P.z) / TileSize + Offset;
}
#endif
