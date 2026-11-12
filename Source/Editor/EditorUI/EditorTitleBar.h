#pragma once

#include <functional>
#include <imgui.h>

class FWindow;

// 에디터 메인 창과 프로젝트 브라우저 창의 제목 표시줄. 한 번에 한 창에만 Install한다.
// Windows 기본 제목 표시줄을 없애고, 창 맨 위에 ImGui로 끌기용 띠를 그린다.
// 띠에는 왼쪽 위 로고 칸, 가운데 창 제목, 오른쪽 최소화·최대화·닫기 버튼이 있다.
//
// 에디터는 UE처럼 이 띠에 메뉴를 함께 그리고(DrawMenuBar), 바로 아래 줄에 탭 바를 둔다.
// 이때 로고는 띠와 탭 줄 두 줄 높이로 크게 그린다(SetLogoBottom).
namespace EditorTitleBar
{
	// 창의 기본 제목 표시줄을 끈다. ImGui 초기화 뒤에 한 번 호출한다.
	void Install(FWindow& Window);
	// 창을 닫기 전에 호출한다. 다른 창(프로젝트 브라우저 다음의 에디터)이 다시 Install할 수 있다.
	void Uninstall();

	// 왼쪽 위 칸에 그릴 로고. 정하지 않으면 빈 칸으로 둔다.
	void SetLogo(ImTextureID Logo);

	// 매 프레임, 메인 뷰포트를 쓰는 다른 창보다 먼저 호출한다.
	// 띠 높이만큼 뷰포트 작업 영역이 줄어 WorkPos를 쓰는 창은 그 아래에 놓인다. Install 전에는 아무것도 하지 않는다.
	void Draw();

	// Draw 뒤에 같은 프레임에서 호출한다. 띠의 로고 칸 오른쪽에 메뉴를 그린다.
	// DrawMenus 안에서 ImGui::BeginMenu를 부른다. Install 전이면 false를 돌려주고 아무것도 그리지 않는다.
	bool DrawMenuBar(const std::function<void()>& DrawMenus);

	// 로고를 띠 아래 탭 줄까지 두 줄 높이로 그린다. ScreenY는 탭 줄의 아래 끝(화면 좌표)이다.
	// 매 프레임 DrawMenuBar 전에 호출한다. 호출하지 않으면 로고는 띠 한 줄 안에만 그린다.
	void SetLogoBottom(float ScreenY);
	// 로고 칸의 오른쪽 끝(화면 좌표). 탭 바를 이 오른쪽에서 시작하게 할 때 쓴다.
	float GetLogoColumnMaxX();

	// FWindow가 WM_NCHITTEST에서 부른다. 클라이언트 좌표가 버튼과 메뉴를 뺀 띠 위면 true.
	bool IsCaption(int X, int Y);
}
