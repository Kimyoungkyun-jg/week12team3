#pragma once

#include "Editor/EditorUI/EditorPanel.h"

class IEditorTab
{
public:
	virtual ~IEditorTab() = default;
	virtual const char* GetTabName() const = 0;
	virtual bool CanDetach() const { return true; }
	// BeginMenuBar/EndMenuBar는 컨테이너가 담당하고 탭은 메뉴 내용만 그린다.
	virtual void DrawMenuBar() {}
	virtual bool CanClose() const { return false; }
	virtual void OnCloseRequested() {}
	// 닫아도 되면 true. false면 닫기를 취소한다 (탭이 저장 확인 같은 UI를 직접 띄운다).
	virtual bool ConfirmClose() { return true; }
	virtual void OnRender(bool bActive) = 0;
};
