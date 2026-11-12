#include "EnginePCH.h"
#include "BlendSpaceEditorTab.h"
#include "BlendSpaceEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/BlendSpacePlayPanel.h"

FBlendSpaceEditorTab::FBlendSpaceEditorTab(FBlendSpaceEditor& InEditor)
    : FDockedAssetEditorTab(InEditor, "BlendSpaceDockSpace", "BlendSpacePanelClass")
    , BlendSpaceEditor(InEditor)
{
    RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
    RegisterPanel(MakeUnique<FBlendSpacePlayPanel>(InEditor));
}

void FBlendSpaceEditorTab::BeforeRender()
{
    // 패널이 그려지지 않은 프레임에는 프리뷰 입력을 비운다.
    BlendSpaceEditor.SetViewportState(0, 0, {});
}

void FBlendSpaceEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
    ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

    ImGuiID ViewportId = InDockSpaceId;
    const ImGuiID ControlsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.30f, nullptr, &ViewportId);

    ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
    ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), ControlsId);
    ImGui::DockBuilderFinish(InDockSpaceId);

}
