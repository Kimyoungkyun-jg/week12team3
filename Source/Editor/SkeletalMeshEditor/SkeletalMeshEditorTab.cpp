#include "EnginePCH.h"
#include "SkeletalMeshEditorTab.h"
#include "SkeletalMeshEditor.h"
#include "Panels/SkeletalMeshViewportPanel.h"
#include "Panels/SkeletalMeshInfoPanel.h"
#include "Panels/SkeletalMeshDetailsPanel.h"
#include "Panels/SkeletalMeshSettingsPanel.h"
#include "Panels/BoneHierarchyPanel.h"

FSkeletalMeshEditorTab::FSkeletalMeshEditorTab(FSkeletalMeshEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, MeshEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 애셋별 도킹 ID를 유지합니다.
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("SkeletalMeshDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("SkeletalMeshPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FSkeletalMeshViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshInfoPanel>(InEditor));
	Panels.Add(MakeUnique<FBoneHierarchyPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshSettingsPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FSkeletalMeshEditorTab::DrawMenuBar()
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

void FSkeletalMeshEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID RightId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.22f, nullptr, &ViewportId);
	// 오른쪽 패널을 위에서부터 Info 20%, Hierarchy 30%, Details 30%, Settings 20%로 배치합니다.
	ImGuiID RemainingId = RightId;
	const ImGuiID InfoId = ImGui::DockBuilderSplitNode(RemainingId, ImGuiDir_Up, 0.20f, nullptr, &RemainingId);
	const ImGuiID HierarchyId = ImGui::DockBuilderSplitNode(RemainingId, ImGuiDir_Up, 3.0f / 8.0f, nullptr, &RemainingId);
	ImGuiID DetailsId = RemainingId;
	const ImGuiID SettingsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Down, 0.40f, nullptr, &DetailsId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), InfoId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), HierarchyId);
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[4]->GetWindowName(), SettingsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FSkeletalMeshEditorTab::OnRender(bool bActive)
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
