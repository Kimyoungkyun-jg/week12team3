#pragma pack_matrix(row_major)

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
	float2 Pad;
};

struct FGpuDistributionDesc
{
	float4 AtlasUVAndMode;
	float4 ValueScale;
	float4 ValueBias;
};

cbuffer SimParams : register(b0)
{
	float DeltaTime;
	uint MaxParticles;
	uint SpawnCount;
	uint WriteCursor;

	FGpuDistributionDesc SizeScale;
	FGpuDistributionDesc ColorOverLife;
	FGpuDistributionDesc SubUV;
	FGpuDistributionDesc VelocityScale;
	FGpuDistributionDesc RotationRateScale;

	float SubUVCols;
	float SubUVRows;
	float SubUVPlayRate;
	uint SubUVLinear;

	uint HasColorOverride;
	float3 Pad;
	float4 ColorOverride;
	
    uint HasCollision;
    float CollisionRestitution;
    float CollisionFriction;
    uint CollisionMaxCount;

    float CollisionRadius;
    float DepthThickness;
    float2 CollisionPad;
	
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float4 ScreenAndClip;
};

StructuredBuffer<FGpuParticle> SpawnBuffer : register(t0);
Texture2D CurveAtlas : register(t1);
Texture2D <float> SceneDepth : register(t2);
SamplerState CurveSampler : register(s0);
RWStructuredBuffer<FGpuParticle> ParticleBuffer : register(u0);

[numthreads(64, 1, 1)]
void CSInjectSpawns(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= SpawnCount || MaxParticles == 0) return;
	uint Slot = (WriteCursor + DTid.x) % MaxParticles;
	ParticleBuffer[Slot] = SpawnBuffer[DTid.x];
}

float3 PixelToWorld(int2 Pixel, float DeviceZ)
{
    float2 UV = (Pixel + 0.5f) / ScreenAndClip.xy;
    float2 NDC = float2(UV.x * 2 - 1, 1 - UV.y * 2);
    float4 H = mul(float4(NDC, DeviceZ, 1), InvViewProj);
	
    return H / H.w;
}

float3 Bounce(float3 V, float3 N)
{
    float VdotN = dot(V, N);
    if (VdotN >= 0)
        return V;
	
    float3 PN = N * (-VdotN);
    V += PN; // 벽방향 성분 제거
    V *= (1 - CollisionFriction);
    V += PN * CollisionRestitution;
    return V;
}

int DepthCollision(float3 Position, out float3 Normal, out float3 A)
{
    Normal = float3(0, 0, 1);
    A = Position;

    // 좌표 변환 
    float4 Clip = mul(float4(Position, 1), ViewProj);
    if (Clip.w <= 0)
        return 0;

    float2 NDC = Clip.xy / Clip.w;
    float U = (NDC.x + 1) * 0.5f;
    float V = (1 - NDC.y) * 0.5f;
    if (U < 0 || U >= 1 || V < 0 || V >= 1)
        return 0;

    int2 Pixel = int2(float2(U, V) * ScreenAndClip.xy);
    Pixel = min(Pixel, int2(ScreenAndClip.xy) - 2);
    float Depth = SceneDepth.Load(int3(Pixel, 0));
    if (Depth >= 1)
        return 0;

    // 깊이를 선형 거리로 바꿔 Thickness 과 비교 
    float4 H = mul(float4(NDC, Depth, 1), InvViewProj);
    float WallDir = 1 / H.w;
    float ParticleDir = Clip.w;
    float Penetration = ParticleDir - WallDir;
    if (Penetration < -CollisionRadius || Penetration > DepthThickness)
        return 1;

    // 이웃픽셸로 노멀 복원 
    float DepthR = SceneDepth.Load(int3(Pixel + int2(1, 0), 0));
    float DepthD = SceneDepth.Load(int3(Pixel + int2(0, 1), 0));
    if (DepthR >= 1 || DepthD >= 1)
        return 2;
    
    A = PixelToWorld(Pixel, Depth);
    float3 B = PixelToWorld(Pixel + int2(1, 0), DepthR);
    float3 C = PixelToWorld(Pixel + int2(0, 1), DepthD);
    if (length(C - A) > DepthThickness || length(B - A) > DepthThickness)
        return 3;

    // 4단계: 물체 경계가 아니다 → 노멀 확정
    Normal = normalize(cross(C - A, B - A));
    float3 CamPoint = PixelToWorld(Pixel, 0);
    Normal = (dot(Normal, CamPoint - A) < 0.0f ? -Normal : Normal);
    return 4;
}

// 분포 모드에 따라 상수 또는 전역 아틀라스에서 값을 평가한다
float4 EvaluateDistribution(FGpuDistributionDesc Desc, float T)
{
	if (Desc.AtlasUVAndMode.w < 1.5f)
		return Desc.ValueScale;
	float2 UV = float2(Desc.AtlasUVAndMode.x + saturate(T) * Desc.AtlasUVAndMode.y, Desc.AtlasUVAndMode.z);
	return CurveAtlas.SampleLevel(CurveSampler, UV, 0) * Desc.ValueScale + Desc.ValueBias;
}

[numthreads(256, 1, 1)]
void CSSimulate(uint3 DTid : SV_DispatchThreadID)
{
	uint Idx = DTid.x;
	if (Idx >= MaxParticles) return;

	FGpuParticle p = ParticleBuffer[Idx];
	if (p.bAlive == 0) return;

	p.RelativeTime += DeltaTime / max(p.Lifetime, 0.001f);
	if (p.RelativeTime >= 1.0f)
	{
		p.bAlive = 0;
		ParticleBuffer[Idx] = p;
		return;
	}

	float T = saturate(p.RelativeTime);
	if (VelocityScale.AtlasUVAndMode.w > 0.5f)
	{
		float3 velScale = EvaluateDistribution(VelocityScale, T).xyz;
		p.Velocity = p.BaseVelocity * velScale;
	}
	if (RotationRateScale.AtlasUVAndMode.w > 0.5f)
	{
		float rotScale = EvaluateDistribution(RotationRateScale, T).x;
		p.RotationRate = p.BaseRotationRate * rotScale;
	}
	
	p.Position += p.Velocity * DeltaTime;
	p.Rotation += p.RotationRate * DeltaTime;

    float Depth = 0.0f;
    int Stage = 0;
    bool bHit = false;
    float3 Normal = float3(0, 0, 1);
    float3 A = p.Position;
	
    if (HasCollision != 0 && p.Pad.x < CollisionMaxCount)
    {
        Stage = DepthCollision(p.Position, Normal, A);
        bHit = (Stage == 4);
    }

    if (bHit)
    {
        float d = dot(p.Position - A, Normal); // 표면->노멀방향 거리
        if (d < CollisionRadius)
            p.Position += Normal * (CollisionRadius - d); // 밀어내기

        if (dot(p.Velocity, Normal) < 0)
            p.Pad.x++;
		
        p.Velocity = Bounce(p.Velocity, Normal);
        p.BaseVelocity = Bounce(p.BaseVelocity, Normal);
    }
	
	if (SizeScale.AtlasUVAndMode.w > 0.5f)
	{
		float3 sizeScale = EvaluateDistribution(SizeScale, T).xyz;
		p.Size = p.BaseSize * sizeScale;
	}
	if (ColorOverLife.AtlasUVAndMode.w > 0.5f)
	{
		float4 curveColor = saturate(EvaluateDistribution(ColorOverLife, T));
		p.Color = float4(curveColor.rgb, p.BaseColor.a * curveColor.a);
	}
	if (HasColorOverride != 0)
	{
		p.Color = float4(ColorOverride.rgb, ColorOverLife.AtlasUVAndMode.w > 0.5f ? p.Color.a : ColorOverride.a);
	}
	if (SubUV.AtlasUVAndMode.w > 0.5f || SubUVLinear != 0)
	{
		float TotalFrames = max(1.0f, SubUVCols * SubUVRows);
		float ClampedT = clamp(p.RelativeTime, 0.0f, 0.9999f);
		float Rate = max(0.0f, SubUVPlayRate);
		float ScaledT = ClampedT * Rate;
		float RawIndex = 0.0f;
		if (SubUVLinear != 0)
		{
			RawIndex = (Rate > 1.0f) ? fmod(ScaledT * TotalFrames, TotalFrames) : (ScaledT * TotalFrames);
		}
		else if (SubUV.AtlasUVAndMode.w > 0.5f)
		{
			float CurveT = (Rate > 1.0f) ? fmod(ScaledT, 1.0f) : saturate(ScaledT);
			RawIndex = EvaluateDistribution(SubUV, CurveT).x;
		}
		p.SubImageIndex = floor(clamp(RawIndex, 0.0f, TotalFrames - 1.0f));
	}
	
    if (p.Pad.x > 0)
        p.Color.rgb = float3(1.0f, 0.2f, 0.2f);
	
	ParticleBuffer[Idx] = p;
}
