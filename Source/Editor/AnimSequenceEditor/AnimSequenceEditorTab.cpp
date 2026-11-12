#include "EnginePCH.h"
#include "AnimSequenceEditorTab.h"
#include "AnimSequenceEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/AnimTimelinePanel.h"
#include "Panels/AnimSequenceDetailsPanel.h"
#include "Panels/AnimBrowserPanels.h"

FAnimSequenceEditorTab::FAnimSequenceEditorTab(FAnimSequenceEditor& InEditor)
    : FDockedAssetEditorTab(InEditor, "AnimSequenceDockSpace", "AnimSequencePanelClass")
    , AnimEditor(InEditor)
{
    RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
    RegisterPanel(MakeUnique<FAnimTimelinePanel>(InEditor));
    RegisterPanel(MakeUnique<FAnimSequenceDetailsPanel>(InEditor));
    RegisterPanel(MakeUnique<FAnimSkeletonTreePanel>(InEditor));
    RegisterPanel(MakeUnique<FAnimAssetBrowserPanel>(InEditor));
}

void FAnimSequenceEditorTab::BeforeRender()
{
    // 패널이 그려지지 않은 프레임에는 프리뷰 입력을 비운다.
    AnimEditor.SetViewportState(0, 0, {});
}

void FAnimSequenceEditorTab::DrawMenuBar()
{
    if (ImGui::BeginMenu("Viewer"))
    {
        if (ImGui::MenuItem("Save", "Ctrl+S", false, AnimEditor.IsDirty()))
        {
            AnimEditor.Save();
        }
        if (ImGui::MenuItem("Revert to Saved", nullptr, false, AnimEditor.IsDirty()))
        {
            AnimEditor.RevertToSaved();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, AnimEditor.CanUndoNotifyEdit()))
        {
            AnimEditor.UndoNotifyEdit();
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, AnimEditor.CanRedoNotifyEdit()))
        {
            AnimEditor.RedoNotifyEdit();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close Viewer"))
        {
            AnimEditor.Close();
        }
        ImGui::EndMenu();
    }

}

void FAnimSequenceEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
    ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

    // Unreal 애니메이션 뷰어 배치: 왼쪽 Skeleton Tree, 가운데 뷰포트와 타임라인, 오른쪽 위 Details, 오른쪽 아래 Asset Browser
    ImGuiID ViewportId = InDockSpaceId;
    const ImGuiID SkeletonId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.18f, nullptr, &ViewportId);
    ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.28f, nullptr, &ViewportId);
    const ImGuiID BrowserId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Down, 0.45f, nullptr, &DetailsId);
    const ImGuiID TimelineId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Down, 0.25f, nullptr, &ViewportId);

    ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
    ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), TimelineId);
    ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
    ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), SkeletonId);
    ImGui::DockBuilderDockWindow(Panels[4]->GetWindowName(), BrowserId);
    ImGui::DockBuilderFinish(InDockSpaceId);
}

void FAnimSequenceEditorTab::OnRender(bool bActive)
{
    FDockedAssetEditorTab::OnRender(bActive);

    if (AnimEditor.IsCloseRequested())
    {
        return;
    }

    // 이번 프레임의 편집을 실행 취소 단계로 확정 (드래그/입력 중이면 놓을 때까지 미룬다)
    AnimEditor.CommitNotifyUndo(ImGui::IsAnyItemActive());

    // 단축키: 이 탭이 보이고 있을 때만 (레벨 에디터 등 다른 탭과 겹치지 않게)
    const ImGuiIO& IO = ImGui::GetIO();
    if (bActive && IO.KeyCtrl && !IO.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && AnimEditor.IsDirty())
        {
            AnimEditor.Save();
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (IO.KeyShift) AnimEditor.RedoNotifyEdit();
            else             AnimEditor.UndoNotifyEdit();
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
        {
            AnimEditor.RedoNotifyEdit();
        }
    }

    DrawUnsavedChangesPrompt();
}

void FAnimSequenceEditorTab::DrawUnsavedChangesPrompt()
{
    // 닫기가 막히면 에디터가 요청을 남긴다. 팝업은 이 탭의 ImGui 문맥에서 연다.
    if (AnimEditor.IsUnsavedPromptRequested())
    {
        AnimEditor.ClearUnsavedPromptRequest();
        ImGui::OpenPopup("Unsaved Notify Changes");
    }

    // 창 가운데에 띄운다
    const ImGuiViewport* Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Unsaved Notify Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s has unsaved notify changes.", AnimEditor.GetAssetPath().c_str());
        ImGui::TextDisabled("Closing without saving reverts them (the sequence is shared at runtime).");
        ImGui::Spacing();

        if (ImGui::Button("Save and Close"))
        {
            // 저장에 실패하면 닫지 않고 메시지를 보여 준다
            if (AnimEditor.Save())
            {
                ImGui::CloseCurrentPopup();
                AnimEditor.CloseWithoutPrompt();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and Close"))
        {
            AnimEditor.RevertToSaved();
            ImGui::CloseCurrentPopup();
            AnimEditor.CloseWithoutPrompt();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            ImGui::CloseCurrentPopup();
        }

        if (!AnimEditor.GetSaveMessage().empty() && AnimEditor.GetSaveMessage().rfind("Save failed", 0) == 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", AnimEditor.GetSaveMessage().c_str());
        }
        ImGui::EndPopup();
    }
}
