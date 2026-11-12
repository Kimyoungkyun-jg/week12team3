#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineString.h"

// 애셋 에디터 탭 안에 도킹되는 패널의 공통 동작을 담당합니다.
// 창 생성, 도킹 클래스 지정, 탭 도킹 영역 밖으로 분리됐는지 판정까지가 여기 있고
// 에디터 타입에 따라 달라지는 부분은 TAssetEditorPanel이 맡습니다.
class FAssetEditorPanelBase : public IEditorPanel
{
public:
	// InWindowId는 같은 이름의 패널이 다른 애셋 창과 섞이지 않도록 하는 ImGui 창 ID입니다.
	FAssetEditorPanelBase(const char* InName, const FString& InWindowId);

	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;

	const char* GetPanelName() const override { return PanelName.c_str(); }
	const char* GetWindowName() const { return WindowName.c_str(); }
	void SetDockClass(ImGuiID InClassId) { DockClassId = InClassId; }

	// 현재 패널이 소속 탭의 도킹 영역 밖에 있는지 확인합니다.
	bool IsDetached(ImGuiID DockSpaceId) const;

protected:
	// 파생 패널은 Begin/End 없이 창 내부 내용만 그립니다.
	virtual void DrawContent() = 0;

	// 스크롤 금지처럼 창 플래그가 필요한 패널만 재정의합니다.
	virtual ImGuiWindowFlags GetWindowFlags() const { return ImGuiWindowFlags_None; }

private:
	FString PanelName;
	FString WindowName;
	ImGuiID DockClassId = 0;
};

// 에디터 타입별 패널 베이스입니다. 에디터마다 베이스 클래스를 새로 만들지 않고 별칭만 둡니다.
//   using FUIEditorPanel = TAssetEditorPanel<FUIEditor>;
// TEditor는 GetAssetPath()를 제공해야 합니다 (FAssetEditor 파생이면 충족).
template <typename TEditor>
class TAssetEditorPanel : public FAssetEditorPanelBase
{
public:
	// InIdPrefix는 에디터 종류를 구분하는 접두사입니다. 예: "UIEditorPanel"
	TAssetEditorPanel(TEditor& InEditor, const char* InName, const char* InIdPrefix)
		: FAssetEditorPanelBase(InName, FString(InIdPrefix) + ":" + InName + ":" + InEditor.GetAssetPath())
		, Editor(InEditor)
	{
	}

protected:
	TEditor& Editor;
};
