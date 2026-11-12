#include "EnginePCH.h"
#include "Editor/UIEditor/UIEditorTab.h"
#include "Editor/UIEditor/UIEditor.h"
#include "Editor/UIEditor/Panels/UIEditorPanels.h"

#include "UI/UserWidget.h"
#include "UObject/UObjectHash.h"

#include <format>

namespace
{
	// BuildDefaultLayout이 쓰는 패널 순서
	enum EUIEditorPanelIndex { Panel_Canvas, Panel_Palette, Panel_Hierarchy, Panel_Details };
}

FUIEditorTab::FUIEditorTab(FUIEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, UIEditor(InEditor)
{
	// 같은 위젯을 다시 열어도 배치가 유지되고, 다른 에셋 창과 섞이지 않도록 에셋 경로로 ID를 만든다
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("UIEditorDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("UIEditorPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FUICanvasPanel>(InEditor));      // Panel_Canvas
	Panels.Add(MakeUnique<FUIPalettePanel>(InEditor));     // Panel_Palette
	Panels.Add(MakeUnique<FUIHierarchyPanel>(InEditor));   // Panel_Hierarchy
	Panels.Add(MakeUnique<FUIDetailsPanel>(InEditor));     // Panel_Details
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FUIEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S")) UIEditor.Save();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor")) UIEditor.Close();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", "Ctrl+Z", false, UIEditor.CanUndo())) UIEditor.Undo();
		if (ImGui::MenuItem("Redo", "Ctrl+Y", false, UIEditor.CanRedo())) UIEditor.Redo();
		ImGui::Separator();
		if (ImGui::MenuItem("Delete", "Del", false, UIEditor.GetSelected() && UIEditor.GetSelected() != UIEditor.GetRoot()))
			UIEditor.DeleteSelected();
		ImGui::EndMenu();
	}

	// 앵커가 맞는지 다른 비율에서 확인한다.
	static const FVector2 Sizes[] = { {1920, 1080}, {1280, 720}, {2560, 1080}, {1080, 1920} };
	const FVector2& PreviewSize = UIEditor.GetPreviewSize();
	const FString SizeLabel = std::format("{}x{}###PreviewSize", (int)PreviewSize.X, (int)PreviewSize.Y);
	if (ImGui::BeginMenu(SizeLabel.c_str()))
	{
		for (const FVector2& Size : Sizes)
			if (ImGui::MenuItem(std::format("{}x{}", (int)Size.X, (int)Size.Y).c_str()))
				UIEditor.SetPreviewSize(Size);
		ImGui::EndMenu();
	}

	// 이 에셋으로 만들 C++ 클래스 (UUserWidget 자손)
	if (ImGui::BeginMenu(("Parent: " + UIEditor.GetParentClassName() + "###ParentClass").c_str()))
	{
		TArray<UClass*> Classes;
		GetDerivedClasses(UUserWidget::StaticClass(), Classes);
		Classes.Add(UUserWidget::StaticClass());
		for (UClass* Class : Classes)
			if (ImGui::MenuItem(Class->Name.c_str(), nullptr, Class->Name == UIEditor.GetParentClassName()))
				UIEditor.SetParentClassName(Class->Name);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
		{
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, Panel->IsOpen()))
			{
				Panel->SetOpen(!Panel->IsOpen());
			}
		}
		ImGui::EndMenu();
	}

	ImGui::TextDisabled("%s%s", UIEditor.GetFilePath().c_str(), UIEditor.IsDirty() ? " *" : "");
}

void FUIEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	// 왼쪽에 Palette(위)와 Hierarchy(아래), 가운데 Canvas, 오른쪽에 Details를 둔다.
	ImGuiID CanvasId = InDockSpaceId;
	ImGuiID LeftId = ImGui::DockBuilderSplitNode(CanvasId, ImGuiDir_Left, 0.16f, nullptr, &CanvasId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(CanvasId, ImGuiDir_Right, 0.24f, nullptr, &CanvasId);
	const ImGuiID PaletteId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Up, 0.35f, nullptr, &LeftId);

	ImGui::DockBuilderDockWindow(Panels[Panel_Canvas]->GetWindowName(), CanvasId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Palette]->GetWindowName(), PaletteId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Hierarchy]->GetWindowName(), LeftId);
	ImGui::DockBuilderDockWindow(Panels[Panel_Details]->GetWindowName(), DetailsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FUIEditorTab::OnRender(bool bActive)
{
	if (UIEditor.IsCloseRequested())
	{
		return;
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 제거하고 기본 레이아웃으로 시작한다.
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(DockSpaceId, Size);
			bLayoutInitialized = true;
		}

		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = PanelClassId;
		WindowClass.DockingAllowUnclassed = false;
		const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
		{
			ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
		}
	}

	bool bAnyRendered = false;
	for (auto& Panel : Panels)
	{
		// 분리된 패널은 부모 탭이 가려져도 독립적으로 표시한다.
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
			bAnyRendered = true;
		}
	}

	if (bAnyRendered)
	{
		// 트리를 순회하는 도중에는 바꾸지 않고, 패널을 모두 그린 뒤 옮긴다.
		UIEditor.FlushPendingReparent();
		UIEditor.CommitUndoIfChanged();
	}
}
