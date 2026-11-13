#pragma pack_matrix(row_major)

// Reflection equation and shading frequency are orthogonal compile-time axes.
#if !defined(SHADING_MODEL_GOURAUD) && !defined(SHADING_MODEL_PHONG) && !defined(VIEW_MODE_UNLIT) && !defined(VIEW_MODE_WORLD_NORMAL)
    #define SHADING_MODEL_PHONG 1
#endif
#if !defined(REFLECTION_MODEL_PHONG) && !defined(REFLECTION_MODEL_BLINN_PHONG) && !defined(REFLECTION_MODEL_LAMBERT)
    #define REFLECTION_MODEL_LAMBERT 1
#endif
#ifndef REFLECTION_MODEL_LAMBERT
    #define REFLECTION_MODEL_LAMBERT 0
#endif
#if (SHADING_MODEL_GOURAUD + SHADING_MODEL_PHONG + VIEW_MODE_UNLIT + VIEW_MODE_WORLD_NORMAL) != 1
    #error Exactly one shading model or view mode must be enabled.
#endif
#if (REFLECTION_MODEL_PHONG + REFLECTION_MODEL_BLINN_PHONG + REFLECTION_MODEL_LAMBERT) != 1
    #error Exactly one reflection model must be enabled.
#endif

#include "LightData.hlsli"
#include "LightEvaluation.hlsli"
#include "LightGridCommon.hlsli"
#include "NormalMapCommon.hlsli"
#include "Skinning.hlsli"
#include "MaterialUV.hlsli"

struct FAmbientLightInfo
{
    float4 ColorIntensity; // rgb: 선형 색상, a: 광도
};

cbuffer PerObject : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    matrix NormalMatrix; // World 행렬의 역행렬을 전치한 법선 변환 행렬
    float4 ShadowFlags;
};

cbuffer Lighting : register(b1)
{
    FAmbientLightInfo Ambient;
    float4 CameraPosition;
    uint4 LightCounts; // x: 포인트 개수, y: 스포트 개수, z: 방향광 개수
    uint4 LightOffsets; // SceneLights에서 각 종류가 시작되는 위치, 순서는 위와 같음
    // 그림자는 광원마다 Light.ShadowMapChannel로 ScreenShadowMask(t13)에서 읽는다.
    float4 SkyColorIntensity; // rgb: Sky Light 색상, a: 세기 (0이면 비활성)
};

StructuredBuffer<FLightRenderParameters> SceneLights : register(t8);
StructuredBuffer<uint4> TileLightRanges : register(t9);
StructuredBuffer<uint> TilePointIndices : register(t10);
TextureCube<float4> SkyIrradiance : register(t11);
StructuredBuffer<uint> TileSpotIndices : register(t12);
// 언리얼 ScreenShadowMask: 그림자 광원마다 배정된 채널(R~A)에 Projection 결과가 들어 있다. 1 = 빛 받음.
Texture2D<float4> ScreenShadowMask : register(t13);

// 데칼
Texture2D<float4> DBufferLit : register(t14);
Texture2D<float4> DBufferUnlit : register(t15);
Texture2D<float4> DBufferNormal : register(t16);

cbuffer MaterialParams : register(b2)
{
    float4 BaseColor;
    float4 Emissive;
    float4 SurfaceParams; // x: 노멀맵 Y 반전, y: 예약, z: 컬러 텍스처 사용, w: 노멀맵 인코딩
    float4 UVTransform; // xy: 이동량, zw: 배율
    float4 SpecularColorShininess; // rgb: 정반사 색상, w: 광택 지수
    float4 WorldUVParams; // xy: tile size, z: local axes, w: opaque
};

Texture2D ColorTexture : register(t0);
Texture2D NormalTexture : register(t1);

SamplerState ColorSampler : register(s0);
SamplerState SkySampler : register(s1);

struct VS_IN
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 UV : TEXCOORD;
	float4 Color : COLOR;
#if GPU_SKINNING
	int4 BoneIndices : BLENDINDICES;
	float4 BoneWeights : BLENDWEIGHT;
#endif
};

struct VS_OUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
    float3 WorldNormal : TEXCOORD4;
    float4 WorldTangent : TEXCOORD5;
    float3 MaterialWorldPos : TEXCOORD6;
    float3 MaterialLocalPos : TEXCOORD7;
    float3 MaterialLocalNormal : TEXCOORD8;
#if SHADING_MODEL_GOURAUD
    float3 VertexDiffuse : TEXCOORD1;
    float3 VertexSpecular : TEXCOORD2;
#elif !VIEW_MODE_WORLD_NORMAL
    float3 WorldPosition : TEXCOORD1;
    float ViewDepth : TEXCOORD2;
#endif
};

struct FUberOutput
{
    float4 LitColor : SV_Target0;
#if !VIEW_MODE_WORLD_NORMAL
    float4 NormalRoughness : SV_Target1;
#endif
};

// 광원의 그림자 (1 = 빛 받음). 채널이 없거나 이 물체의 bReceiveShadow가 꺼져 있으면 1.
// Gouraud는 채널을 받지 않으므로 정점 셰이더에서 불려도 마스크를 읽지 않는다.
float GetLightShadow(FLightRenderParameters Light, float3 PixelPosition)
{
#if SHADING_MODEL_GOURAUD
    return 1.0f;
#else
    if (Light.ShadowMapChannel == INVALID_SHADOW_CHANNEL || ShadowFlags.x < 0.5f) return 1.0f;
    return ScreenShadowMask.Load(int3(int2(PixelPosition.xy), 0))[Light.ShadowMapChannel];
#endif
}

// 조명과 World Normal 보기가 같은 노멀맵 해석 및 TBN 변환을 사용한다.
float3 GetShadingNormal(VS_OUT Input, float3 ProjectedT, float3 ProjectedB)
{
    float LengthSquared = dot(Input.WorldNormal, Input.WorldNormal);
    float3 N = LengthSquared > 1.0e-8f
        ? Input.WorldNormal * rsqrt(LengthSquared)
        : float3(0.0f, 0.0f, 1.0f);
    if (SurfaceParams.w > 0.5f)
    {
        float3 MapNormal = DecodeMapNormal(NormalTexture.Sample(ColorSampler, Input.UV).xyz, SurfaceParams.w);
        if (SurfaceParams.x > 0.5f) MapNormal.y = -MapNormal.y;
        float3 T = Input.WorldTangent.xyz - N * dot(N, Input.WorldTangent.xyz);
        if (WorldUVParams.x > 0.0f) T = ProjectedT - N * dot(N, ProjectedT);
        float TangentLengthSquared = dot(T, T);
        if (TangentLengthSquared > 1.0e-8f)
        {
            T *= rsqrt(TangentLengthSquared);
            float3 B = cross(N, T) * Input.WorldTangent.w;
            if (WorldUVParams.x > 0.0f) B = normalize(ProjectedB);
            float3 MappedNormal = T * MapNormal.x + B * MapNormal.y + N * MapNormal.z;
            float MappedLengthSquared = dot(MappedNormal, MappedNormal);
            if (MappedLengthSquared > 1.0e-8f)
                N = MappedNormal * rsqrt(MappedLengthSquared);
        }
    }
    return N;
}

#if !VIEW_MODE_WORLD_NORMAL
float3 CalculateAmbientLight(FAmbientLightInfo Info)
{
    return Info.ColorIntensity.rgb * max(Info.ColorIntensity.a, 0.0f);
}

float3 CalculateSkyLight(float3 N)
{
    if (SkyColorIntensity.a <= 0.0f) return 0.0f;
    return SkyIrradiance.SampleLevel(SkySampler, N, 0).rgb *
        SkyColorIntensity.rgb * SkyColorIntensity.a;
}

void AddLight(inout FLightResult Result, float3 N, float3 V, float3 L,
    float3 Radiance, bool bIncludeSpecular)
{
    AccumulateLight(Result, N, V, L, Radiance, SpecularColorShininess.rgb,
        SpecularColorShininess.w, bIncludeSpecular);
}

void CalculateDirectionalLight(inout FLightResult Result, FLightRenderParameters Info, float3 N, float3 V, bool bIncludeSpecular)
{
    if (Info.Intensity <= 0.0f)
    {
        return;
    }
    
    float3 L = normalize(-Info.Direction);
    float3 Radiance = Info.Color * Info.Intensity;
    
    AddLight(Result, N, V, L, Radiance, bIncludeSpecular);
}

void CalculatePointLight(inout FLightResult Result, FLightRenderParameters Info,
    float3 P, float3 N, float3 V, bool bIncludeSpecular)
{
    float3 ToLight = Info.Position - P;
    float DistanceSquared = dot(ToLight, ToLight);
    float RadiusSquared = Info.Radius * Info.Radius;
    if (RadiusSquared <= 0.0f || DistanceSquared >= RadiusSquared || DistanceSquared < 1.0e-8f)
        return;
    float Distance = sqrt(max(DistanceSquared, 1.0e-6f));
    // Deferred와 동일하게 Point Light의 RadiusFallOff를 반경 감쇠 지수로 사용한다.
    float Fade = pow(saturate(1.0f - Distance / Info.Radius), max(Info.FalloffExponent, 0.0f));
    if (Fade <= 0.0f) return;
    float3 L = ToLight / Distance;
    float3 Radiance = Info.Color * max(Info.Intensity, 0.0f) * Fade;
    AddLight(Result, N, V, L, Radiance, bIncludeSpecular);
}

void CalculateSpotLight(inout FLightResult Result, FLightRenderParameters Info, float3 P, float3 N, float3 V, bool bIncludeSpecular)
{
    float3 ToLight = Info.Position - P;
    float DistanceSquared = dot(ToLight, ToLight);
    float RadiusFade = CalculateDistanceAttenuation(DistanceSquared, Info.Radius * Info.Radius);
    if (RadiusFade <= 0.0f) return;
    float3 L = ToLight / sqrt(max(DistanceSquared, 1.0e-6f));
	float ConeCos = dot(-L, normalize(Info.Direction));
	float ConeFade = CalculateConeAttenuation(ConeCos, Info.InnerConeCos, Info.OuterConeCos);
	ConeFade = pow(ConeFade, max(Info.FalloffExponent, 0.0001f));

	float3 Radiance = Info.Color * max(Info.Intensity, 0.0f) * ConeFade * RadiusFade;

    AddLight(Result, N, V, L, Radiance, bIncludeSpecular);
}

void CalculateRectLight(inout FLightResult Result, FLightRenderParameters Info,
	float3 P, float3 N, float3 V, bool bIncludeSpecular)
{
	float3 Position = Info.Position;
	float Radius = Info.Radius;
	float3 Direction = Info.Direction;
	float BarnDoorLength = Info.BarnDoorLength;
	float3 Color = Info.Color;
	float Intensity = Info.Intensity;
	float BarnDoorCos = Info.BarnDoorCos;
	float BarnDoorSin = Info.BarnDoorSin;
	float FalloffExponent = Info.FalloffExponent;
	float3 Right = Info.Right;
	float HalfWidth = Info.HalfWidth;
	float HalfHeight = Info.HalfHeight;

	float3 Up = cross(Direction, Right);
	float3 CenterToSurface = P - Position;

	float RectX = clamp(dot(CenterToSurface, Right), -HalfWidth, HalfWidth);
	float RectY = clamp(dot(CenterToSurface, Up), -HalfHeight, HalfHeight);
	float3 ClosestPoint = Position + Right * RectX + Up * RectY;

	float3 ToLight = ClosestPoint - P;
	float DistanceSquared = dot(ToLight, ToLight);
	float RadiusSquared = Radius * Radius;
	if (RadiusSquared <= 0.0f || DistanceSquared >= RadiusSquared || DistanceSquared < 1.0e-8f)
		return;

	float Distance = sqrt(max(DistanceSquared, 1.0e-6f));
	float3 L = ToLight / Distance;

	float Attenuation = pow(saturate(1.0f - Distance / max(Radius, 0.001f)), max(FalloffExponent, 0.001f));
	float FrontFace = saturate(dot(-L, Direction));
	if (FrontFace <= 0.0f || Attenuation <= 0.0f)
		return;

	float BarnDoorMask = 1.0f;
	if (BarnDoorLength > 0.0f)
	{
		float3 EmitterToSurface = P - ClosestPoint;
		float EmitterDist = max(length(EmitterToSurface), 0.0001f);
		float ForwardDist = dot(EmitterToSurface, Direction);
		float LateralX = abs(dot(EmitterToSurface, Right));
		float LateralY = abs(dot(EmitterToSurface, Up));
		float SlackX = (ForwardDist * BarnDoorSin - LateralX * BarnDoorCos) / EmitterDist;
		float SlackY = (ForwardDist * BarnDoorSin - LateralY * BarnDoorCos) / EmitterDist;
		BarnDoorMask = smoothstep(-0.05f, 0.0f, SlackX) * smoothstep(-0.05f, 0.0f, SlackY);
	}

	float3 Radiance = Color * max(Intensity, 0.0f) * Attenuation * FrontFace * BarnDoorMask;
	AddLight(Result, N, V, L, Radiance, bIncludeSpecular);
}

FLightResult CalculateBaseLighting(float3 PixelPosition, float3 P, float ViewDepth, float3 N, float3 V, bool bIncludeSpecular)
{
    FLightResult Result;
    Result.Diffuse = CalculateAmbientLight(Ambient) + CalculateSkyLight(N);
    Result.Specular = 0.0f;

    [loop]
    for (uint DirectionalIndex = 0; DirectionalIndex < LightCounts.z; ++DirectionalIndex)
    {
        FLightRenderParameters Light = SceneLights[LightOffsets.z + DirectionalIndex];
        
        float Shadow = GetLightShadow(Light, PixelPosition);
            
        FLightResult DirectionalResult = (FLightResult) 0;
        CalculateDirectionalLight(DirectionalResult, Light, N, V, bIncludeSpecular);
        DirectionalResult.Diffuse *= Shadow;
        DirectionalResult.Specular *= Shadow;
         
        Result.Diffuse += DirectionalResult.Diffuse;
        Result.Specular += DirectionalResult.Specular;
    }
    
    return Result;
}

void AddAllLocalLights(inout FLightResult Result, float3 PixelPosition, float3 P, float3 N,
    float3 V, bool bIncludeSpecular)
{
    [loop]
    for (uint PointIndex = 0; PointIndex < LightCounts.x; ++PointIndex)
    {
        FLightRenderParameters Light = SceneLights[LightOffsets.x + PointIndex];

        float Shadow = GetLightShadow(Light, PixelPosition);

        FLightResult PointResult = (FLightResult) 0;
        CalculatePointLight(PointResult, SceneLights[LightOffsets.x + PointIndex], P, N, V, bIncludeSpecular);
         
        Result.Diffuse += PointResult.Diffuse * Shadow;
        Result.Specular += PointResult.Specular * Shadow;
    }
    [loop]
    for (uint SpotIndex = 0; SpotIndex < LightCounts.y; ++SpotIndex)
    {
        FLightRenderParameters Light = SceneLights[LightOffsets.y + SpotIndex];
        
        float Shadow = GetLightShadow(Light, PixelPosition);
            
        FLightResult SpotResult = (FLightResult) 0;
        CalculateSpotLight(SpotResult, Light, P, N, V, bIncludeSpecular);

        Result.Diffuse += SpotResult.Diffuse * Shadow;
        Result.Specular += SpotResult.Specular * Shadow;
    }
	[loop]
	for (uint RectIndex = 0; RectIndex < LightCounts.w; ++RectIndex)
    {
		CalculateRectLight(Result, SceneLights[LightOffsets.w + RectIndex], P, N, V, bIncludeSpecular);
    }
}

FLightResult CalculateLighting(float3 PixelPosition, float3 P, float ViewDepth, float3 Normal, bool bIncludeSpecular)
{
    float3 N = normalize(Normal);
    float3 ToCamera = CameraPosition.xyz - P;
    float3 V = ToCamera * rsqrt(max(dot(ToCamera, ToCamera), 1.0e-8f));
    FLightResult Result = CalculateBaseLighting(PixelPosition, P, ViewDepth, N, V, bIncludeSpecular);
    AddAllLocalLights(Result, PixelPosition, P, N, V, bIncludeSpecular);
    return Result;
}

// 타일 목록의 인덱스는 kim의 공유 SceneLights 안에서 종류별 시작 위치에 더한다.
FLightResult CalculateTiledLighting(float3 PixelPosition, float3 P, float ViewDepth, float3 Normal, bool bIncludeSpecular)
{
    float3 N = normalize(Normal);
    float3 ToCamera = CameraPosition.xyz - P;
    float3 V = ToCamera * rsqrt(max(dot(ToCamera, ToCamera), 1.0e-8f));
    FLightResult Result = CalculateBaseLighting(PixelPosition, P, ViewDepth, N, V, bIncludeSpecular);

    if (GridMode == LIGHT_GRID_MODE_OFF)
    {
        // Off는 업로드된 전체 로컬 광원을 평가한다.
        AddAllLocalLights(Result, PixelPosition, P, N, V, bIncludeSpecular);
    }
    else
    {
        // SV_POSITION.xy는 픽셀 위치, z는 현재 표면의 DeviceZ다.
        uint CellIndex = GetLightGridCellIndex(uint2(PixelPosition.xy), PixelPosition.z);
        uint4 Range = TileLightRanges[CellIndex];
        bool UsePointList = Range.x != 0xffffffffu;
        bool UseSpotList = Range.z != 0xffffffffu;
        uint PointCount = UsePointList ? Range.y : LightCounts.x;
        uint SpotCount = UseSpotList ? Range.w : LightCounts.y;

        // 예약 실패한 종류만 전체 배열을 평가하는 기존 fallback을 유지한다.
        [loop]
        for (uint PointListIndex = 0; PointListIndex < PointCount; ++PointListIndex)
        {
            uint LightIndex = UsePointList ? TilePointIndices[Range.x + PointListIndex] : PointListIndex;
            
            FLightRenderParameters Light = SceneLights[LightOffsets.x + LightIndex];

            float Shadow = GetLightShadow(Light, PixelPosition);
            
            FLightResult PointResult = (FLightResult) 0;
            CalculatePointLight(PointResult, Light, P, N, V, bIncludeSpecular);
         
            Result.Diffuse += PointResult.Diffuse * Shadow;
            Result.Specular += PointResult.Specular * Shadow;
        }

        [loop]
        for (uint SpotListIndex = 0; SpotListIndex < SpotCount; ++SpotListIndex)
        {
            uint LightIndex = UseSpotList ? TileSpotIndices[Range.z + SpotListIndex] : SpotListIndex;
            
            FLightRenderParameters Light = SceneLights[LightOffsets.y + LightIndex];
            
            float Shadow = GetLightShadow(Light, PixelPosition);
            
            FLightResult SpotResult = (FLightResult) 0;
            CalculateSpotLight(SpotResult, Light, P, N, V, bIncludeSpecular);
            
            Result.Diffuse += SpotResult.Diffuse * Shadow;
            Result.Specular += SpotResult.Specular * Shadow;
        }

		[loop]
		for (uint RectIndex = 0; RectIndex < LightCounts.w; ++RectIndex)
		{
			CalculateRectLight(Result, SceneLights[LightOffsets.w + RectIndex], P, N, V, bIncludeSpecular);
		}
    }
    return Result;
}

#endif


VS_OUT Uber_VS(VS_IN Input)
{
#if GPU_SKINNING
    SkinVertex(Input.Position, Input.Normal, Input.Tangent, Input.BoneIndices, Input.BoneWeights);
#endif
    VS_OUT Output;
    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    float4 ViewPosition = mul(WorldPosition, View);
    Output.Position = mul(ViewPosition, Projection);
    Output.Color = Input.Color;
    Output.UV = Input.UV;
    Output.MaterialWorldPos = WorldPosition.xyz;
    Output.MaterialLocalPos = Input.Position;
    Output.MaterialLocalNormal = Input.Normal;

    float3 N = mul(float4(Input.Normal, 0.0f), NormalMatrix).xyz;
    Output.WorldNormal = N;
    float3 T = mul(float4(Input.Tangent.xyz, 0.0f), World).xyz;
    // CPU가 구한 로컬 공간 부호에 월드 변환의 거울 반전만 반영한다.
    float Handedness = Input.Tangent.w * (determinant((float3x3)World) < 0.0f ? -1.0f : 1.0f);
    Output.WorldTangent = float4(T, Handedness);
#if SHADING_MODEL_GOURAUD
    FLightResult Lit = CalculateLighting(float3(0.0f, 0.0f, 0.0f), WorldPosition.xyz, ViewPosition.x, N, !REFLECTION_MODEL_LAMBERT);
    Output.VertexDiffuse = Lit.Diffuse;
    Output.VertexSpecular = Lit.Specular;
#elif !VIEW_MODE_WORLD_NORMAL
    Output.WorldPosition = WorldPosition.xyz;
    Output.ViewDepth = ViewPosition.x;
#endif
    return Output;
}

FUberOutput Uber_PS(VS_OUT Input)
{
    FUberOutput Output;
    float3 ProjectedT, ProjectedB;
    Input.UV = ResolveMaterialUV(Input.UV, Input.MaterialWorldPos, Input.WorldNormal,
        Input.MaterialLocalPos, Input.MaterialLocalNormal, World,
        UVTransform.zw, UVTransform.xy, WorldUVParams.xy, WorldUVParams.z > 0.5f, ProjectedT, ProjectedB);
    float3 ShadingNormal = GetShadingNormal(Input, ProjectedT, ProjectedB);
#if VIEW_MODE_WORLD_NORMAL
    Output.LitColor = float4(ShadingNormal * 0.5f + 0.5f, 1.0f);
#else
    float4 SurfaceColor;
    if (SurfaceParams.z > 0.5f)
    {
        float4 TextureColor = ColorTexture.Sample(ColorSampler, Input.UV);
        SurfaceColor = float4(pow(max(TextureColor.rgb, 0.0f), 2.2f), TextureColor.a);
    }
    else
    {
        SurfaceColor = Input.Color;
    }
    float3 Albedo = SurfaceColor.rgb * BaseColor.rgb;
    
    int3 PixelCoord = int3(int2(Input.Position.xy), 0);
    
    bool bReceiveDecal = ShadowFlags.y > 0.5f;
    
    if (bReceiveDecal)
    {
        float4 LitDecal = DBufferLit.Load(PixelCoord);
        
        Albedo = Albedo * (1.0f - LitDecal.a) + LitDecal.rgb;
    }

#if SHADING_MODEL_PHONG
    if (bReceiveDecal)
    {
        float4 DecalNormal =
            DBufferNormal.Load(PixelCoord);
        
        float3 Combined =
            ShadingNormal * (1.0f - DecalNormal.a) +
            DecalNormal.rgb;
        
        float LengthSquared = dot(Combined, Combined);
        
        if (LengthSquared > 1.0e-8f)
        {
            ShadingNormal = Combined * rsqrt(LengthSquared);
        }

    }
#endif
      
        float3 Diffuse;
    float3 Specular = 0.0f;

#if SHADING_MODEL_GOURAUD
    Diffuse = Input.VertexDiffuse;
    Specular = Input.VertexSpecular;
#elif SHADING_MODEL_PHONG
    FLightResult Lit = CalculateTiledLighting(Input.Position.xyz, Input.WorldPosition, Input.ViewDepth, ShadingNormal, !REFLECTION_MODEL_LAMBERT);
    Diffuse = Lit.Diffuse;
    Specular = Lit.Specular;
#elif VIEW_MODE_UNLIT
    // 조명·그림자를 계산하지 않고 표면 색을 그대로 낸다.
    Diffuse = 1.0f;
#endif
    float3 FinalColor = Albedo * Diffuse + Specular + Emissive.rgb;
    float4 UnlitDecal = DBufferUnlit.Load(PixelCoord);

    if (bReceiveDecal)
    {
        FinalColor = FinalColor * (1.0f - UnlitDecal.a) + UnlitDecal.rgb;
    }

    Output.LitColor = float4(FinalColor, WorldUVParams.w > 0.5f ? 1.0f : SurfaceColor.a * BaseColor.a);
    Output.NormalRoughness = float4(ShadingNormal, 0.8f);
#endif
    return Output;
}

// 현재 리소스 매니저는 .hlsl 파일을 스캔할 때 mainVS/mainPS 진입점을 찾는다.
VS_OUT mainVS(VS_IN Input) { return Uber_VS(Input); }
FUberOutput mainPS(VS_OUT Input) { return Uber_PS(Input); }
