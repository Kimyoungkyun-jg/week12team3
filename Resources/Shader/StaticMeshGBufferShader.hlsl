#pragma pack_matrix(row_major)
#include "MaterialUV.hlsli"

cbuffer ViewConstants : register(b0)
{
    matrix VP;
};

cbuffer WorldConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix;
    float4 ShadowFlags; // x: bReceiveShadow, y: bReceiveDecal
};

cbuffer MaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVOffset;
    float2 TextureFlags; // x: 노멀맵 인코딩, y: 노멀맵 Y 반전
    float2 UVTiling;
    float2 WorldUVTileSize;
    float4 Emissive;
    float4 SpecularColorShininess;
    float4 WorldUVParams;
};

struct VS_INPUT
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 UV : TEXCOORD;
	float4 Color : COLOR;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 WorldNormal : NORMAL;
    float4 WorldTangent : TANGENT;
    float2 UV : TEXCOORD0;
    nointerpolation float ReceiveShadow : TEXCOORD1; // 오브젝트 상수(b2)는 VS에만 묶이므로 VS가 넘긴다
    float3 WorldPos : TEXCOORD2;
    float3 LocalPos : TEXCOORD3;
    float3 LocalNormal : TEXCOORD4;
    nointerpolation float ReceiveDecal : TEXCOORD5;
};

struct GBUFFER_OUTPUT
{
    float4 BaseColorMetallic : SV_Target0;
    float4 NormalRoughness : SV_Target1;
    float4 EmissiveModel : SV_Target2;
    float4 Specular : SV_Target3;
};

Texture2D ColorTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState ColorSampler : register(s0);

// 데칼 DBuffer (UberLit.hlsl과 같은 슬롯). 데칼이 없는 뷰는 묶지 않으며, 비어 있는 슬롯은 0을 읽어 효과가 없다.
Texture2D<float4> DBufferLit : register(t14);
Texture2D<float4> DBufferUnlit : register(t15);
Texture2D<float4> DBufferNormal : register(t16);

float3 ResolveWorldNormal(float3 WorldNormal, float4 WorldTangent, float3 MapNormal)
{
    float3 N = normalize(WorldNormal);
    float3 T = WorldTangent.xyz - N * dot(N, WorldTangent.xyz);
    T *= rsqrt(max(dot(T, T), 1.0e-8f));
    float3 B = cross(N, T) * WorldTangent.w;
    return normalize(T * MapNormal.x + B * MapNormal.y + N * MapNormal.z);
}

float3 DecodeMapNormal(float3 Sample, float Encoding)
{
    if (Encoding > 1.5f)
    {
        float2 XY = Encoding > 2.5f ? Sample.xy : Sample.xy * 2.0f - 1.0f;
        return float3(XY, sqrt(saturate(1.0f - dot(XY, XY))));
    }
    return Sample * 2.0f - 1.0f;
}

PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;
    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);

    Output.Position = mul(WorldPosition, VP);
    Output.WorldNormal = mul(float4(Input.Normal, 0.0f), NormalMatrix).xyz;
    float3 T = mul(float4(Input.Tangent.xyz, 0.0f), World).xyz;

    // CPU가 구한 로컬 공간 부호에 월드 변환의 거울 반전만 반영한다.
    float Handedness = Input.Tangent.w * (determinant((float3x3)World) < 0.0f ? -1.0f : 1.0f);
    Output.WorldTangent = float4(T, Handedness);
    Output.UV = Input.UV;
    Output.WorldPos = WorldPosition.xyz;
    Output.LocalPos = Input.Position;
    Output.LocalNormal = Input.Normal;
    Output.ReceiveShadow = ShadowFlags.x;
    Output.ReceiveDecal = ShadowFlags.y;
    return Output;
}

GBUFFER_OUTPUT mainPS(PS_INPUT Input)
{
    GBUFFER_OUTPUT Output;

    float3 ProjectedT, ProjectedB;
    float2 UV = ResolveMaterialUV(Input.UV, Input.WorldPos, Input.WorldNormal,
        Input.LocalPos, Input.LocalNormal, World, UVTiling, UVOffset, WorldUVTileSize,
        WorldUVParams.x > 0.5f, ProjectedT, ProjectedB);
    float4 TextureColor = ColorTexture.Sample(ColorSampler, UV);
    // 색 텍스처는 UNORM으로 읽히므로 sRGB 값을 선형 조명 공간으로 변환한다.
    float3 LinearAlbedo = pow(max(TextureColor.rgb, 0.0f), 2.2f) * BaseColor.rgb;
    float3 Normal = normalize(Input.WorldNormal);
    if (TextureFlags.x > 0.5f)
    {
        float3 MapNormal = DecodeMapNormal(NormalTexture.Sample(ColorSampler, UV).xyz, TextureFlags.x);
        if (TextureFlags.y > 0.5f) MapNormal.y = -MapNormal.y;
        Normal = ResolveWorldNormal(Input.WorldNormal, Input.WorldTangent, MapNormal);
        if (WorldUVTileSize.x > 0.0f)
            Normal = normalize(ProjectedT * MapNormal.x + ProjectedB * MapNormal.y + normalize(Input.WorldNormal) * MapNormal.z);
    }

    // Lit 데칼: 조명 전에 표면 색과 노멀을 덮는다 (UberLit.hlsl과 같은 식).
    const bool bReceiveDecal = Input.ReceiveDecal > 0.5f;
    const int3 PixelCoord = int3(int2(Input.Position.xy), 0);
    if (bReceiveDecal)
    {
        float4 LitDecal = DBufferLit.Load(PixelCoord);
        LinearAlbedo = LinearAlbedo * (1.0f - LitDecal.a) + LitDecal.rgb;

        float4 DecalNormal = DBufferNormal.Load(PixelCoord);
        float3 Combined = Normal * (1.0f - DecalNormal.a) + DecalNormal.rgb;
        float LengthSquared = dot(Combined, Combined);
        if (LengthSquared > 1.0e-8f)
            Normal = Combined * rsqrt(LengthSquared);
    }

    // 현재 UMaterial에는 Metallic/Roughness 값이 없어 임시 상수를 쓴다.
    Output.BaseColorMetallic = float4(LinearAlbedo, 0.0f);
    // bReceiveShadow가 꺼진 물체는 w를 음수(-Roughness - 1)로 저장한다. Deferred 조명이 그림자 마스크를 무시한다.
    const float Roughness = 0.8f;
    Output.NormalRoughness = float4(Normal, Input.ReceiveShadow > 0.5f ? Roughness : -Roughness - 1.0f);
    Output.EmissiveModel = float4(Emissive.rgb, WorldUVParams.z > 0.5f ? WorldUVParams.w + 1.0f : 0.0f);
    Output.Specular = SpecularColorShininess;

    // Unlit 데칼: Forward는 조명 결과를 FinalColor * (1 - a) + rgb로 덮는다.
    // 조명은 Albedo·Specular에 선형이므로 둘을 (1 - a)배 하고 rgb를 Emissive로 더하면 같은 결과가 된다.
    if (bReceiveDecal)
    {
        float4 UnlitDecal = DBufferUnlit.Load(PixelCoord);
        const float Keep = 1.0f - UnlitDecal.a;
        Output.BaseColorMetallic.rgb *= Keep;
        Output.Specular.rgb *= Keep;
        Output.EmissiveModel.rgb = Output.EmissiveModel.rgb * Keep + UnlitDecal.rgb;
    }
    return Output;
}
