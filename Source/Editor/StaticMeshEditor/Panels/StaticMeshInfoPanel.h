#pragma once

#include "StaticMeshEditorPanel.h"

// 원본 애셋의 식별 정보와 메시 통계를 읽기 전용으로 표시합니다.
class FStaticMeshInfoPanel : public FStaticMeshEditorPanel
{
public:
	explicit FStaticMeshInfoPanel(FStaticMeshEditor& InEditor);

protected:
	void DrawContent() override;
};
