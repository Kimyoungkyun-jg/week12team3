#pragma once

#include "StaticMeshEditorPanel.h"

// 메시 상세 정보 UI를 확장하기 위한 패널 골격입니다.
class FStaticMeshDetailsPanel : public FStaticMeshEditorPanel
{
public:
	explicit FStaticMeshDetailsPanel(FStaticMeshEditor& InEditor);

protected:
	void DrawContent() override;
};
