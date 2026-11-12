#pragma once

#include "StaticMeshEditorPanel.h"

// 프리뷰 렌더 결과를 표시하고 카메라 입력과 화면 크기를 Editor에 전달합니다.
class FStaticMeshViewportPanel : public FStaticMeshEditorPanel
{
public:
	explicit FStaticMeshViewportPanel(FStaticMeshEditor& InEditor);

protected:
	void DrawContent() override;
};
