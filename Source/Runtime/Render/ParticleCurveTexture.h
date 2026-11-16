#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector4.h"
#include "Render/Texture2D.h"

struct FParticleCurveAtlasHandle
{
	uint16 X = 0;
	uint16 Y = 0;
	uint16 Size = 0;
	float UStart = 0.0f;
	float UScale = 0.0f;
	float VCoord = 0.0f;

	bool IsValid() const { return Size > 0; }
	void Invalidate()
	{
		X = 0;
		Y = 0;
		Size = 0;
		UStart = 0.0f;
		UScale = 0.0f;
		VCoord = 0.0f;
	}
};

// 파티클 커브들을 공유 이차원 텍스처 아틀라스에 가변 폭으로 관리한다
class FParticleCurveTexture
{
public:
	static constexpr uint32 AtlasWidth = 512;
	static constexpr uint32 AtlasHeight = 512;
	static constexpr uint32 MaxCurveSampleWidth = 128;
	static constexpr uint32 CurveSampleWidth = MaxCurveSampleWidth;
	static constexpr uint32 TotalAtlasTexels = AtlasWidth * AtlasHeight;

	static FParticleCurveTexture& Get();
	static bool IsInitialized();

	bool Init();
	void Shutdown();

	FParticleCurveAtlasHandle AllocateCurve(const TArray<FVector4>& Samples);
	void UpdateCurveSamples(const FParticleCurveAtlasHandle& Handle, const TArray<FVector4>& Samples);
	void FreeCurve(FParticleCurveAtlasHandle& InOutHandle);
	void FlushPendingUploads();
	FTexture2D* GetTexture() const { return AtlasTexture.get(); }
	uint32 GetUsedTexels() const { return AllocatedTexels; }
	uint32 GetTotalTexels() const { return TotalAtlasTexels; }
	uint32 GetActiveCurveCount() const { return ActiveCurveCount; }

private:
	struct FTexelSpan
	{
		uint16 Begin = 0;
		uint16 Size = 0;
	};

	struct FPendingUpload
	{
		uint16 X = 0;
		uint16 Y = 0;
		TArray<FVector4> Texels;
	};

	void ResetFreeSpans();
	void EnqueueUpload(uint16 X, uint16 Y, uint16 Width, const TArray<FVector4>& Samples);
	FParticleCurveAtlasHandle MakeHandle(uint16 X, uint16 Y, uint16 Size) const;

	TUniquePtr<FTexture2D> AtlasTexture;
	TArray<TArray<FTexelSpan>> RowFreeSpans;
	TArray<FPendingUpload> PendingUploads;
	uint32 AllocatedTexels = 0;
	uint32 ActiveCurveCount = 0;
	bool bShutDown = false;
};
