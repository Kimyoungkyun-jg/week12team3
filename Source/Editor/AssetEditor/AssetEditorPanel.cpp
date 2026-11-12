#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"

FAssetEditorPanelBase::FAssetEditorPanelBase(const char* InName, const FString& InWindowId)
	: PanelName(InName)
	, WindowName(PanelName + "###" + InWindowId)
{
}

void FAssetEditorPanelBase::OnRender()
{
	// 같은 탭의 패널끼리만 도킹되도록 탭이 정한 클래스를 지정합니다.
	ImGuiWindowClass WindowClass;
	WindowClass.ClassId = DockClassId;
	WindowClass.DockingAllowUnclassed = false;
	WindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
	ImGui::SetNextWindowClass(&WindowClass);
	ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin(GetWindowName(), nullptr, GetWindowFlags()))
	{
		DrawContent();
	}
	ImGui::End();
}

bool FAssetEditorPanelBase::IsDetached(ImGuiID DockSpaceId) const
{
	const ImGuiWindow* Window = ImGui::FindWindowByName(GetWindowName());
	return Window && (!Window->DockNode || ImGui::DockNodeGetRootNode(Window->DockNode)->ID != DockSpaceId);
}
