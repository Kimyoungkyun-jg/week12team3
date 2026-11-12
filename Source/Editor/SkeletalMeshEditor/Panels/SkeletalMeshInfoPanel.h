#pragma once

#include "SkeletalMeshEditorPanel.h"

// 원본 애셋의 식별 정보와 메시 통계를 읽기 전용으로 표시합니다.
class FSkeletalMeshInfoPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshInfoPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;
};
