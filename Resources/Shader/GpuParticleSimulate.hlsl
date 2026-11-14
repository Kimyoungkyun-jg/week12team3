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
	float SubImageIndex;
	uint bAlive;
	float2 Pad;
};

cbuffer SimParams : register(b0)
{
	float DeltaTime;
	uint MaxParticles;
	uint SpawnCount;
	uint WriteCursor;

	float4 ColorStart;
	float4 ColorEnd;

	float3 SizeStart;
	float SubUVCols;

	float3 SizeEnd;
	float SubUVRows;

	uint UseColorModule;
	uint UseSizeModule;
	uint UseColorOverLife;
	uint UseSizeByLife;

	uint UseSubUV;
	uint SubUVLinear;
	float SubUVPlayRate;
	uint HasColorOverride;

	float4 ColorOverride;
};

StructuredBuffer<FGpuParticle> SpawnBuffer : register(t0);
StructuredBuffer<float4> CurveLUT : register(t1);
RWStructuredBuffer<FGpuParticle> ParticleBuffer : register(u0);

[numthreads(64, 1, 1)]
void CSInjectSpawns(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= SpawnCount || MaxParticles == 0) return;
	uint Slot = (WriteCursor + DTid.x) % MaxParticles;
	ParticleBuffer[Slot] = SpawnBuffer[DTid.x];
}

float4 SampleCurveLUT(uint BaseOffset, float T)
{
	float Scaled = saturate(T) * 127.0f;
	uint Idx0 = (uint)floor(Scaled);
	uint Idx1 = min(Idx0 + 1, 127);
	float Frac = Scaled - (float)Idx0;
	return lerp(CurveLUT[BaseOffset + Idx0], CurveLUT[BaseOffset + Idx1], Frac);
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

	p.Position += p.Velocity * DeltaTime;
	p.Rotation += p.RotationRate * DeltaTime;

	float T = saturate(p.RelativeTime);
	if (UseColorModule != 0)
	{
		p.Color = lerp(ColorStart, ColorEnd, T);
	}
	if (UseSizeModule != 0)
	{
		p.Size = lerp(SizeStart, SizeEnd, T);
	}
	if (UseSizeByLife != 0)
	{
		float3 Scale = SampleCurveLUT(0, T).xyz;
		p.Size = p.BaseSize * Scale;
	}
	if (UseColorOverLife != 0)
	{
		p.Color = saturate(SampleCurveLUT(128, T));
	}
	if (HasColorOverride != 0)
	{
		p.Color = float4(ColorOverride.rgb, UseColorOverLife != 0 ? p.Color.a : ColorOverride.a);
	}
	if (UseSubUV != 0)
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
		else
		{
			float CurveT = (Rate > 1.0f) ? fmod(ScaledT, 1.0f) : saturate(ScaledT);
			RawIndex = SampleCurveLUT(256, CurveT).x;
		}
		p.SubImageIndex = floor(clamp(RawIndex, 0.0f, TotalFrames - 1.0f));
	}

	ParticleBuffer[Idx] = p;
}
