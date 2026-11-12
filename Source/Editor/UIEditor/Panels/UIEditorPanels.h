#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/UIEditor/UIEditor.h"

// UI 에디터 패널의 베이스입니다. 공통 동작은 TAssetEditorPanel에 있으므로 별칭만 둡니다.
using FUIEditorPanel = TAssetEditorPanel<FUIEditor>;

// 만들 수 있는 위젯 클래스 목록입니다. 누르면 선택된 패널 아래에 추가합니다.
class FUIPalettePanel : public FUIEditorPanel
{
public:
	explicit FUIPalettePanel(FUIEditor& InEditor);

protected:
	void DrawContent() override;
};

// 위젯 트리입니다. 끌어서 다른 패널 아래로 옮길 수 있습니다.
class FUIHierarchyPanel : public FUIEditorPanel
{
public:
	explicit FUIHierarchyPanel(FUIEditor& InEditor);

protected:
	void DrawContent() override;

private:
	void DrawNode(UWidget* Widget);
};

// 미리보기 텍스처를 보여 주고 클릭 선택, 이동, 크기 조절을 처리합니다.
class FUICanvasPanel : public FUIEditorPanel
{
public:
	explicit FUICanvasPanel(FUIEditor& InEditor);

protected:
	void DrawContent() override;
	ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

private:
	enum class EDragMode { None, Move, Resize };

	void HandleInput(bool bCanvasHovered);
	UWidget* PickWidget(UWidget* Widget, float X, float Y) const;
	bool IsOverResizeHandle(float X, float Y) const;
	void ApplyDrag(float DX, float DY);

	// 디자인 좌표를 화면 좌표로 바꿉니다.
	ImVec2 ToScreen(float X, float Y) const { return ImVec2(CanvasMin.x + X * ViewScale, CanvasMin.y + Y * ViewScale); }

	ImVec2 CanvasMin;
	float ViewScale = 1.0f;

	EDragMode DragMode = EDragMode::None;
	float DragStartX = 0.0f, DragStartY = 0.0f;
	FVector4 DragStartOffsets;
};

// 선택된 위젯의 앵커 프리셋과 프로퍼티를 편집합니다.
class FUIDetailsPanel : public FUIEditorPanel
{
public:
	explicit FUIDetailsPanel(FUIEditor& InEditor);

protected:
	void DrawContent() override;
};
