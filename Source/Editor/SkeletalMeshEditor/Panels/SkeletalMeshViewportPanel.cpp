#include "EnginePCH.h"
#include "SkeletalMeshViewportPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"

FSkeletalMeshViewportPanel::FSkeletalMeshViewportPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Viewport")
{
}

void FSkeletalMeshViewportPanel::DrawContent()
{

	int32 Mode = static_cast<int32>(Editor.GetGizmoMode());
	ImGui::SetNextItemWidth(300.0f);
	
	constexpr const char* GizmoModeNames[] = { "Location", "Rotation", "Scale" };
	if (ImGui::Combo("##GizmoMode", &Mode, GizmoModeNames, IM_ARRAYSIZE(GizmoModeNames)))
	{
		Editor.SetGizmoMode(static_cast<EGizmoMode>(Mode));
	}

	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
	{
		return;
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();

	// 이미지 영역을 입력 항목으로 등록하여 드래그 중에도 카메라 조작을 유지합니다.
	ImGui::InvisibleButton("##SkeletalMeshViewport", Size,
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


	// 분리된 플랫폼 창의 입력도 받을 수 있도록 ImGui 입력을 사용합니다.
	//
	// TODO: 여기서도 FInputSystem을 사용할 수 있도록, 구조 개선하기
	// ImGui가 생성하는 HWND에 직접 WndProc을 연결하고 FInputSystem에 전달시켜야함

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

	FViewportCameraInput Input
	{
		.bCaptured		= bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !Editor.IsGizmoDragging(),
		.bAllowWheel	= bHovered && !bMouseDown && !Editor.IsGizmoDragging(),
		.bForward		= ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward		= ImGui::IsKeyDown(ImGuiKey_S),
		.bRight			= ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft			= ImGui::IsKeyDown(ImGuiKey_A),
		.bUp			= ImGui::IsKeyDown(ImGuiKey_E),
		.bDown			= ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX			= IO.MouseDelta.x,
		.MouseY			= IO.MouseDelta.y,

		// ImGui의 휠 단위를 기존 카메라 이동 함수가 사용하는 Windows 휠 단위로 변환합니다.
		.Wheel			= IO.MouseWheel * 120.0f,
	};

	// 키보드로 Gizmo 키 변경
	if (!bCameraDragging)
	{
		if (ImGui::IsKeyDown(ImGuiKey_W))
		{
			Editor.SetGizmoMode(EGizmoMode::Location);
		}
		if (ImGui::IsKeyDown(ImGuiKey_E))
		{
			Editor.SetGizmoMode(EGizmoMode::Rotation);
		}
		if (ImGui::IsKeyDown(ImGuiKey_R))
		{
			Editor.SetGizmoMode(EGizmoMode::Scale);
		}

		// 뷰포트에서 Space를 누를 때마다 이동, 회전, 크기 모드를 순환합니다.
		if (bFocused && bHovered && !IO.WantTextInput && !IO.AppFocusLost &&
			!Editor.IsGizmoDragging() && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		{
			const int32 NextMode = (static_cast<int32>(Editor.GetGizmoMode()) + 1) % IM_ARRAYSIZE(GizmoModeNames);
			Editor.SetGizmoMode(static_cast<EGizmoMode>(NextMode));
		}
	}

	// 렌더 타깃 변경은 Editor의 다음 Render에서 수행합니다.
	uint32 Width = static_cast<uint32>(Size.x);
	uint32 Height = static_cast<uint32>(Size.y);
	Editor.SetViewportState(Width, Height, Input, PointerInput);
}
