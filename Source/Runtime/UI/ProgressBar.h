#pragma once

#include "UI/Widget.h"
#include "ProgressBar.generated.h"

// UE EProgressBarFillType과 순서·이름이 같다. 파일에는 항목 이름으로 저장된다.
UENUM()
enum class EProgressBarFillType : uint8
{
	LeftToRight = 0,
	RightToLeft = 1,
	FillFromCenter = 2,             // 가로세로 모두 가운데에서 바깥으로
	FillFromCenterHorizontal = 3,
	FillFromCenterVertical = 4,
	TopToBottom = 5,
	BottomToTop = 6,
};

// UE UProgressBar의 축소판. 배경 위에 Percent만큼 채움을 그린다.
// 텍스처 스타일(WidgetStyle)과 마퀴는 없고 색만 쓴다.
// 맞았을 때 잔상(trail)은 위젯 기능이 아니다. 바 두 개를 겹치고 뒤쪽 바를 Lua에서 늦게 따라가게 만든다.
UCLASS()
class UProgressBar : public UWidget
{
	GENERATED_BODY()

public:
	UProgressBar();

	// 0~1로 잘라서 넣는다.
	void SetPercent(float InPercent);
	float GetPercent() const { return Percent; }

	void SetFillColorAndOpacity(const FLinearColor& InColor) { FillColorAndOpacity = InColor; }

	virtual void Paint(FUIDrawList& DrawList, float ParentOpacity) const override;

	UPROPERTY(EditAnywhere)
	float Percent = 1.0f;
	UPROPERTY(EditAnywhere)
	EProgressBarFillType BarFillType = EProgressBarFillType::LeftToRight;
	UPROPERTY(EditAnywhere)
	FLinearColor FillColorAndOpacity = FLinearColor(0.20f, 0.85f, 0.35f, 1.0f);
	UPROPERTY(EditAnywhere)
	FLinearColor BackgroundColor = FLinearColor(0.05f, 0.05f, 0.07f, 0.85f);   // UE는 WidgetStyle.BackgroundImage
	// 디자인 해상도 기준 픽셀. 배경 안쪽으로 채움을 줄이는 여백이다.
	// UE는 FVector2D(가로, 세로)지만 이 엔진에는 FVector2 UPROPERTY가 없어 X, Y만 쓰는 FVector로 둔다.
	UPROPERTY(EditAnywhere)
	FVector BorderPadding = FVector(2.0f, 2.0f, 0.0f);
};