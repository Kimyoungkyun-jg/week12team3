#pragma pack_matrix(row_major)

// 언리얼 ShadowProjectionPixelShader: 그림자 한 장을 화면 마스크(ScreenShadowMask)의 한 채널에 쓴다. 1 = 빛을 받음.
// 깊이 아틀라스는 ShadowDepth.hlsl이 쓴 값이다. Directional은 Ortho 깊이, Spot·Point는 MinSubjectZ~MaxSubjectZ 선형 깊이이고 bias는 이미 들어 있다.

#define SHADOW_ATLAS_TEXEL (1.0f / 4096.0f) // ShadowRendering.h ShadowAtlasSize

cbuffer ShadowProjectionConstants : register(b0)
{
    // 위치는 모두 카메라 기준(Translated World)이다. 원점에서 먼 곳의 float 정밀도 문제를 피한다.
    matrix InvViewProjection;    // 화면 NDC + 깊이 -> 카메라 기준 위치
    matrix ShadowViewProjection; // 카메라 기준 위치 -> 그림자 클립
    float4 AtlasRect;            // xy = 타일 그리는 영역 시작 UV, zw = 크기 UV
    float4 ViewportSize;         // xy = 화면 크기, zw = 1 / 화면 크기
    float3 LightPosition;
    float  ShadowNear;
    float3 LightDirection;
    float  InvDepthRange;
    float3 ViewOrigin;
    float  FadeAlpha;
    float3 ViewForward;
    float  ShadowSharpen;        // Sharpen * 7 + 1
    float  SplitNear;
    float  SplitFar;
    float  FadePlaneOffset;
    float  InvFadePlaneLength;
    uint   AtlasIndex;
    uint   bPerspective;
    uint   bUseFadePlane;
    uint   PcfQuality;
    uint   FilterMethod;
    float  LightSourceSize;
    float  MaxSoftKernelTexels;
    float  WorldTexelScale;
};

Texture2D<float> SceneDepthTexture : register(t0);
Texture2DArray<float> ShadowDepthAtlas : register(t1);

SamplerState PointSampler : register(s2);
SamplerComparisonState ShadowSampler : register(s3); // LESS_EQUAL: 수신점 깊이 <= 저장 깊이면 1

#define POISSON_COUNT 16
static const float2 PoissonDisk[POISSON_COUNT] =
{
    float2(0.758417f, 0.604537f),
    float2(0.666262f, -0.501792f),
    float2(0.044159f, 0.940217f),
    float2(-0.618833f, 0.442616f),
    float2(0.276293f, 0.651964f),
    float2(-0.399537f, -0.290897f),
    float2(-0.104335f, -0.698866f),
    float2(0.260315f, -0.652012f),
    float2(0.129753f, 0.215359f),
    float2(0.333606f, -0.138314f),
    float2(0.484882f, 0.283357f),
    float2(-0.074304f, -0.153791f),
    float2(-0.255648f, 0.191645f),
    float2(-0.649947f, -0.709589f),
    float2(0.838194f, 0.199567f),
    float2(-0.354359f, 0.686538f)
};

float InterleavedGradientNoise(float2 PixelPosition)
{
    return frac(52.9829189f * frac(dot(PixelPosition, float2(0.06711056f, 0.00583715f))));
}

// 커널이 타일 밖(테두리, 옆 타일)을 읽지 않게 그리는 영역 안으로 붙인다.
float2 ClampToTile(float2 UV)
{
    const float2 HalfTexel = 0.5f * SHADOW_ATLAS_TEXEL;
    return clamp(UV, AtlasRect.xy + HalfTexel, AtlasRect.xy + AtlasRect.zw - HalfTexel);
}

float SampleShadow(float2 UV, float Receiver)
{
    return ShadowDepthAtlas.SampleCmpLevelZero(ShadowSampler, float3(ClampToTile(UV), AtlasIndex), Receiver).r;
}

float PoissonPCF(float2 UV, float Receiver, float RadiusTexels, float2 SvPosition, float RotationOffset)
{
    float S, C;
    sincos((InterleavedGradientNoise(SvPosition) + RotationOffset) * 6.28318530718f, S, C);
    float Sum = 0.0f;
    [loop]
    for (uint i = 0; i < POISSON_COUNT; ++i)
    {
        const float2 P = PoissonDisk[i];
        const float2 Rotated = float2(C * P.x - S * P.y, S * P.x + C * P.y);
        Sum += SampleShadow(UV + Rotated * RadiusTexels * SHADOW_ATLAS_TEXEL, Receiver);
    }
    return Sum / POISSON_COUNT;
}

float SampleShadowOffset(float2 BaseUV, float U, float V, float Receiver)
{
    return SampleShadow(BaseUV + float2(U, V) * SHADOW_ATLAS_TEXEL, Receiver);
}

// Castaño의 최적화 PCF (The Witness, "Shadow Mapping Summary"). 하드웨어 bilinear 비교 한 번이 2x2 텍셀을 섞는 것을 이용해
// FilterSize x FilterSize 텍셀 tent 필터를 (FilterSize / 2)^2 번의 SampleCmp로 계산한다.
// 픽셀마다 회전하는 Poisson과 달리 노이즈가 없어서 TAA 없이도 반그림자가 매끈하다.
float OptimizedPCF(float2 UV, float Receiver, uint FilterSize)
{
    const float2 TexelPos = UV / SHADOW_ATLAS_TEXEL;   // 1 = 텍셀 하나
    float2 BaseUV = floor(TexelPos + 0.5f);
    const float S = TexelPos.x + 0.5f - BaseUV.x;
    const float T = TexelPos.y + 0.5f - BaseUV.y;
    BaseUV = (BaseUV - 0.5f) * SHADOW_ATLAS_TEXEL;

    float Sum = 0.0f;
    float Result = 1.0f;
    [branch]
    if (FilterSize <= 3)
    {
        const float UW0 = 3.0f - 2.0f * S, UW1 = 1.0f + 2.0f * S;
        const float U0 = (2.0f - S) / UW0 - 1.0f, U1 = S / UW1 + 1.0f;
        const float VW0 = 3.0f - 2.0f * T, VW1 = 1.0f + 2.0f * T;
        const float V0 = (2.0f - T) / VW0 - 1.0f, V1 = T / VW1 + 1.0f;

        Sum += UW0 * VW0 * SampleShadowOffset(BaseUV, U0, V0, Receiver);
        Sum += UW1 * VW0 * SampleShadowOffset(BaseUV, U1, V0, Receiver);
        Sum += UW0 * VW1 * SampleShadowOffset(BaseUV, U0, V1, Receiver);
        Sum += UW1 * VW1 * SampleShadowOffset(BaseUV, U1, V1, Receiver);
        Result = Sum / 16.0f;
    }
    else if (FilterSize <= 5)
    {
        const float UW0 = 4.0f - 3.0f * S, UW1 = 7.0f, UW2 = 1.0f + 3.0f * S;
        const float U0 = (3.0f - 2.0f * S) / UW0 - 2.0f, U1 = (3.0f + S) / UW1, U2 = S / UW2 + 2.0f;
        const float VW0 = 4.0f - 3.0f * T, VW1 = 7.0f, VW2 = 1.0f + 3.0f * T;
        const float V0 = (3.0f - 2.0f * T) / VW0 - 2.0f, V1 = (3.0f + T) / VW1, V2 = T / VW2 + 2.0f;

        Sum += UW0 * VW0 * SampleShadowOffset(BaseUV, U0, V0, Receiver);
        Sum += UW1 * VW0 * SampleShadowOffset(BaseUV, U1, V0, Receiver);
        Sum += UW2 * VW0 * SampleShadowOffset(BaseUV, U2, V0, Receiver);
        Sum += UW0 * VW1 * SampleShadowOffset(BaseUV, U0, V1, Receiver);
        Sum += UW1 * VW1 * SampleShadowOffset(BaseUV, U1, V1, Receiver);
        Sum += UW2 * VW1 * SampleShadowOffset(BaseUV, U2, V1, Receiver);
        Sum += UW0 * VW2 * SampleShadowOffset(BaseUV, U0, V2, Receiver);
        Sum += UW1 * VW2 * SampleShadowOffset(BaseUV, U1, V2, Receiver);
        Sum += UW2 * VW2 * SampleShadowOffset(BaseUV, U2, V2, Receiver);
        Result = Sum / 144.0f;
    }
    else
    {
        // 7x7 (16 탭). 가중치 부호는 원본 그대로이며 곱하면 양수가 된다.
        const float UW0 = 5.0f * S - 6.0f, UW1 = 11.0f * S - 28.0f, UW2 = -(11.0f * S + 17.0f), UW3 = -(5.0f * S + 1.0f);
        const float U0 = (4.0f * S - 5.0f) / UW0 - 3.0f, U1 = (4.0f * S - 16.0f) / UW1 - 1.0f;
        const float U2 = -(7.0f * S + 5.0f) / UW2 + 1.0f, U3 = -S / UW3 + 3.0f;
        const float VW0 = 5.0f * T - 6.0f, VW1 = 11.0f * T - 28.0f, VW2 = -(11.0f * T + 17.0f), VW3 = -(5.0f * T + 1.0f);
        const float V0 = (4.0f * T - 5.0f) / VW0 - 3.0f, V1 = (4.0f * T - 16.0f) / VW1 - 1.0f;
        const float V2 = -(7.0f * T + 5.0f) / VW2 + 1.0f, V3 = -T / VW3 + 3.0f;

        const float UW[4] = { UW0, UW1, UW2, UW3 };
        const float US[4] = { U0, U1, U2, U3 };
        const float VW[4] = { VW0, VW1, VW2, VW3 };
        const float VS[4] = { V0, V1, V2, V3 };
        [unroll]
        for (int y = 0; y < 4; ++y)
        {
            [unroll]
            for (int x = 0; x < 4; ++x)
                Sum += UW[x] * VW[y] * SampleShadowOffset(BaseUV, US[x], VS[y], Receiver);
        }
        Result = Sum / 2704.0f;
    }
    return Result;
}

// r.ShadowQuality 단계 (1 하드웨어 1회, 2 3x3, 3 5x5, 4~5 7x7). 모든 픽셀이 같은 분기를 탄다.
float FilterPCF(float2 UV, float Receiver)
{
    float Shadow;
    [branch]
    if (PcfQuality <= 1)
        Shadow = SampleShadow(UV, Receiver);
    else
        Shadow = OptimizedPCF(UV, Receiver, PcfQuality == 2 ? 3u : (PcfQuality == 3 ? 5u : 7u));
    return Shadow;
}

// 정규화 깊이 -> 라이트 공간 거리. Directional은 깊이 범위 안의 거리, Spot은 광원에서의 거리.
float DepthToLightDistance(float Depth)
{
    return ShadowNear * bPerspective + Depth / InvDepthRange;
}

// 텍셀 하나의 월드 크기. Spot은 광원에서 멀수록 커진다.
float TexelWorldAt(float LightDistance)
{
    return bPerspective ? WorldTexelScale * LightDistance : WorldTexelScale;
}

// PCSS (Directional, Spot). 1) Blocker 평균 깊이 2) 반그림자 폭 3) 그 폭으로 PCF
float FilterPCSS(float2 UV, float Receiver, float2 SvPosition)
{
    const float ReceiverDistance = DepthToLightDistance(Receiver);

    // Blocker 탐색 반경: Directional은 광원 각도 x 깊이 범위, Spot은 광원 크기를 수신점에서 본 크기
    float SearchWorld = bPerspective
        ? LightSourceSize * (ReceiverDistance - ShadowNear) / max(ReceiverDistance, 1.0e-4f)
        : LightSourceSize * (ReceiverDistance);
    const float SearchTexels = clamp(SearchWorld / TexelWorldAt(bPerspective ? ShadowNear : ReceiverDistance), 1.0f, MaxSoftKernelTexels);

    // Blocker 탐색은 회전 없는 고정 패턴(+ 중심)으로 한다. 이웃 픽셀이 거의 같은 텍셀을 읽으므로
    // 평균 Blocker 깊이, 즉 반그림자 폭이 픽셀마다 튀지 않는다 (회전하면 폭 자체가 노이즈가 된다).
    const float CenterDepth = ShadowDepthAtlas.SampleLevel(PointSampler, float3(ClampToTile(UV), AtlasIndex), 0).r;
    float BlockerSum = CenterDepth < Receiver ? CenterDepth : 0.0f;
    float BlockerCount = CenterDepth < Receiver ? 1.0f : 0.0f;
    [loop]
    for (uint i = 0; i < POISSON_COUNT; ++i)
    {
        const float2 TapUV = ClampToTile(UV + PoissonDisk[i] * SearchTexels * SHADOW_ATLAS_TEXEL);
        const float Depth = ShadowDepthAtlas.SampleLevel(PointSampler, float3(TapUV, AtlasIndex), 0).r;
        if (Depth < Receiver)
        {
            BlockerSum += Depth;
            BlockerCount += 1.0f;
        }
    }
    if (BlockerCount <= 0.0f)
        return 1.0f;

    const float BlockerDistance = DepthToLightDistance(BlockerSum / BlockerCount);
    const float PenumbraWorld = bPerspective
        ? LightSourceSize * (ReceiverDistance - BlockerDistance) / max(BlockerDistance, 1.0e-4f)
        : LightSourceSize * (ReceiverDistance - BlockerDistance);
    const float FilterTexels = clamp(PenumbraWorld / TexelWorldAt(ReceiverDistance), 0.0f, MaxSoftKernelTexels);

    // 좁은 반그림자(접촉부)는 노이즈 없는 tent PCF로 선명하게. 넓어질수록 회전 Poisson 32탭으로 넘어간다.
    // 7x7 tent(반경 3.5텍셀)와 Poisson 사이는 섞어서 경계에 링이 생기지 않게 한다.
    const float PoissonBlend = saturate((FilterTexels - 3.5f) / 1.5f);
    float Tent = 1.0f;
    [branch]
    if (PoissonBlend < 1.0f)
        Tent = OptimizedPCF(UV, Receiver, FilterTexels <= 1.5f ? 3u : (FilterTexels <= 2.5f ? 5u : 7u));
    float Poisson = 1.0f;
    [branch]
    if (PoissonBlend > 0.0f)
    {
        const float Radius = max(FilterTexels, 3.5f);
        Poisson = 0.5f * (PoissonPCF(UV, Receiver, Radius, SvPosition, 0.0f) + PoissonPCF(UV, Receiver, Radius, SvPosition, 0.5f));
    }
    return lerp(Tent, Poisson, PoissonBlend);
}

struct FVSOutput
{
    float4 Position : SV_POSITION;
};

FVSOutput mainVS(uint VertexID : SV_VertexID)
{
    FVSOutput Output;
    const float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FVSOutput Input) : SV_Target
{
    const int2 Pixel = int2(Input.Position.xy);
    const float SceneDepth = SceneDepthTexture.Load(int3(Pixel, 0));
    if (SceneDepth >= 1.0f)
        discard; // 하늘

    const float2 ScreenUV = Input.Position.xy * ViewportSize.zw;
    const float2 NDC = float2(ScreenUV.x * 2.0f - 1.0f, 1.0f - ScreenUV.y * 2.0f);
    const float4 WorldH = mul(float4(NDC, SceneDepth, 1.0f), InvViewProjection);
    const float3 World = WorldH.xyz / WorldH.w;

    // Directional은 이 캐스케이드가 맡은 화면 깊이 구간만
    const float SceneW = dot(World - ViewOrigin, ViewForward);
    if (!bPerspective && (SceneW < SplitNear || SceneW > SplitFar))
        discard;

    const float4 ShadowClip = mul(float4(World, 1.0f), ShadowViewProjection);
    if (ShadowClip.w <= 0.0f)
        discard;
    const float3 ShadowNDC = ShadowClip.xyz / ShadowClip.w;
    const float2 TileUV = ShadowNDC.xy * float2(0.5f, -0.5f) + 0.5f;
    const float Receiver = bPerspective
        ? (dot(World - LightPosition, LightDirection) - ShadowNear) * InvDepthRange
        : ShadowNDC.z;
    // 이 그림자 프러스텀 밖. Point 6면은 서로 겹치지 않으므로 각 면이 자기 영역만 쓴다.
    if (any(TileUV < 0.0f) || any(TileUV > 1.0f) || Receiver < 0.0f || Receiver > 1.0f)
        discard;

    const float2 AtlasUV = AtlasRect.xy + TileUV * AtlasRect.zw;
    float Shadow = FilterMethod == 1 ? FilterPCSS(AtlasUV, Receiver, Input.Position.xy)
                                     : FilterPCF(AtlasUV, Receiver);

    // UE ShadowProjectionPixelShader.usf: Sharpen 후 FadeAlpha로 그림자 세기를 줄인다.
    Shadow = saturate((Shadow - 0.5f) * ShadowSharpen + 0.5f);
    Shadow = lerp(1.0f, Shadow, FadeAlpha);

    // 캐스케이드 끝부분(FadePlane ~ SplitFar)에서 알파를 0으로 낮춰 먼저 그린 먼 캐스케이드와 섞는다.
    const float BlendFactor = bUseFadePlane ? 1.0f - saturate((SceneW - FadePlaneOffset) * InvFadePlaneLength) : 1.0f;
    return float4(Shadow, Shadow, Shadow, BlendFactor);
}
