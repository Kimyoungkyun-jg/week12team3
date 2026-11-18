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
	uint CollisionCount;
	float Pad;
	float4 MeshRotation;
	float3 AngularVelocity;
	float SimPad;
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
	float3 Acceleration;
	float AccelPad;
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

// 분포 모드에 따라 상수 또는 전역 아틀라스에서 값을 평가한다
float4 EvaluateDistribution(FGpuDistributionDesc Desc, float T)
{
	if (Desc.AtlasUVAndMode.w < 1.5f)
		return Desc.ValueScale;
	float2 UV = float2(Desc.AtlasUVAndMode.x + saturate(T) * Desc.AtlasUVAndMode.y, Desc.AtlasUVAndMode.z);
	return CurveAtlas.SampleLevel(CurveSampler, UV, 0) * Desc.ValueScale + Desc.ValueBias;
}

bool ReconstructSceneWorldPos(int2 Pixel, int2 MaxPixel, out float3 OutWorldPos, out float OutViewDepth)
{
	OutWorldPos = float3(0.0f, 0.0f, 0.0f);
	OutViewDepth = 0.0f;
	if (Pixel.x < 0 || Pixel.y < 0 || Pixel.x > MaxPixel.x || Pixel.y > MaxPixel.y)
		return false;

	float D = SceneDepth.Load(int3(Pixel, 0));
	if (D >= 1.0f)
		return false;

	float2 PixelUV = (float2(Pixel) + 0.5f) / ScreenAndClip.xy;
	float2 PixelNDC = float2(PixelUV.x * 2.0f - 1.0f, 1.0f - PixelUV.y * 2.0f);
	float4 H = mul(float4(PixelNDC, D, 1.0f), InvViewProj);
	if (abs(H.w) < 1e-6f)
		return false;

	OutWorldPos = H.xyz / H.w;
	OutViewDepth = 1.0f / H.w;
	return true;
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
	else
	{
		p.Velocity += Acceleration * DeltaTime;
	}
	if (RotationRateScale.AtlasUVAndMode.w > 0.5f)
	{
		float rotScale = EvaluateDistribution(RotationRateScale, T).x;
		p.RotationRate = p.BaseRotationRate * rotScale;
	}

	p.Position += p.Velocity * DeltaTime;
	p.Rotation += p.RotationRate * DeltaTime;

	float AngSpeedSq = dot(p.AngularVelocity, p.AngularVelocity);
	if (AngSpeedSq > 1e-10f)
	{
		float AngSpeed = sqrt(AngSpeedSq);
		float HalfAngle = 0.5f * AngSpeed * DeltaTime;
		float S = sin(HalfAngle);
		float C = cos(HalfAngle);
		float4 DeltaQ = float4((p.AngularVelocity / AngSpeed) * S, C);
		float4 BaseQ = length(p.MeshRotation) > 1e-5f ? p.MeshRotation : float4(0.0f, 0.0f, 0.0f, 1.0f);
		float4 NextQ;
		NextQ.x = DeltaQ.w * BaseQ.x + DeltaQ.x * BaseQ.w + DeltaQ.y * BaseQ.z - DeltaQ.z * BaseQ.y;
		NextQ.y = DeltaQ.w * BaseQ.y - DeltaQ.x * BaseQ.z + DeltaQ.y * BaseQ.w + DeltaQ.z * BaseQ.x;
		NextQ.z = DeltaQ.w * BaseQ.z + DeltaQ.x * BaseQ.y - DeltaQ.y * BaseQ.x + DeltaQ.z * BaseQ.w;
		NextQ.w = DeltaQ.w * BaseQ.w - DeltaQ.x * BaseQ.x - DeltaQ.y * BaseQ.y - DeltaQ.z * BaseQ.z;
		p.MeshRotation = normalize(NextQ);
	}

	if (HasCollision != 0 && p.CollisionCount < CollisionMaxCount)
	{
		float4 Clip = mul(float4(p.Position, 1.0f), ViewProj);
		if (Clip.w > 0.0f && ScreenAndClip.x > 0.0f && ScreenAndClip.y > 0.0f)
		{
			float2 NDC = Clip.xy / Clip.w;
			float U = (NDC.x + 1.0f) * 0.5f;
			float V = (1.0f - NDC.y) * 0.5f;
			if (U >= 0.0f && U < 1.0f && V >= 0.0f && V < 1.0f)
			{
				int2 MaxPixel = int2(ScreenAndClip.xy) - int2(1, 1);
				int2 Pixel = clamp(int2(float2(U, V) * ScreenAndClip.xy), int2(0, 0), MaxPixel);

				float3 HWorld = float3(0.0f, 0.0f, 0.0f);
				float WallDir = 0.0f;
				if (ReconstructSceneWorldPos(Pixel, MaxPixel, HWorld, WallDir))
				{
					float ParticleDir = Clip.w;
					float Penetration = ParticleDir - WallDir;
					if (Penetration >= -CollisionRadius && Penetration <= DepthThickness)
					{
						float4 NearWH = mul(float4(NDC, 0.0f, 1.0f), InvViewProj);
						float3 CamRay = abs(NearWH.w) > 1e-6f ? (HWorld - NearWH.xyz / NearWH.w) : p.Velocity;

						float3 HitNormal = length(p.Velocity) > 1e-5f ? -normalize(p.Velocity) : -normalize(CamRay);
						float3 PosR, PosL, PosD, PosU;
						float DepthUnused;
						bool bHasR = ReconstructSceneWorldPos(Pixel + int2(1, 0), MaxPixel, PosR, DepthUnused);
						bool bHasL = ReconstructSceneWorldPos(Pixel - int2(1, 0), MaxPixel, PosL, DepthUnused);
						bool bHasD = ReconstructSceneWorldPos(Pixel + int2(0, 1), MaxPixel, PosD, DepthUnused);
						bool bHasU = ReconstructSceneWorldPos(Pixel - int2(0, 1), MaxPixel, PosU, DepthUnused);

						float3 dX = float3(0.0f, 0.0f, 0.0f);
						if (bHasR && bHasL)
						{
							float3 DiffR = PosR - HWorld;
							float3 DiffL = HWorld - PosL;
							dX = dot(DiffR, DiffR) <= dot(DiffL, DiffL) ? DiffR : DiffL;
						}
						else if (bHasR)
						{
							dX = PosR - HWorld;
						}
						else if (bHasL)
						{
							dX = HWorld - PosL;
						}

						float3 dY = float3(0.0f, 0.0f, 0.0f);
						if (bHasD && bHasU)
						{
							float3 DiffD = PosD - HWorld;
							float3 DiffU = HWorld - PosU;
							dY = dot(DiffD, DiffD) <= dot(DiffU, DiffU) ? DiffD : DiffU;
						}
						else if (bHasD)
						{
							dY = PosD - HWorld;
						}
						else if (bHasU)
						{
							dY = HWorld - PosU;
						}

						float3 SurfaceCross = cross(dX, dY);
						float CrossLenSq = dot(SurfaceCross, SurfaceCross);
						if (CrossLenSq > 1e-10f)
						{
							HitNormal = SurfaceCross * rsqrt(CrossLenSq);
							if (dot(HitNormal, CamRay) > 0.0f)
								HitNormal = -HitNormal;
						}

						float VDotN = dot(p.Velocity, HitNormal);
						if (VDotN < 0.0f)
						{
							p.Position = HWorld + HitNormal * (CollisionRadius + 0.002f);

							float3 ProjectedNormal = HitNormal * (-VDotN);
							float3 Vt = p.Velocity + ProjectedNormal;
							Vt *= (1.0f - CollisionFriction);
							Vt += ProjectedNormal * CollisionRestitution;
							p.Velocity = Vt;
							p.AngularVelocity *= (1.0f - CollisionFriction);
							p.CollisionCount += 1;
						}
					}
				}
			}
		}
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

	ParticleBuffer[Idx] = p;
}
