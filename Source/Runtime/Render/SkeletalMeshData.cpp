#include "EnginePCH.h"
#include "SkeletalMeshData.h"

bool FSkeletalMeshData::Validate(FString& OutError) const
{
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.Num());
	const uint32 IndexCount = static_cast<uint32>(Indices.Num());

	if (VertexCount == 0 || IndexCount == 0 || IndexCount % 3 != 0)
	{
		OutError = std::format("invalid mesh (vertices {}, indices {})", VertexCount, IndexCount);
		return false;
	}

	for (uint32 Index : Indices)
	{
		if (Index >= VertexCount)
		{
			OutError = std::format("index {} out of range (vertices {})", Index, VertexCount);
			return false;
		}
	}

	uint32 SectionIndexSum = 0;
	for (const FSkeletalMeshSection& Section : Sections)
	{
		if (Section.StartIndex + Section.IndexCount > IndexCount)
		{
			OutError = std::format("section range {}+{} exceeds indices {}", Section.StartIndex, Section.IndexCount, IndexCount);
			return false;
		}
		if (Section.MaterialSlotIndex >= static_cast<uint32>(MaterialSlots.Num()))
		{
			OutError = std::format("material slot {} out of range (slots {})", Section.MaterialSlotIndex, MaterialSlots.Num());
			return false;
		}
		SectionIndexSum += Section.IndexCount;
	}
	if (SectionIndexSum != IndexCount)
	{
		OutError = std::format("section index sum {} != indices {}", SectionIndexSum, IndexCount);
		return false;
	}

	return true;
}

void FSkeletalMeshData::BuildTangentSpace() {
	const int32 VertexCount{ SkinnedVertices.Num() };
	const int32 IndexCount{ Indices.Num() };
	constexpr float UVTolerance{ 1.0e-8f };
	constexpr float VectorToleranceSquared{ 1.0e-12f };

	TArray<FVector> TangentSums{};
	TArray<FVector> BitangentSums{};
	TangentSums.SetNum(VertexCount);
	BitangentSums.SetNum(VertexCount);
	// 삼각형마다 Tangent, Bitangent를 계산하고 각 정점에 합산
	for (int32 Index{ 0 }; Index + 2 < IndexCount; Index += 3) {
		const uint32 IndexA{ Indices[Index] };
		const uint32 IndexB{ Indices[Index + 1] };
		const uint32 IndexC{ Indices[Index + 2] };

		const FSkeletalVertex& VertexA{ SkinnedVertices[IndexA] };
		const FSkeletalVertex& VertexB{ SkinnedVertices[IndexB] };
		const FSkeletalVertex& VertexC{ SkinnedVertices[IndexC] };

		const FVector Edge1{ VertexB.Position - VertexA.Position };
		const FVector Edge2{ VertexC.Position - VertexA.Position };
		const FVector2 DeltaUV1{ VertexB.UV - VertexA.UV };
		const FVector2 DeltaUV2{ VertexC.UV - VertexA.UV };

		const float Determinant{ DeltaUV1.X * DeltaUV2.Y - DeltaUV1.Y * DeltaUV2.X };
		if (std::abs(Determinant) <= UVTolerance) {
			continue;
		}

		const FVector Tangent{ (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) / Determinant };
		const FVector Bitangent{ (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) / Determinant };

		TangentSums[IndexA] += Tangent;
		TangentSums[IndexB] += Tangent;
		TangentSums[IndexC] += Tangent;

		BitangentSums[IndexA] += Bitangent;
		BitangentSums[IndexB] += Bitangent;
		BitangentSums[IndexC] += Bitangent;
	}

	// 각 정점마다 합산된 Tangent, Bitangent를 정규화하고 Handedness를 계산하여 최종 Tangent를 설정
	for (int32 Index{ 0 }; Index < VertexCount; ++Index) {
		FSkeletalVertex& Vertex{ SkinnedVertices[Index] };
		const FVector Normal{ Vertex.Normal.GetSafeNormal(VectorToleranceSquared) };

		if (Normal.SizeSquared() <= VectorToleranceSquared) {
			Vertex.Tangent = FVector4{ 1.0f, 0.0f, 0.0f, 1.0f };
			continue;
		}

		FVector Tangent{ TangentSums[Index] - Normal * Normal.Dot(TangentSums[Index]) };

		if (Tangent.SizeSquared() <= VectorToleranceSquared) {
			const FVector Axis{ std::abs(Normal.X) < 0.9f ? FVector{ 1.0f, 0.0f, 0.0f } : FVector{ 0.0f, 1.0f, 0.0f } };
			Tangent = Axis - Normal * Normal.Dot(Axis);
		}

		Tangent = Tangent.GetSafeNormal(VectorToleranceSquared);

		const float Handedness{ Normal.Cross(Tangent).Dot(BitangentSums[Index]) < 0.0f ? -1.0f : 1.0f };
		Vertex.Tangent = FVector4{ Tangent, Handedness };
	}
}