#include "EnginePCH.h"
#include "Render/ParticleCurveTexture.h"
#include "Render/RenderCommand.h"
#include <algorithm>
#include <cstring>

namespace
{
	bool bParticleCurveTextureAlive = false;
}

FParticleCurveTexture& FParticleCurveTexture::Get()
{
	static FParticleCurveTexture Instance;
	return Instance;
}

bool FParticleCurveTexture::IsInitialized()
{
	return bParticleCurveTextureAlive;
}

bool FParticleCurveTexture::Init()
{
	if (bShutDown)
		return false;
	if (AtlasTexture)
		return true;

	TArray<FVector4> InitialPixels;
	InitialPixels.SetNum(AtlasWidth * AtlasHeight);
	for (uint32 i = 0; i < AtlasWidth * AtlasHeight; ++i)
		InitialPixels[i] = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	FTextureDesc Desc{};
	Desc.Width = AtlasWidth;
	Desc.Height = AtlasHeight;
	Desc.Format = ETextureFormat::R32G32B32A32_Float;
	Desc.BindFlags = ETextureBindFlags::ShaderResource;
	Desc.Usage = ETextureUsage::Default;

	AtlasTexture = RenderCommand::CreateTexture2D(Desc, InitialPixels.GetData());
	if (!AtlasTexture)
		return false;

	bParticleCurveTextureAlive = true;
	return true;
}

void FParticleCurveTexture::Shutdown()
{
	bParticleCurveTextureAlive = false;
	bShutDown = true;
	PendingUploads.Empty();
	FreeSlots.Empty();
	NextSlotIndex = 0;
	AtlasTexture.reset();
}

FParticleCurveAtlasHandle FParticleCurveTexture::MakeHandle(uint32 SlotIndex) const
{
	const uint32 Col = SlotIndex % CurvesPerRow;
	const uint32 Row = SlotIndex / CurvesPerRow;
	const float PixelX = static_cast<float>(Col * CurveSampleWidth);
	const float PixelY = static_cast<float>(Row);

	FParticleCurveAtlasHandle Handle{};
	Handle.SlotIndex = SlotIndex;
	Handle.UStart = (PixelX + 0.5f) / static_cast<float>(AtlasWidth);
	Handle.UScale = static_cast<float>(CurveSampleWidth - 1) / static_cast<float>(AtlasWidth);
	Handle.VCoord = (PixelY + 0.5f) / static_cast<float>(AtlasHeight);
	return Handle;
}

void FParticleCurveTexture::EnqueueUpload(uint32 SlotIndex, const TArray<FVector4>& Samples)
{
	FPendingUpload Upload{};
	Upload.SlotIndex = SlotIndex;
	Upload.Texels.SetNum(CurveSampleWidth);

	const int32 SourceCount = static_cast<int32>(Samples.Num());
	if (SourceCount <= 0)
	{
		for (uint32 i = 0; i < CurveSampleWidth; ++i)
			Upload.Texels[i] = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else if (SourceCount == static_cast<int32>(CurveSampleWidth))
	{
		std::memcpy(Upload.Texels.GetData(), Samples.GetData(), CurveSampleWidth * sizeof(FVector4));
	}
	else
	{
		for (uint32 i = 0; i < CurveSampleWidth; ++i)
		{
			const float U = static_cast<float>(i) / static_cast<float>(CurveSampleWidth - 1);
			const float Scaled = U * static_cast<float>(SourceCount - 1);
			const int32 Idx0 = std::clamp(static_cast<int32>(Scaled), 0, SourceCount - 1);
			const int32 Idx1 = std::min(Idx0 + 1, SourceCount - 1);
			const float Frac = Scaled - static_cast<float>(Idx0);
			const FVector4& A = Samples[Idx0];
			const FVector4& B = Samples[Idx1];
			Upload.Texels[i] = FVector4(
				A.X + (B.X - A.X) * Frac,
				A.Y + (B.Y - A.Y) * Frac,
				A.Z + (B.Z - A.Z) * Frac,
				A.W + (B.W - A.W) * Frac);
		}
	}

	for (FPendingUpload& Existing : PendingUploads)
	{
		if (Existing.SlotIndex == SlotIndex)
		{
			Existing.Texels = std::move(Upload.Texels);
			return;
		}
	}
	PendingUploads.Add(std::move(Upload));
}

FParticleCurveAtlasHandle FParticleCurveTexture::AllocateCurve(const TArray<FVector4>& Samples)
{
	bShutDown = false;
	if (!Init())
		return FParticleCurveAtlasHandle{};

	uint32 SlotIndex = UINT32_MAX;
	if (!FreeSlots.IsEmpty())
	{
		SlotIndex = FreeSlots.Last();
		FreeSlots.Pop();
	}
	else if (NextSlotIndex < TotalAtlasSlots)
	{
		SlotIndex = NextSlotIndex++;
	}
	else
	{
		return FParticleCurveAtlasHandle{};
	}

	EnqueueUpload(SlotIndex, Samples);
	return MakeHandle(SlotIndex);
}

void FParticleCurveTexture::UpdateCurveSamples(const FParticleCurveAtlasHandle& Handle, const TArray<FVector4>& Samples)
{
	if (!Handle.IsValid() || Handle.SlotIndex >= TotalAtlasSlots)
		return;
	EnqueueUpload(Handle.SlotIndex, Samples);
}

void FParticleCurveTexture::FreeCurve(FParticleCurveAtlasHandle& InOutHandle)
{
	if (!InOutHandle.IsValid())
		return;
	if (bParticleCurveTextureAlive && InOutHandle.SlotIndex < TotalAtlasSlots)
	{
		for (int32 i = static_cast<int32>(PendingUploads.Num()) - 1; i >= 0; --i)
		{
			if (PendingUploads[i].SlotIndex == InOutHandle.SlotIndex)
				PendingUploads.RemoveAt(static_cast<uint32>(i));
		}
		FreeSlots.Add(InOutHandle.SlotIndex);
	}
	InOutHandle.Invalidate();
}

void FParticleCurveTexture::FlushPendingUploads()
{
	if (PendingUploads.IsEmpty())
		return;
	if (!Init())
		return;

	const uint32 RowPitch = CurveSampleWidth * sizeof(FVector4);
	for (const FPendingUpload& Upload : PendingUploads)
	{
		if (Upload.SlotIndex >= TotalAtlasSlots || Upload.Texels.Num() != CurveSampleWidth)
			continue;
		const uint32 Col = Upload.SlotIndex % CurvesPerRow;
		const uint32 Row = Upload.SlotIndex / CurvesPerRow;
		RenderCommand::UpdateTexture2DRegion(
			AtlasTexture.get(),
			Col * CurveSampleWidth,
			Row,
			CurveSampleWidth,
			1,
			Upload.Texels.GetData(),
			RowPitch);
	}
	PendingUploads.Empty();
}
