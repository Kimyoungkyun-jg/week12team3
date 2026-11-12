#include "EnginePCH.h"
#include "UI/ProgressBar.h"

UProgressBar::UProgressBar()
{
	Offsets = FVector4(0.0f, 0.0f, 300.0f, 24.0f);
}

void UProgressBar::SetPercent(float InPercent)
{
	Percent = FMath::Clamp(InPercent, 0.0f, 1.0f);
}

void UProgressBar::Paint(FUIDrawList& DrawList, float ParentOpacity) const
{
	if (!IsVisible()) return;

	const float Opacity = ParentOpacity * RenderOpacity;

	FLinearColor Background = BackgroundColor;
	Background.A *= Opacity;
	DrawList.AddRect(CachedRect, Background);

	const float Ratio = FMath::Clamp(Percent, 0.0f, 1.0f);
	if (Ratio <= 0.0f) return;

	// 여백만큼 안쪽으로 줄인 영역
	const float PadX = FMath::Max(0.0f, BorderPadding.X * CachedScale);
	const float PadY = FMath::Max(0.0f, BorderPadding.Y * CachedScale);
	FUIRect Inner = CachedRect;
	Inner.X += PadX;
	Inner.Y += PadY;
	Inner.W = FMath::Max(0.0f, Inner.W - PadX * 2.0f);
	Inner.H = FMath::Max(0.0f, Inner.H - PadY * 2.0f);

	// 채우는 방향에 따라 안쪽 영역에서 비율만큼 잘라 낸다.
	FUIRect Fill = Inner;
	switch (BarFillType)
	{
	case EProgressBarFillType::RightToLeft:
		Fill.W = Inner.W * Ratio;
		Fill.X = Inner.X + Inner.W - Fill.W;
		break;
	case EProgressBarFillType::FillFromCenter:
		Fill.W = Inner.W * Ratio;
		Fill.H = Inner.H * Ratio;
		Fill.X = Inner.X + (Inner.W - Fill.W) * 0.5f;
		Fill.Y = Inner.Y + (Inner.H - Fill.H) * 0.5f;
		break;
	case EProgressBarFillType::FillFromCenterHorizontal:
		Fill.W = Inner.W * Ratio;
		Fill.X = Inner.X + (Inner.W - Fill.W) * 0.5f;
		break;
	case EProgressBarFillType::FillFromCenterVertical:
		Fill.H = Inner.H * Ratio;
		Fill.Y = Inner.Y + (Inner.H - Fill.H) * 0.5f;
		break;
	case EProgressBarFillType::TopToBottom:
		Fill.H = Inner.H * Ratio;
		break;
	case EProgressBarFillType::BottomToTop:
		Fill.H = Inner.H * Ratio;
		Fill.Y = Inner.Y + Inner.H - Fill.H;
		break;
	case EProgressBarFillType::LeftToRight:
	default:
		Fill.W = Inner.W * Ratio;
		break;
	}

	FLinearColor FillColor = FillColorAndOpacity;
	FillColor.A *= Opacity;
	DrawList.AddRect(Fill, FillColor);
}