#include "EnginePCH.h"
#include "Editor/EditorUI/EditorTitleBar.h"

#include "Core/Window.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace
{
	// 마지막으로 그린 프레임의 제목 표시줄 정보. 좌표는 메인 창 클라이언트 기준 픽셀이다.
	// WM_NCHITTEST는 ImGui 프레임 사이에 오므로 직전 프레임 값으로 판단한다. 둘 다 메인 스레드에서만 쓴다.
	struct FCaptionArea
	{
		float Height = 0.0f;
		float DragMinX = 0.0f;
		float DragMaxX = 0.0f;
		// 로고 칸과 버튼 사이에 메뉴가 있으면 그 구간은 끌기에서 뺀다. 메뉴가 없으면 LogoMaxX == MenusMaxX.
		float LogoMaxX = 0.0f;
		float MenusMaxX = 0.0f;
		// 메뉴 팝업이 열려 있으면 빈 곳 클릭이 팝업을 닫아야 하므로 끌기를 막는다.
		bool bBlocked = true;
	};

	constexpr const char* TitleBarWindowName = "##WindowTitleBar";
	// 메뉴 항목의 세로 여백. 에디터 메뉴(FEditorTabContainer::DrawTabMenu)와 같은 값이라 띠 높이와 메뉴 높이가 맞는다.
	constexpr float MenuFramePaddingY = 10.0f;
	constexpr ImGuiWindowFlags TitleBarFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;

	bool GInstalled = false;
	HWND GHwnd = nullptr;
	FString GTitle;
	ImTextureID GLogo = ImTextureID_Invalid;
	FCaptionArea GCaption;

	// 이번 프레임의 띠 영역(화면 좌표). Draw가 정하고 DrawMenuBar가 쓴다.
	ImRect GBarRect;
	// 로고를 탭 줄까지 두 줄로 그릴 때 그 아래 끝. SetLogoBottom을 한 번이라도 부르면 두 줄 모드가 된다.
	bool bLogoSpansRows = false;
	float GLogoBottom = 0.0f;
	float GLogoColumnMaxX = 0.0f;
	// 직전 프레임에 그린 메뉴의 오른쪽 끝. 창 제목이 메뉴와 겹치지 않게 할 때 쓴다.
	float GMenusMaxX = 0.0f;

	float RowHeight()
	{
		return ImFloor(ImGui::GetFontSize() + MenuFramePaddingY * 2.0f);
	}

	FString ReadWindowTitle(HWND Hwnd)
	{
		const int Length = GetWindowTextLengthW(Hwnd);
		if (Length <= 0) return {};
		std::wstring Wide(static_cast<size_t>(Length) + 1, L'\0');
		GetWindowTextW(Hwnd, Wide.data(), Length + 1);
		Wide.resize(static_cast<size_t>(Length));

		const int Size = WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), Length, nullptr, 0, nullptr, nullptr);
		FString Utf8(static_cast<size_t>(Size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), Length, Utf8.data(), Size, nullptr, nullptr);
		return Utf8;
	}

	enum class ECaptionButton { Minimize, Maximize, Close };

	// 버튼 하나를 그리고 눌렸으면 true. 모양은 Windows 11 제목 표시줄 버튼을 따라 선으로 그린다.
	bool CaptionButton(const char* Id, ECaptionButton Kind, const ImVec2& Min, const ImVec2& Size, bool bMaximized)
	{
		ImGui::SetCursorScreenPos(Min);
		const bool bPressed = ImGui::InvisibleButton(Id, Size);
		const bool bHovered = ImGui::IsItemHovered();
		const bool bHeld = ImGui::IsItemActive();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
		if (bHovered || bHeld)
		{
			ImU32 Background;
			if (Kind == ECaptionButton::Close)
				Background = bHeld ? IM_COL32(241, 112, 122, 255) : IM_COL32(232, 17, 35, 255);
			else
				Background = bHeld ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 24);
			DrawList->AddRectFilled(Min, Max, Background);
		}

		const ImU32 Color = ImGui::GetColorU32(ImGuiCol_Text);
		const float Thickness = ImMax(1.0f, ImFloor(ImGui::GetStyle().FontScaleDpi));
		// 아이콘 크기는 버튼 높이에 비례시켜 DPI 배율을 따라간다.
		const float Half = ImFloor(Size.y * 0.15f);
		const ImVec2 Center(ImFloor((Min.x + Max.x) * 0.5f) + 0.5f, ImFloor((Min.y + Max.y) * 0.5f) + 0.5f);

		switch (Kind)
		{
		case ECaptionButton::Minimize:
			DrawList->AddLine(ImVec2(Center.x - Half, Center.y), ImVec2(Center.x + Half, Center.y), Color, Thickness);
			break;
		case ECaptionButton::Maximize:
			if (bMaximized)
			{
				// 복원 모양: 뒤쪽 사각형의 보이는 두 변과 앞쪽 사각형.
				const float Offset = ImFloor(Half * 0.4f);
				DrawList->AddLine(ImVec2(Center.x - Half + Offset, Center.y - Half), ImVec2(Center.x + Half, Center.y - Half), Color, Thickness);
				DrawList->AddLine(ImVec2(Center.x + Half, Center.y - Half), ImVec2(Center.x + Half, Center.y + Half - Offset), Color, Thickness);
				DrawList->AddRect(ImVec2(Center.x - Half, Center.y - Half + Offset), ImVec2(Center.x + Half - Offset, Center.y + Half), Color, 0.0f, 0, Thickness);
			}
			else
			{
				DrawList->AddRect(ImVec2(Center.x - Half, Center.y - Half), ImVec2(Center.x + Half, Center.y + Half), Color, 0.0f, 0, Thickness);
			}
			break;
		case ECaptionButton::Close:
			DrawList->AddLine(ImVec2(Center.x - Half, Center.y - Half), ImVec2(Center.x + Half, Center.y + Half), Color, Thickness);
			DrawList->AddLine(ImVec2(Center.x + Half, Center.y - Half), ImVec2(Center.x - Half, Center.y + Half), Color, Thickness);
			break;
		}
		return bPressed;
	}
}

void EditorTitleBar::Install(FWindow& Window)
{
	GHwnd = Window.GetHandle();
	GTitle = ReadWindowTitle(GHwnd);
	Window.EnableCustomTitleBar(&EditorTitleBar::IsCaption);
	GInstalled = true;
}

void EditorTitleBar::Uninstall()
{
	GInstalled = false;
	GHwnd = nullptr;
	GTitle.clear();
	GLogo = ImTextureID_Invalid;
	GCaption = FCaptionArea{};
	GBarRect = ImRect();
	bLogoSpansRows = false;
	GLogoBottom = 0.0f;
	GLogoColumnMaxX = 0.0f;
	GMenusMaxX = 0.0f;
}

void EditorTitleBar::SetLogo(ImTextureID Logo)
{
	GLogo = Logo;
}

void EditorTitleBar::SetLogoBottom(float ScreenY)
{
	bLogoSpansRows = true;
	GLogoBottom = ScreenY;
}

float EditorTitleBar::GetLogoColumnMaxX()
{
	return GLogoColumnMaxX;
}

void EditorTitleBar::Draw()
{
	if (!GInstalled) return;

	// 메인 뷰포트 맨 위에 붙는 띠 창으로 만든다. 이 높이만큼 뷰포트 작업 영역(WorkPos, WorkSize)이 줄어들어
	// 그 아래에 놓이는 창(에디터의 탭 바와 도킹 영역, 브라우저 본문)이 자동으로 밀려 내려간다.
	// 띠 전체를 메뉴 바로 만들어 두고, 에디터는 DrawMenuBar에서 그 안에 메뉴를 채운다.
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const float Height = RowHeight();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, MenuFramePaddingY));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetColorU32(ImGuiCol_TitleBg));
	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetColorU32(ImGuiCol_TitleBg));
	const bool bVisible = ImGui::BeginViewportSideBar(TitleBarWindowName, MainViewport, ImGuiDir_Up, Height, TitleBarFlags);
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);

	if (!bVisible)
	{
		GCaption.bBlocked = true;
		ImGui::End();
		return;
	}

	ImGuiWindow* Window = ImGui::GetCurrentWindow();
	const ImRect Bar = Window->Rect();
	GBarRect = Bar;
	// 띠 전체가 메뉴 바 영역이라 기본 클립 영역(내용 영역)은 비어 있다. 로고, 제목, 버튼은 띠 전체에 그린다.
	ImGui::PushClipRect(Bar.Min, Bar.Max, false);

	// 왼쪽 위 정사각형 칸은 로고 자리다. 두 줄 모드면 탭 줄 아래 끝까지가 한 변이고, 아니면 띠 높이가 한 변이다.
	// 두 줄 모드의 로고는 DrawMenuBar에서 그린다. 로고가 없으면 빈 칸으로 둔다.
	const float Column = (bLogoSpansRows && GLogoBottom > Bar.Max.y) ? ImFloor(GLogoBottom - Bar.Min.y) : Height;
	GLogoColumnMaxX = Bar.Min.x + Column;
	if (!bLogoSpansRows && GLogo != ImTextureID_Invalid)
	{
		const float Inset = ImFloor(Height * 0.15f);
		Window->DrawList->AddImage(GLogo,
			ImVec2(Bar.Min.x + Inset, Bar.Min.y + Inset),
			ImVec2(GLogoColumnMaxX - Inset, Bar.Max.y - Inset));
	}

	const ImVec2 ButtonSize(ImFloor(Height * 1.4f), Height);
	const float ButtonsMinX = Bar.Max.x - ButtonSize.x * 3.0f;
	// 메뉴는 이 프레임 뒤쪽(DrawMenuBar)에서 그리므로 직전 프레임의 메뉴 끝을 쓴다.
	const float MenusMaxX = ImMax(GLogoColumnMaxX, GMenusMaxX);

	// 창 제목은 가운데에 두되, 로고나 메뉴, 버튼과 겹치면 메뉴 바로 뒤로 민다. 그래도 자리가 없으면 그리지 않는다.
	if (!GTitle.empty())
	{
		const ImVec2 TextSize = ImGui::CalcTextSize(GTitle.c_str());
		const float Spacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
		float TextX = ImFloor((Bar.Min.x + Bar.Max.x - TextSize.x) * 0.5f);
		TextX = ImMax(TextX, MenusMaxX + Spacing);
		if (TextX + TextSize.x <= ButtonsMinX - Spacing)
		{
			const float TextY = ImFloor(Bar.Min.y + (Height - TextSize.y) * 0.5f);
			Window->DrawList->AddText(ImVec2(TextX, TextY), ImGui::GetColorU32(ImGuiCol_TextDisabled), GTitle.c_str());
		}
	}

	// 버튼은 바로 실행하지 않고 메시지로 보낸다. 프레임 도중에 창 크기가 바뀌지 않고, 닫기는 평소의 WM_CLOSE 흐름을 탄다.
	const bool bMaximized = IsZoomed(GHwnd) != FALSE;
	ImGui::PushID("##TitleBarButtons");
	if (CaptionButton("Minimize", ECaptionButton::Minimize, ImVec2(ButtonsMinX, Bar.Min.y), ButtonSize, bMaximized))
		PostMessageW(GHwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
	if (CaptionButton("Maximize", ECaptionButton::Maximize, ImVec2(ButtonsMinX + ButtonSize.x, Bar.Min.y), ButtonSize, bMaximized))
		PostMessageW(GHwnd, WM_SYSCOMMAND, bMaximized ? SC_RESTORE : SC_MAXIMIZE, 0);
	if (CaptionButton("Close", ECaptionButton::Close, ImVec2(ButtonsMinX + ButtonSize.x * 2.0f, Bar.Min.y), ButtonSize, bMaximized))
		PostMessageW(GHwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
	ImGui::PopID();
	ImGui::PopClipRect();

	// 로고 칸과, 메뉴 끝부터 버튼 앞까지의 빈 곳으로 창을 끈다.
	// ImGui 좌표는 화면 기준이고 메인 뷰포트 위치가 클라이언트 원점이다.
	GCaption.Height = Bar.Max.y - MainViewport->Pos.y;
	GCaption.DragMinX = Bar.Min.x - MainViewport->Pos.x;
	GCaption.LogoMaxX = GLogoColumnMaxX - MainViewport->Pos.x;
	GCaption.MenusMaxX = MenusMaxX - MainViewport->Pos.x;
	GCaption.DragMaxX = ButtonsMinX - MainViewport->Pos.x;
	GCaption.bBlocked = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

	ImGui::End();
}

bool EditorTitleBar::DrawMenuBar(const std::function<void()>& DrawMenus)
{
	if (!GInstalled) return false;

	// Draw가 만든 띠 창에 이어서 그린다. 같은 프레임에 같은 이름으로 Begin하면 내용이 덧붙는다.
	const bool bVisible = ImGui::Begin(TitleBarWindowName, nullptr, TitleBarFlags);
	if (bVisible && ImGui::BeginMenuBar())
	{
		// 메뉴는 로고 칸 바로 오른쪽에서 시작한다. 커서를 옮기기만 하면 메뉴가 하나도 없는 프레임에
		// ImGui가 "SetCursorPos로 경계를 넓혔다"고 assert하므로, 그 폭만큼 빈 항목(Dummy)을 넣어 민다.
		const float LeadingWidth = GLogoColumnMaxX - ImGui::GetCursorScreenPos().x;
		if (LeadingWidth > 0.0f) ImGui::Dummy(ImVec2(LeadingWidth, 0.0f));
		if (DrawMenus) DrawMenus();
		GMenusMaxX = ImGui::GetCursorScreenPos().x;
		ImGui::EndMenuBar();

		const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
		GCaption.MenusMaxX = ImMax(GLogoColumnMaxX, GMenusMaxX) - MainViewport->Pos.x;
		GCaption.bBlocked = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	}

	// 두 줄 모드의 로고는 아래쪽 반이 탭 줄 위에 걸치므로 맨 앞 그리기 목록에 그린다.
	// 탭 바는 로고 칸 오른쪽에서 시작하므로(FEditorTabContainer::DrawTabs) 탭과 겹치지 않는다.
	if (bVisible && bLogoSpansRows && GLogo != ImTextureID_Invalid && GLogoBottom > GBarRect.Max.y)
	{
		const float Size = GLogoColumnMaxX - GBarRect.Min.x;
		const float Inset = ImFloor(Size * 0.1f);
		ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->AddImage(GLogo,
			ImVec2(GBarRect.Min.x + Inset, GBarRect.Min.y + Inset),
			ImVec2(GLogoColumnMaxX - Inset, GBarRect.Min.y + Size - Inset));
	}

	ImGui::End();
	return true;
}

bool EditorTitleBar::IsCaption(int X, int Y)
{
	if (!GInstalled || GCaption.bBlocked) return false;
	const float PointX = static_cast<float>(X);
	const float PointY = static_cast<float>(Y);
	if (PointY < 0.0f || PointY >= GCaption.Height) return false;
	const bool bOnLogo = PointX >= GCaption.DragMinX && PointX < GCaption.LogoMaxX;
	const bool bAfterMenus = PointX >= GCaption.MenusMaxX && PointX < GCaption.DragMaxX;
	return bOnLogo || bAfterMenus;
}
