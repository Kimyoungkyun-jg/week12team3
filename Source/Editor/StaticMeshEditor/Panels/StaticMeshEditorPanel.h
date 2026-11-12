#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineString.h"

class FStaticMeshEditor;

// StaticMesh 패널의 창 생성, 닫기 상태 및 도킹을 공통으로 관리합니다.
class FStaticMeshEditorPanel : public IEditorPanel
{
public:
	FStaticMeshEditorPanel(FStaticMeshEditor& InEditor, const char* InName);

	////////////////////////////////////////////////////////////
	// 패널 공통 인터페이스
	////////////////////////////////////////////////////////////

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const char* GetPanelName() const override { return PanelName.c_str(); }
	const char* GetWindowName() const { return WindowName.c_str(); }
	void SetDockClass(ImGuiID InClassId) { DockClassId = InClassId; }

	// 현재 패널이 소속 탭의 도킹 영역 밖에 있는지 확인합니다.
	bool IsDetached(ImGuiID DockSpaceId) const;

protected:
	// 파생 패널은 Begin/End 없이 창 내부 내용만 그립니다.
	virtual void DrawContent() = 0;

	FStaticMeshEditor& Editor;

private:
	FString PanelName;
	FString WindowName;
	ImGuiID DockClassId = 0;
};
