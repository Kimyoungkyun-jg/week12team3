#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector4.h"
#include "Render/Texture2D.h"

struct FParticleCurveAtlasHandle
{
	uint32 SlotIndex = UINT32_MAX;
	float UStart = 0.0f;
	float UScale = 0.0f;
	float VCoord = 0.0f;

	bool IsValid() const { return SlotIndex != UINT32_MAX; }
	void Invalidate()
	{
		SlotIndex = UINT32_MAX;
		UStart = 0.0f;
		UScale = 0.0f;
		VCoord = 0.0f;
	}
};

// 파티클 커브들을 공유 이차원 텍스처 아틀라스에 관리한다
class FParticleCurveTexture
{
public:
	static constexpr uint32 AtlasWidth = 512;
	static constexpr uint32 AtlasHeight = 512;
	static constexpr uint32 CurveSampleWidth = 128;
	static constexpr uint32 CurvesPerRow = AtlasWidth / CurveSampleWidth;
	static constexpr uint32 TotalAtlasSlots = AtlasHeight * CurvesPerRow;

	static FParticleCurveTexture& Get();
	static bool IsInitialized();

	bool Init();
	void Shutdown();

	FParticleCurveAtlasHandle AllocateCurve(const TArray<FVector4>& Samples);
	void UpdateCurveSamples(const FParticleCurveAtlasHandle& Handle, const TArray<FVector4>& Samples);
	void FreeCurve(FParticleCurveAtlasHandle& InOutHandle);
	void FlushPendingUploads();
	FTexture2D* GetTexture() const { return AtlasTexture.get(); }

private:
	struct FPendingUpload
	{
		uint32 SlotIndex = 0;
		TArray<FVector4> Texels;
	};

	void EnqueueUpload(uint32 SlotIndex, const TArray<FVector4>& Samples);
	FParticleCurveAtlasHandle MakeHandle(uint32 SlotIndex) const;

	TUniquePtr<FTexture2D> AtlasTexture;
	TArray<uint32> FreeSlots;
	TArray<FPendingUpload> PendingUploads;
	uint32 NextSlotIndex = 0;
	bool bShutDown = false;
};
