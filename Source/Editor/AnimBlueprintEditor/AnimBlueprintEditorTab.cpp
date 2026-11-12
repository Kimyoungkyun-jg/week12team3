#include "EnginePCH.h"
#include "AnimBlueprintEditorTab.h"
#include "AnimBlueprintEditor.h"
#include "Core/Misc/Paths.h"

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/AnimBlueprintGraphPanel.h"
#include "Panels/AnimBlueprintDetailsPanel.h"

FAnimBlueprintEditorTab::FAnimBlueprintEditorTab(FAnimBlueprintEditor& InEditor)
    : FDockedAssetEditorTab(InEditor, "AnimBlueprintDockSpace", "AnimBlueprintPanelClass")
    , Editor(InEditor)
{
    RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
    RegisterPanel(MakeUnique<FAnimBlueprintGraphPanel>(InEditor));
    RegisterPanel(MakeUnique<FAnimBlueprintDetailsPanel>(InEditor));
}

void FAnimBlueprintEditorTab::BeforeRender()
{
    Editor.SetViewportState(0, 0, {});
}

void FAnimBlueprintEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
    ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

    ImGuiID GraphId = InDockSpaceId;

    const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(GraphId, ImGuiDir_Right, 0.25f, nullptr, &GraphId);
    const ImGuiID ViewportId = ImGui::DockBuilderSplitNode(GraphId, ImGuiDir_Left, 0.30f, nullptr, &GraphId);

    ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
    ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), GraphId);
    ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);

    ImGui::DockBuilderFinish(InDockSpaceId);
}
const char* FAnimBlueprintEditorTab::GetTabName() const
{
    DisplayName = FPaths::ToUtf8(FPaths::FromUtf8(Editor.GetAssetPath()).filename());
    DisplayName += Editor.HasUnsavedChanges() ? " * (AnimBlueprint)" : " (AnimBlueprint)";
    DisplayName += "###AssetEditor:" + Editor.GetAssetPath();
    return DisplayName.c_str();
}

void FAnimBlueprintEditorTab::DrawMenuBar()
{
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Save", "Ctrl+S"))
        {
            Editor.RequestSave();
        }

        ImGui::EndMenu();
    }

    FDockedAssetEditorTab::DrawMenuBar();
}

void FAnimBlueprintEditorTab::OnRender(bool bActive)
{
    if (bActive && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        Editor.RequestSave();
    }

    FDockedAssetEditorTab::OnRender(bActive);
}

void FAnimBlueprintEditorTab::OnCloseRequested()
{
    Editor.HandleTabClose(*this);
}
