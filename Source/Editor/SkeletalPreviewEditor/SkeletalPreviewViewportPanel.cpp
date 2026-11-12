#include "EnginePCH.h"
#include "SkeletalPreviewViewportPanel.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"

void FSkeletalPreviewViewportPanel::DrawContent()
{
    FEditorViewportClient* Client = Editor.GetViewportClient();
    const ImVec2 Size = ImGui::GetContentRegionAvail();
    if (!Client || Size.x < 1.0f || Size.y < 1.0f)
    {
        return;
    }

    const ImVec2 Origin = ImGui::GetCursorScreenPos();

    // 이미지 영역을 입력 항목으로 등록하여 드래그 중에도 카메라 조작을 유지한다
    ImGui::InvisibleButton("##AnimViewport", Size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (FTexture2D* Target = Client->GetColorTarget(); Target && Target->GetSRV())
    {
        ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin,
            ImVec2(Origin.x + Size.x, Origin.y + Size.y));
    }

    const bool bHovered = ImGui::IsItemHovered();
    if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
    {
        ImGui::SetWindowFocus();
    }

    const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        Editor.FocusMesh();
    }

    const ImGuiIO& IO = ImGui::GetIO();
    const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const FViewportPointerInput PointerInput
    {
        .MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
        .bHovered = bHovered,
        .bFocused = bFocused && !IO.AppFocusLost,
        .bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
        .bMouseDown = bMouseDown,
        .bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
    };

    const FViewportCameraInput CameraInput
    {
        .bCaptured = bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown,
        .bAllowWheel = bHovered && !bMouseDown,
        .bForward = ImGui::IsKeyDown(ImGuiKey_W),
        .bBackward = ImGui::IsKeyDown(ImGuiKey_S),
        .bRight = ImGui::IsKeyDown(ImGuiKey_D),
        .bLeft = ImGui::IsKeyDown(ImGuiKey_A),
        .bUp = ImGui::IsKeyDown(ImGuiKey_E),
        .bDown = ImGui::IsKeyDown(ImGuiKey_Q),
        .MouseX = IO.MouseDelta.x,
        .MouseY = IO.MouseDelta.y,
        // ImGui의 휠 단위를 카메라 이동 함수가 쓰는 Windows 휠 단위로 바꾼다
        .Wheel = IO.MouseWheel * 120.0f,
    };

    Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), CameraInput, PointerInput);
}
