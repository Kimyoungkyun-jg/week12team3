#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/EditorUI/EditorCommand.h"
#include "Editor/HitoriEd/PlayInEditorDataTypes.h"

// 툴바 그룹 규칙. Play 그룹과 편집 컨트롤이 같은 배경·간격·높이로 보이도록 함께 쓴다.
namespace LevelEditorToolbarUI
{
	// 사이의 항목을 배경 상자 하나로 묶는다. 항목 간격은 툴바의 ItemSpacing(4px)이다.
	void BeginGroup();
	void EndGroup();
	// 그룹 사이의 세로 구분선. 앞뒤로 같은 간격을 둔다.
	void Separator();
	// 버튼 바로 아래 왼쪽 끝에 다음 팝업을 연다. 클릭한 지점과 관계없이 같은 자리에 뜬다.
	void PlaceNextPopupBelowLastItem();
}

class FLevelEditorToolbar
{
public:
	~FLevelEditorToolbar() = default;

	void SetCommands(const TArray<FEditorCommand>& InCommands) { Commands = InCommands; }
	// Play 그룹 끝에 PIE 실행 위치 선택 메뉴를 붙인다. 엔진이 상태를 소유한다.
	void SetPlayModeBinding(std::function<EPlayModeType()> InGetPlayMode,
		std::function<void(EPlayModeType)> InSetPlayMode, std::function<bool()> InCanChangePlayMode)
	{
		GetPlayMode = std::move(InGetPlayMode);
		SetPlayMode = std::move(InSetPlayMode);
		CanChangePlayMode = std::move(InCanChangePlayMode);
	}
	void Draw();
	void SetEditingControls(std::function<void()> InDraw) { DrawEditingControls = std::move(InDraw); }


private:
	void DrawFileGroup();
	void DrawPlayGroup();
	void DrawPlayModeMenu();

	bool bIsOpen = true;
	TArray<FEditorCommand> Commands;
	std::function<EPlayModeType()> GetPlayMode;
	std::function<void(EPlayModeType)> SetPlayMode;
	std::function<bool()> CanChangePlayMode;
	std::function<void()> DrawEditingControls;


};
