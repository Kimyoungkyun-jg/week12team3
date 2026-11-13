#pragma once

#include "imgui.h"

namespace EditorUIScale
{
// 100% 배율 기준 픽셀 수치를 현재 모니터 배율로 바꾼다. 글자는 FontScaleDpi만큼 커지므로 고정 폭도 같이 키워야 잘리지 않는다.
inline float Px(float Pixels)
{
	return Pixels * ImGui::GetStyle().FontScaleDpi;
}

// 글자가 들어가는 버튼의 폭. 고정값 대신 쓰면 어떤 배율에서도 잘리지 않는다.
inline float ButtonWidth(const char* Label)
{
	return ImGui::CalcTextSize(Label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// 다음 콤보 팝업의 폭을 제한한다. 없으면 팝업이 가장 긴 항목에 맞춰 패널 밖까지 늘어난다.
// BeginCombo 바로 앞에서 부른다. 높이는 ImGui 기본(8줄)과 같다.
inline void LimitNextComboPopupWidth()
{
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float MaxWidth = ImGui::CalcItemWidth() > Px(360.0f) ? ImGui::CalcItemWidth() : Px(360.0f);
	const float MaxHeight = (ImGui::GetFontSize() + Style.ItemSpacing.y) * 8.0f - Style.ItemSpacing.y + Style.WindowPadding.y * 2.0f;
	ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(MaxWidth, MaxHeight));
}

// 다음 항목이 현재 줄에 들어갈 때만 이어 붙이고, 아니면 다음 줄로 내린다.
inline void SameLineIfFits(float NextWidth)
{
	const float RightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;
	if (ImGui::GetItemRectMax().x + Spacing + NextWidth <= RightEdge)
	{
		ImGui::SameLine();
	}
}
}
