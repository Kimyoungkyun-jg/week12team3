#pragma once

#include "Editor/EditorUI/EditorUIScale.h"

namespace AnimationEditorSplitter
{
	inline float Draw(const char* Id, bool bVertical, float Length)
	{
		const float Thickness = EditorUIScale::Px(6.0f);
		ImGui::InvisibleButton(Id, bVertical ? ImVec2(Thickness, Length) : ImVec2(Length, Thickness));
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const bool bActive = ImGui::IsItemHovered() || ImGui::IsItemActive();
		ImGui::GetWindowDrawList()->AddRectFilled(Min, Max,
			bActive ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered) : ImGui::GetColorU32(ImGuiCol_Separator));
		if (bActive) ImGui::SetMouseCursor(bVertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
		return ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)
			? (bVertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y) : 0.0f;
	}
}
