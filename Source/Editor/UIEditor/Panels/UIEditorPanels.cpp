#include "EnginePCH.h"
#include "Editor/UIEditor/Panels/UIEditorPanels.h"

#include "Editor/Details/PropertyEditor.h"
#include "UI/UserWidget.h"
#include "UObject/UObjectHash.h"

#include <algorithm>
#include <cmath>

namespace
{
	const char* PanelIdPrefix = "UIEditorPanel";

	bool IsSelfOrAncestor(const UWidget* Candidate, const UWidget* Widget)
	{
		for (const UWidget* It = Widget; It; It = It->GetParent())
			if (It == Candidate) return true;
		return false;
	}
}

////////////////////////////////////////////////////////////
// Palette
////////////////////////////////////////////////////////////

FUIPalettePanel::FUIPalettePanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Palette", PanelIdPrefix)
{
}

void FUIPalettePanel::DrawContent()
{
	Editor.HandleShortcuts();

	TArray<UClass*> Classes;
	GetDerivedClasses(UWidget::StaticClass(), Classes);

	for (UClass* Class : Classes)
	{
		// 생성할 수 없는 클래스, 베이스 패널, UUserWidget 계열은 뺀다.
		if (!Class->Constructor || Class == UPanelWidget::StaticClass() || Class->IsChildOf(UUserWidget::StaticClass()))
			continue;

		if (ImGui::Selectable(Class->Name.c_str()))
			Editor.AddWidget(Class);
	}
}

////////////////////////////////////////////////////////////
// Hierarchy
////////////////////////////////////////////////////////////

FUIHierarchyPanel::FUIHierarchyPanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Hierarchy", PanelIdPrefix)
{
}

void FUIHierarchyPanel::DrawContent()
{
	Editor.HandleShortcuts();

	if (UPanelWidget* Root = Editor.GetRoot())
		DrawNode(Root);
}

void FUIHierarchyPanel::DrawNode(UWidget* Widget)
{
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (!Panel || Panel->GetChildCount() == 0) Flags |= ImGuiTreeNodeFlags_Leaf;
	if (Widget == Editor.GetSelected()) Flags |= ImGuiTreeNodeFlags_Selected;

	const FString Name = Widget->WidgetName.empty() ? Widget->GetClass()->Name : Widget->WidgetName;
	const FString Label = Name + "##" + std::to_string(Widget->GetUUID());
	const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
	if (ImGui::IsItemClicked()) Editor.SetSelected(Widget);

	// 끌어서 다른 패널 아래로 옮긴다.
	if (Widget != Editor.GetRoot() && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("UI_WIDGET", &Widget, sizeof(UWidget*));
		ImGui::TextUnformatted(Name.c_str());
		ImGui::EndDragDropSource();
	}
	if (Panel && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("UI_WIDGET"))
		{
			UWidget* Dropped = *static_cast<UWidget* const*>(Payload->Data);
			if (!IsSelfOrAncestor(Dropped, Panel))   // 자기 자손 아래로는 못 옮긴다
				Editor.RequestReparent(Dropped, Panel);
		}
		ImGui::EndDragDropTarget();
	}

	if (bOpen)
	{
		if (Panel)
			for (UWidget* Child : Panel->GetChildren())
				DrawNode(Child);
		ImGui::TreePop();
	}
}

////////////////////////////////////////////////////////////
// Canvas
////////////////////////////////////////////////////////////

FUICanvasPanel::FUICanvasPanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Canvas", PanelIdPrefix)
{
}

void FUICanvasPanel::DrawContent()
{
	Editor.HandleShortcuts();

	// 텍스처가 아직 없어도 보였다고 알려야 다음 프레임에 미리보기가 그려진다.
	Editor.MarkCanvasVisible();

	FTexture2D* Preview = Editor.GetPreviewTexture();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	if (!Preview || !Preview->GetSRV() || Avail.x <= 0.0f || Avail.y <= 0.0f)
	{
		Editor.SetCanvasDragging(false);
		return;
	}

	const FVector2& PreviewSize = Editor.GetPreviewSize();
	ViewScale = std::min(Avail.x / PreviewSize.X, Avail.y / PreviewSize.Y);
	const ImVec2 Size(PreviewSize.X * ViewScale, PreviewSize.Y * ViewScale);

	CanvasMin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##UICanvas", Size);
	const bool bHovered = ImGui::IsItemHovered();

	ImDrawList* DL = ImGui::GetWindowDrawList();
	const ImVec2 CanvasMax(CanvasMin.x + Size.x, CanvasMin.y + Size.y);
	DL->AddImage(Preview->GetSRV(), CanvasMin, CanvasMax);
	DL->AddRect(CanvasMin, CanvasMax, IM_COL32(90, 98, 110, 255));

	const UWidget* Selected = Editor.GetSelected();
	if (Selected && Selected != Editor.GetRoot())
	{
		const ImU32 Accent = IM_COL32(255, 170, 40, 255);
		const FUIRect& R = Selected->GetCachedRect();
		DL->AddRect(ToScreen(R.X, R.Y), ToScreen(R.X + R.W, R.Y + R.H), Accent, 0.0f, 0, 2.0f);

		// 오른쪽 아래 크기 조절 손잡이
		const ImVec2 H = ToScreen(R.X + R.W, R.Y + R.H);
		DL->AddRectFilled(ImVec2(H.x - 5, H.y - 5), ImVec2(H.x + 5, H.y + 5), Accent);

		// 앵커: 부모 영역 안의 점(마름모) 또는 영역(사각형)
		if (const UPanelWidget* Parent = Selected->GetParent())
		{
			const FUIRect& P = Parent->GetCachedRect();
			const FVector4& A = Selected->Anchors;
			const ImVec2 A0 = ToScreen(P.X + P.W * A.X, P.Y + P.H * A.Y);
			const ImVec2 A1 = ToScreen(P.X + P.W * A.Z, P.Y + P.H * A.W);
			if (A.X == A.Z && A.Y == A.W)
				DL->AddQuad(ImVec2(A0.x, A0.y - 7), ImVec2(A0.x + 7, A0.y), ImVec2(A0.x, A0.y + 7), ImVec2(A0.x - 7, A0.y), Accent, 2.0f);
			else
				DL->AddRect(A0, A1, IM_COL32(255, 170, 40, 140));
		}
	}

	HandleInput(bHovered);
	Editor.SetCanvasDragging(DragMode != EDragMode::None);
}

UWidget* FUICanvasPanel::PickWidget(UWidget* Widget, float X, float Y) const
{
	if (!Widget || Widget->IsCollapsed() || Widget->Visibility == static_cast<int32>(EUIVisibility::Hidden))
		return nullptr;

	if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		TArray<UWidget*> Sorted;
		Panel->GetSortedChildren(Sorted);
		for (auto It = Sorted.rbegin(); It != Sorted.rend(); ++It)
			if (UWidget* Hit = PickWidget(*It, X, Y))
				return Hit;
	}
	// 루트는 빈 곳 클릭으로 선택되지 않게 한다.
	return (Widget != Editor.GetRoot() && Widget->GetCachedRect().Contains(X, Y)) ? Widget : nullptr;
}

bool FUICanvasPanel::IsOverResizeHandle(float X, float Y) const
{
	const UWidget* Selected = Editor.GetSelected();
	if (!Selected) return false;
	const FUIRect& R = Selected->GetCachedRect();
	const float Grab = 8.0f / ViewScale;   // 화면에서 8픽셀
	return std::abs(X - (R.X + R.W)) < Grab && std::abs(Y - (R.Y + R.H)) < Grab;
}

void FUICanvasPanel::HandleInput(bool bCanvasHovered)
{
	const ImVec2 Mouse = ImGui::GetMousePos();
	const float X = (Mouse.x - CanvasMin.x) / ViewScale;   // 디자인 좌표
	const float Y = (Mouse.y - CanvasMin.y) / ViewScale;

	if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		DragMode = EDragMode::None;
		if (IsOverResizeHandle(X, Y))
		{
			DragMode = EDragMode::Resize;
		}
		else
		{
			Editor.SetSelected(PickWidget(Editor.GetRoot(), X, Y));
			if (Editor.GetSelected())
				DragMode = EDragMode::Move;
		}

		if (UWidget* Selected = Editor.GetSelected())
		{
			DragStartX = X;
			DragStartY = Y;
			DragStartOffsets = Selected->Offsets;
			if (DragMode == EDragMode::Resize && Selected->bAutoSize)
			{
				// 자동 크기였다면 지금 보이는 크기에서 시작한다.
				const FUIRect& R = Selected->GetCachedRect();
				DragStartOffsets.Z = R.W;
				DragStartOffsets.W = R.H;
				Selected->bAutoSize = false;
			}
		}
	}

	// 되돌리기로 트리가 바뀌어 선택이 사라졌거나 마우스를 놓았으면 드래그를 끝낸다.
	if (!Editor.GetSelected() || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		DragMode = EDragMode::None;   // 놓는 순간 CommitUndoIfChanged가 스냅샷을 쌓는다
		return;
	}
	if (DragMode == EDragMode::None) return;

	float DX = X - DragStartX;
	float DY = Y - DragStartY;
	if (ImGui::GetIO().KeyShift)   // Shift: 8픽셀 격자에 맞춤
	{
		DX = std::round(DX / 8.0f) * 8.0f;
		DY = std::round(DY / 8.0f) * 8.0f;
	}
	ApplyDrag(DX, DY);
}

void FUICanvasPanel::ApplyDrag(float DX, float DY)
{
	UWidget* Selected = Editor.GetSelected();
	FVector4 O = DragStartOffsets;
	const FVector4& A = Selected->Anchors;
	const bool bStretchX = A.X != A.Z;
	const bool bStretchY = A.Y != A.W;

	if (DragMode == EDragMode::Move)
	{
		O.X += DX; if (bStretchX) O.Z -= DX;
		O.Y += DY; if (bStretchY) O.W -= DY;
	}
	else
	{
		// 늘어나는 축은 오른쪽/아래 여백이 줄고, 점 앵커 축은 폭/높이가 는다.
		O.Z += bStretchX ? -DX : DX;
		O.W += bStretchY ? -DY : DY;
	}
	Selected->Offsets = O;
}

////////////////////////////////////////////////////////////
// Details
////////////////////////////////////////////////////////////

FUIDetailsPanel::FUIDetailsPanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Details", PanelIdPrefix)
{
}

void FUIDetailsPanel::DrawContent()
{
	Editor.HandleShortcuts();

	UWidget* Selected = Editor.GetSelected();
	if (!Selected)
	{
		ImGui::TextDisabled("Select a widget in the canvas or hierarchy.");
		return;
	}

	ImGui::TextUnformatted(Selected->GetClass()->Name.c_str());

	struct FAnchorPreset { const char* Label; FVector4 Anchors; };
	static const FAnchorPreset Presets[] = {
		{ "Top Left", {0, 0, 0, 0} }, { "Top Center", {0.5f, 0, 0.5f, 0} }, { "Top Right", {1, 0, 1, 0} },
		{ "Center Left", {0, 0.5f, 0, 0.5f} }, { "Center", {0.5f, 0.5f, 0.5f, 0.5f} }, { "Center Right", {1, 0.5f, 1, 0.5f} },
		{ "Bottom Left", {0, 1, 0, 1} }, { "Bottom Center", {0.5f, 1, 0.5f, 1} }, { "Bottom Right", {1, 1, 1, 1} },
		{ "Top Stretch", {0, 0, 1, 0} }, { "Bottom Stretch", {0, 1, 1, 1} }, { "Fill", {0, 0, 1, 1} },
	};
	if (Selected != Editor.GetRoot() && ImGui::BeginCombo("Anchors", "Select Preset"))
	{
		for (const FAnchorPreset& Preset : Presets)
			if (ImGui::Selectable(Preset.Label))
				Editor.SetAnchorsKeepRect(Selected, Preset.Anchors);
		ImGui::EndCombo();
	}

	FPropertyEditor::DrawObjectProperties(Selected);
}
