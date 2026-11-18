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
};

StructuredBuffer<FGpuParticle> SpawnBuffer : register(t0);
Texture2D CurveAtlas : register(t1);
SamplerState CurveSampler : register(s0);
RWStructuredBuffer<FGpuParticle> ParticleBuffer : register(u0);

[numthreads(64, 1, 1)]
void CSInjectSpawns(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= SpawnCount || MaxParticles == 0) return;
	uint Slot = (WriteCursor + DTid.x) % MaxParticles;
	ParticleBuffer[Slot] = SpawnBuffer[DTid.x];
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

	ParticleBuffer[Idx] = p;
}
