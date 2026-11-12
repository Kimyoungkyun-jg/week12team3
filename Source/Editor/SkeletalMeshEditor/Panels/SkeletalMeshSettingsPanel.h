#pragma once

#include "SkeletalMeshEditorPanel.h"

// 원본 애셋을 수정하지 않고 카메라 맞춤과 뷰포트 표시 모드를 설정합니다.
class FSkeletalMeshSettingsPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshSettingsPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;
};
