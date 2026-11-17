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
	if (RowFreeSpans.IsEmpty())
		ResetFreeSpans();
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

void FParticleCurveTexture::ResetFreeSpans()
{
	RowFreeSpans.SetNum(AtlasHeight);
	for (uint32 Row = 0; Row < AtlasHeight; ++Row)
	{
		RowFreeSpans[Row].Reset();
		RowFreeSpans[Row].Add(FTexelSpan{ 0, static_cast<uint16>(AtlasWidth) });
	}
	AllocatedTexels = 0;
	ActiveCurveCount = 0;
}

void FParticleCurveTexture::Shutdown()
{
	bParticleCurveTextureAlive = false;
	bShutDown = true;
	PendingUploads.Empty();
	RowFreeSpans.Empty();
	AllocatedTexels = 0;
	ActiveCurveCount = 0;
	AtlasTexture.reset();
}

FParticleCurveAtlasHandle FParticleCurveTexture::MakeHandle(uint16 X, uint16 Y, uint16 Size) const
{
	FParticleCurveAtlasHandle Handle{};
	Handle.X = X;
	Handle.Y = Y;
	Handle.Size = Size;
	Handle.UStart = (static_cast<float>(X) + 0.5f) / static_cast<float>(AtlasWidth);
	Handle.UScale = Size > 1 ? static_cast<float>(Size - 1) / static_cast<float>(AtlasWidth) : 0.0f;
	Handle.VCoord = (static_cast<float>(Y) + 0.5f) / static_cast<float>(AtlasHeight);
	return Handle;
}

void FParticleCurveTexture::EnqueueUpload(uint16 X, uint16 Y, uint16 Width, const TArray<FVector4>& Samples)
{
	FPendingUpload Upload{};
	Upload.X = X;
	Upload.Y = Y;
	Upload.Texels.SetNum(Width);

	const int32 SourceCount = static_cast<int32>(Samples.Num());
	if (SourceCount <= 0)
	{
		for (uint16 i = 0; i < Width; ++i)
			Upload.Texels[i] = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else if (SourceCount == static_cast<int32>(Width))
	{
		std::memcpy(Upload.Texels.GetData(), Samples.GetData(), Width * sizeof(FVector4));
	}
	else
	{
		for (uint16 i = 0; i < Width; ++i)
		{
			const float U = Width > 1 ? static_cast<float>(i) / static_cast<float>(Width - 1) : 0.0f;
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
		if (Existing.X == X && Existing.Y == Y)
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

	const uint32 RawCount = static_cast<uint32>(Samples.Num());
	const uint16 ReqWidth = static_cast<uint16>(std::clamp<uint32>(
		RawCount > 0 ? RawCount : MaxCurveSampleWidth,
		1u,
		MaxCurveSampleWidth));

	for (uint32 Row = 0; Row < RowFreeSpans.Num(); ++Row)
	{
		TArray<FTexelSpan>& Spans = RowFreeSpans[Row];
		for (uint32 SpanIdx = 0; SpanIdx < Spans.Num(); ++SpanIdx)
		{
			FTexelSpan& Span = Spans[SpanIdx];
			if (Span.Size < ReqWidth)
				continue;

			const uint16 AllocX = Span.Begin;
			if (Span.Size == ReqWidth)
			{
				Spans.RemoveAt(SpanIdx);
			}
			else
			{
				Span.Begin = static_cast<uint16>(Span.Begin + ReqWidth);
				Span.Size = static_cast<uint16>(Span.Size - ReqWidth);
			}
			AllocatedTexels += ReqWidth;
			++ActiveCurveCount;
			EnqueueUpload(AllocX, static_cast<uint16>(Row), ReqWidth, Samples);
			return MakeHandle(AllocX, static_cast<uint16>(Row), ReqWidth);
		}
	}

	return FParticleCurveAtlasHandle{};
}

void FParticleCurveTexture::UpdateCurveSamples(const FParticleCurveAtlasHandle& Handle, const TArray<FVector4>& Samples)
{
	if (!Handle.IsValid() || Handle.Y >= AtlasHeight || Handle.X + Handle.Size > AtlasWidth)
		return;
	EnqueueUpload(Handle.X, Handle.Y, Handle.Size, Samples);
}

void FParticleCurveTexture::FreeCurve(FParticleCurveAtlasHandle& InOutHandle)
{
	if (!InOutHandle.IsValid())
		return;
	if (bParticleCurveTextureAlive && InOutHandle.Y < RowFreeSpans.Num() &&
		static_cast<uint32>(InOutHandle.X) + static_cast<uint32>(InOutHandle.Size) <= AtlasWidth)
	{
		for (int32 i = static_cast<int32>(PendingUploads.Num()) - 1; i >= 0; --i)
		{
			if (PendingUploads[i].X == InOutHandle.X && PendingUploads[i].Y == InOutHandle.Y)
				PendingUploads.RemoveAt(static_cast<uint32>(i));
		}

		AllocatedTexels = AllocatedTexels >= InOutHandle.Size ? AllocatedTexels - InOutHandle.Size : 0;
		if (ActiveCurveCount > 0)
			--ActiveCurveCount;

		TArray<FTexelSpan>& Spans = RowFreeSpans[InOutHandle.Y];
		const FTexelSpan Freed{ InOutHandle.X, InOutHandle.Size };
		uint32 InsertIdx = 0;
		while (InsertIdx < Spans.Num() && Spans[InsertIdx].Begin < Freed.Begin)
			++InsertIdx;

		const bool bMergePrev = InsertIdx > 0 &&
			(Spans[InsertIdx - 1].Begin + Spans[InsertIdx - 1].Size == Freed.Begin);
		const bool bMergeNext = InsertIdx < Spans.Num() &&
			(Freed.Begin + Freed.Size == Spans[InsertIdx].Begin);

		if (bMergePrev && bMergeNext)
		{
			Spans[InsertIdx - 1].Size = static_cast<uint16>(
				Spans[InsertIdx - 1].Size + Freed.Size + Spans[InsertIdx].Size);
			Spans.RemoveAt(InsertIdx);
		}
		else if (bMergePrev)
		{
			Spans[InsertIdx - 1].Size = static_cast<uint16>(Spans[InsertIdx - 1].Size + Freed.Size);
		}
		else if (bMergeNext)
		{
			Spans[InsertIdx].Begin = Freed.Begin;
			Spans[InsertIdx].Size = static_cast<uint16>(Spans[InsertIdx].Size + Freed.Size);
		}
		else
		{
			Spans.Insert(Freed, static_cast<int32>(InsertIdx));
		}
	}
	InOutHandle.Invalidate();
}

void FParticleCurveTexture::FlushPendingUploads()
{
	if (PendingUploads.IsEmpty())
		return;
	if (!Init())
		return;

	for (const FPendingUpload& Upload : PendingUploads)
	{
		const uint32 Width = static_cast<uint32>(Upload.Texels.Num());
		if (Width == 0 || Upload.Y >= AtlasHeight || static_cast<uint32>(Upload.X) + Width > AtlasWidth)
			continue;
		const uint32 RowPitch = Width * sizeof(FVector4);
		RenderCommand::UpdateTexture2DRegion(
			AtlasTexture.get(),
			Upload.X,
			Upload.Y,
			Width,
			1,
			Upload.Texels.GetData(),
			RowPitch);
	}
	PendingUploads.Empty();
}
