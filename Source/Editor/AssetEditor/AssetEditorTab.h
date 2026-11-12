#pragma once

#include "Editor/EditorUI/EditorTab.h"
#include "Core/EngineString.h"

class FAssetEditor;

class FAssetEditorTab : public IEditorTab
{
public:
	explicit FAssetEditorTab(FAssetEditor& InEditor);

	// 탭의 메뉴 바를 그립니다.
	void DrawMenuBar() override;
	
	// 탭의 렌더링을 수행합니다.
	void OnRender(bool bActive) override;

	// 탭이 닫히기 전에 호출됩니다.
	void OnCloseRequested() override;

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	// 저장하지 않은 변경이 있으면 파일 이름 뒤에 *를 붙인다 (### 뒤 ID는 같아 창 배치가 유지된다)
	const char* GetTabName() const override;
	bool CanClose() const override { return true; }
	bool ConfirmClose() override;

private:
	FAssetEditor& Editor;
	FString TabId;
	// GetTabName이 경로가 바뀌었을 때 다시 만든다
	mutable FString NamedPath;
	mutable FString TabName;
	mutable FString DirtyTabName;
};
