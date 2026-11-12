#include "EnginePCH.h"
#include "StaticMeshEditorTab.h"
#include "StaticMeshEditor.h"
#include "Panels/StaticMeshViewportPanel.h"
#include "Panels/StaticMeshInfoPanel.h"
#include "Panels/StaticMeshDetailsPanel.h"
#include "Panels/StaticMeshSettingsPanel.h"

FStaticMeshEditorTab::FStaticMeshEditorTab(FStaticMeshEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, MeshEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 애셋별 도킹 ID를 유지합니다.
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("StaticMeshDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("StaticMeshPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FStaticMeshViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FStaticMeshInfoPanel>(InEditor));
	Panels.Add(MakeUnique<FStaticMeshDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FStaticMeshSettingsPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FStaticMeshEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("Viewer"))
	{
		if (ImGui::MenuItem("Close Viewer"))
		{
			MeshEditor.Close();
		}
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
}

void FStaticMeshEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID RightId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.22f, nullptr, &ViewportId);
	// 오른쪽 패널을 위에서부터 Info 30%, Details 40%, Settings 30%로 배치합니다.
	ImGuiID RemainingId = RightId;
	const ImGuiID InfoId = ImGui::DockBuilderSplitNode(RemainingId, ImGuiDir_Up, 0.30f, nullptr, &RemainingId);
	ImGuiID DetailsId = RemainingId;
	const ImGuiID SettingsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Down, 3.0f / 7.0f, nullptr, &DetailsId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), InfoId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), SettingsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FStaticMeshEditorTab::OnRender(bool bActive)
{
	MeshEditor.SetViewportState(0, 0, {});
	if (MeshEditor.IsCloseRequested())
	{
		return;
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 제거하고 기본 레이아웃으로 시작합니다.
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

	for (auto& Panel : Panels)
	{
		// 분리된 패널은 부모 탭이 가려져도 독립적으로 표시합니다.
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
		}
	}
}
