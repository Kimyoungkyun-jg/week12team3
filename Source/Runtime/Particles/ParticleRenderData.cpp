#include "EnginePCH.h"
#include "Particles/ParticleRenderData.h"

#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"

#include <algorithm>
#include <cmath>
#include <cstring>

bool FDynamicSpriteEmitterData::Initialize(uint32 InMaxParticles)
{
	
	VertexBuffer.reset();
	IndexBuffer.reset();
	MaxParticles = 0;
	ActiveIndexCount = 0;
	SortDistance = 0.0f;

	if (InMaxParticles == 0)
	{
		return false;
	}

	const uint32 MaxVertices = InMaxParticles * 4;
	const uint32 MaxIndices = InMaxParticles * 6;
	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(MaxVertices * sizeof(FParticleVertex), sizeof(FParticleVertex));
	IndexBuffer = RenderCommand::CreateDynamicIndexBuffer(MaxIndices);

	if (!VertexBuffer || !IndexBuffer)
	{
		VertexBuffer.reset();
		IndexBuffer.reset();
		return false;
	}

	MaxParticles = InMaxParticles;
	Vertices.Reserve(MaxVertices);
	Indices.Reserve(MaxIndices);
	return true;
}

bool FDynamicSpriteEmitterData::BuildVertexData(const FDynamicSpriteEmitterReplayData& ReplayData, const FViewContext& ViewContext)
{
	ActiveIndexCount = 0;
	SortDistance = 0.0f;
	const uint32 ParticleCount = static_cast<uint32>(ReplayData.ActiveParticles.Num());
	if (!VertexBuffer || !IndexBuffer || ParticleCount == 0 || ParticleCount > MaxParticles)
	{
		return false;
	}
	TArray<FBaseParticle> SortedParticles = ReplayData.ActiveParticles;
	const auto GetWorldPosition = [&](const FBaseParticle& Particle)
	{
		return ReplayData.ComponentToWorld.TransformPosition(Particle.Location);
	};

	const auto GetDistanceSquared = [&](const FBaseParticle& Particle)
	{
		const FVector Delta = GetWorldPosition(Particle) - ViewContext.CameraPosition;
		return Delta.Dot(Delta);
	};
	std::sort(SortedParticles.begin(), SortedParticles.end(), [&](const FBaseParticle& A, const FBaseParticle& B)
		{ return GetDistanceSquared(A) > GetDistanceSquared(B); });
	Vertices.Reset();
	Indices.Reset();

	const FVector WorldUp(0.0f, 0.0f, 1.0f);
	for (const FBaseParticle& Particle : SortedParticles)
	{
		const FVector WorldPosition = GetWorldPosition(Particle);
		FVector Forward = ViewContext.CameraForward.Normalized();
		if (Forward.Length() <= 1e-6f)
		{
			Forward = (WorldPosition - ViewContext.CameraPosition).Normalized();
		}
		FVector Right = FVector::Cross(WorldUp, Forward).Normalized();
		if (Right.Length() <= 1e-6f)
		{
			Right = FVector(0.0f, 1.0f, 0.0f);
		}
		const FVector Up = FVector::Cross(Forward, Right).Normalized();
		const float Width = std::abs(Particle.Size.X * ReplayData.ComponentScale.Y);
		const float Height = std::abs(Particle.Size.Y * ReplayData.ComponentScale.Z);
		const float Rotation = std::isfinite(Particle.Rotation) ? Particle.Rotation : 0.0f;
		const float CosRotation = std::cos(Rotation);
		const float SinRotation = std::sin(Rotation);

		const FVector2 Corners[] = {
			FVector2(-0.5f, 0.5f),
			FVector2(0.5f, 0.5f),
			FVector2(0.5f, -0.5f),
			FVector2(-0.5f, -0.5f)
		};

		const FVector2 UVs[] = {
			FVector2(0.0f, 0.0f),
			FVector2(1.0f, 0.0f),
			FVector2(1.0f, 1.0f),
			FVector2(0.0f, 1.0f)
		};

		const uint32 BaseVertex = static_cast<uint32>(Vertices.Num());
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float RotatedX = Corners[Index].X * CosRotation - Corners[Index].Y * SinRotation;
			const float RotatedY = Corners[Index].X * SinRotation + Corners[Index].Y * CosRotation;

			FParticleVertex& Vertex = Vertices.AddDefaulted_GetRef();
			Vertex.Position = WorldPosition + Right * (RotatedX * Width) + Up * (RotatedY * Height);
			Vertex.UV = UVs[Index];
			Vertex.Color = FVector4(Particle.Color.R, Particle.Color.G, Particle.Color.B, Particle.Color.A);
		}
		Indices.Add(BaseVertex);
		Indices.Add(BaseVertex + 1);
		Indices.Add(BaseVertex + 2);
		Indices.Add(BaseVertex);
		Indices.Add(BaseVertex + 2);
		Indices.Add(BaseVertex + 3);

		SortDistance = std::max(SortDistance, GetDistanceSquared(Particle));
	}
	void* VertexMemory = RenderCommand::MapWriteDiscard(VertexBuffer.get());
	if (!VertexMemory)
	{
		return false;
	}

	std::memcpy(VertexMemory, Vertices.GetData(), Vertices.Num() * sizeof(FParticleVertex));
	RenderCommand::Unmap(VertexBuffer.get());

	void* IndexMemory = RenderCommand::MapWriteDiscard(IndexBuffer.get());
	if (!IndexMemory)
	{
		return false;
	}

	std::memcpy(IndexMemory, Indices.GetData(), Indices.Num() * sizeof(uint32));
	RenderCommand::Unmap(IndexBuffer.get());

	ActiveIndexCount = static_cast<uint32>(Indices.Num());
	return ActiveIndexCount > 0;
}


