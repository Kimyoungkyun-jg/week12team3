#include "EnginePCH.h"

#include "Window.h"
#include "../resource.h"
#include "Input/InputSystem.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace
{
	// 등록 전에는 nullptr이며, 그 동안의 메시지는 창이 직접 처리한다.
	FWndProcHook GWndProcHook = nullptr;

	// 기본 프레임의 크기 조절 테두리 두께. 창의 DPI에 맞춘 값이다.
	int GetFrameThicknessX(HWND hWnd)
	{
		const UINT Dpi = GetDpiForWindow(hWnd);
		return GetSystemMetricsForDpi(SM_CXFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi);
	}

	int GetFrameThicknessY(HWND hWnd)
	{
		const UINT Dpi = GetDpiForWindow(hWnd);
		return GetSystemMetricsForDpi(SM_CYFRAME, Dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, Dpi);
	}
}

void FWindow::SetWndProcHook(FWndProcHook Hook)
{
	GWndProcHook = Hook;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	FWindow* window = nullptr;

	if (msg == WM_NCCREATE)
	{
		CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
		window = reinterpret_cast<FWindow*>(cs->lpCreateParams);

		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
	}
	else
	{
		window = reinterpret_cast<FWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	// 훅을 받지 않는 창의 메시지는 ImGui 입력 상태에 섞이지 않게 바로 창이 처리한다.
	if ((!window || window->ReceivesWndProcHook()) &&
		GWndProcHook && GWndProcHook(hWnd, msg, wParam, lParam))
		return true;

	if (window)
		return window->HandleMessage(hWnd, msg, wParam, lParam);

	return DefWindowProc(hWnd, msg, wParam, lParam);   // return 0 대신
}

bool FWindow::Create(HINSTANCE hInstance, int InWidth, int InHeight, const wchar_t* Title, bool bBorderless)
{
	Width = InWidth;
	Height = InHeight;
	const wchar_t CLASS_NAME[] = L"EngineWindowClass";

	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON2));
	RegisterClassW(&wc);

	DWORD style = bBorderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;

	// 원하는 클라이언트 크기 -> 실제 윈도우 크기로 보정
	RECT rc = { 0, 0, Width, Height };
	AdjustWindowRect(&rc, style, FALSE);   // FALSE = 메뉴 없음
	int WindowWidth = rc.right - rc.left;
	int WindowHeight = rc.bottom - rc.top;


	hWnd = CreateWindowEx(
		0, CLASS_NAME, Title,
		style,
		bBorderless ? 0 : CW_USEDEFAULT, bBorderless ? 0 : CW_USEDEFAULT, WindowWidth, WindowHeight,
		nullptr, nullptr, hInstance, this);

	if (hWnd == nullptr)
		return false;

	// 초기화(스플래시)가 끝난 뒤 Show()로 띄운다.
	return true;
}

void FWindow::Show()
{
	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);
	SetForegroundWindow(hWnd);
}

void FWindow::EnableCustomTitleBar(FCaptionHitTest HitTest)
{
	if (!hWnd)
		return;

	bCustomTitleBar = true;
	CaptionHitTest = HitTest;

	// 창 스타일(WS_OVERLAPPEDWINDOW)은 그대로 둬서 그림자, 스냅, 최소화 애니메이션을 유지한다.
	// DWM 프레임을 클라이언트 쪽으로 1픽셀 넓혀야 기본 프레임을 없앤 뒤에도 그림자가 남는다.
	const MARGINS Margins{ 0, 0, 1, 0 };
	DwmExtendFrameIntoClientArea(hWnd, &Margins);

	// WM_NCCALCSIZE를 다시 받아 기본 제목 표시줄을 걷어낸다. 클라이언트 크기가 바뀌므로 WM_SIZE도 뒤따른다.
	SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT FWindow::HitTestCustomFrame(HWND hWnd, LPARAM lParam) const
{
	POINT Point{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
	ScreenToClient(hWnd, &Point);

	RECT Client;
	GetClientRect(hWnd, &Client);

	// 최대화 상태에서는 테두리로 크기를 바꿀 수 없다.
	if (!IsZoomed(hWnd))
	{
		const int BorderX = GetFrameThicknessX(hWnd);
		const int BorderY = GetFrameThicknessY(hWnd);
		const bool bLeft = Point.x < BorderX;
		const bool bRight = Point.x >= Client.right - BorderX;
		const bool bTop = Point.y < BorderY;
		const bool bBottom = Point.y >= Client.bottom - BorderY;

		if (bTop && bLeft) return HTTOPLEFT;
		if (bTop && bRight) return HTTOPRIGHT;
		if (bBottom && bLeft) return HTBOTTOMLEFT;
		if (bBottom && bRight) return HTBOTTOMRIGHT;
		if (bLeft) return HTLEFT;
		if (bRight) return HTRIGHT;
		if (bTop) return HTTOP;
		if (bBottom) return HTBOTTOM;
	}

	// HTCAPTION을 돌려주면 끌기, 더블클릭 최대화, 우클릭 시스템 메뉴를 Windows가 처리한다.
	if (CaptionHitTest && CaptionHitTest(Point.x, Point.y))
		return HTCAPTION;

	return HTCLIENT;
}

void FWindow::Destroy()
{
	if (!hWnd)
		return;

	DestroyWindow(hWnd);
	hWnd = nullptr;
}

void FWindow::ProcessMessage(bool& bIsRunning)
{
	MSG msg;
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if (msg.message == WM_QUIT) { bIsRunning = false; }
	}
}

LRESULT FWindow::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_NCCALCSIZE:
		if (!bCustomTitleBar || !wParam)
			return DefWindowProc(hWnd, msg, wParam, lParam);
		// 0을 돌려주면 창 전체가 클라이언트 영역이 된다.
		// 최대화된 창은 테두리 두께만큼 화면 밖으로 나가 있으므로 그만큼 안쪽으로 줄인다.
		if (IsZoomed(hWnd))
		{
			NCCALCSIZE_PARAMS* Params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
			const int BorderX = GetFrameThicknessX(hWnd);
			const int BorderY = GetFrameThicknessY(hWnd);
			Params->rgrc[0].left += BorderX;
			Params->rgrc[0].right -= BorderX;
			Params->rgrc[0].top += BorderY;
			Params->rgrc[0].bottom -= BorderY;
		}
		return 0;

	case WM_NCHITTEST:
		if (!bCustomTitleBar)
			return DefWindowProc(hWnd, msg, wParam, lParam);
		return HitTestCustomFrame(hWnd, lParam);

	case WM_LBUTTONDOWN:
		FInputSystem::OnMouseDown(EMouseButton::Left);
		break;
	case WM_LBUTTONUP:
		FInputSystem::OnMouseUp(EMouseButton::Left);
		break;
	case WM_RBUTTONDOWN:
		FInputSystem::OnMouseDown(EMouseButton::Right);
		break;
	case WM_RBUTTONUP:
		FInputSystem::OnMouseUp(EMouseButton::Right);
		break;
	case WM_MBUTTONDOWN:
		FInputSystem::OnMouseDown(EMouseButton::Middle);
		break;
	case WM_MBUTTONUP:
		FInputSystem::OnMouseUp(EMouseButton::Middle);
		break;
	case WM_XBUTTONDOWN:
	{
		int Button = GET_XBUTTON_WPARAM(wParam); // XBUTTON1 또는 XBUTTON2 매크로
		if (Button == XBUTTON1)
			FInputSystem::OnMouseDown(EMouseButton::Side1);
		else if (Button == XBUTTON2)
			FInputSystem::OnMouseDown(EMouseButton::Side2);
		break;
	}

	case WM_XBUTTONUP:
	{
		int Button = GET_XBUTTON_WPARAM(wParam); // XBUTTON1 또는 XBUTTON2 매크로
		if (Button == XBUTTON1)
			FInputSystem::OnMouseUp(EMouseButton::Side1);
		else if (Button == XBUTTON2)
			FInputSystem::OnMouseUp(EMouseButton::Side2);
		break;
	}

	case WM_MOUSEMOVE:
	{
		int MouseX = (int)(short)LOWORD(lParam);
		int MouseY = (int)(short)HIWORD(lParam);
		FInputSystem::OnMouseMove(MouseX, MouseY);
	}
	break;

	case WM_MOUSEWHEEL:
		FInputSystem::OnMouseWheelDelta(GET_WHEEL_DELTA_WPARAM(wParam));
		break;

	case WM_KEYDOWN:
		FInputSystem::OnKeyDown(wParam);
		break;

	case WM_KEYUP:
		FInputSystem::OnKeyUp(wParam);
		break;

	// Alt를 누른 상태의 키(Alt 자체 포함)는 WM_KEY* 대신 WM_SYSKEY*로 온다. Alt+P 같은 단축키를 위해 같이 기록한다.
	case WM_SYSKEYDOWN:
		FInputSystem::OnKeyDown(wParam);
		// Alt+F4(창 닫기)는 기본 처리로 넘긴다. 나머지는 넘기지 않아야 Alt가 시스템 메뉴를 열지 않는다
		if (wParam == VK_F4)
			return DefWindowProc(hWnd, msg, wParam, lParam);
		break;

	case WM_SYSKEYUP:
		FInputSystem::OnKeyUp(wParam);
		break;

	// Alt+문자 조합이 메뉴 단축키를 찾다 경고음을 내지 않도록 먹는다
	case WM_SYSCHAR:
		break;

	// 포커스를 잃으면 이후의 KeyUp/ButtonUp 메시지가 이 창으로 오지 않으므로
	// 눌린 상태가 그대로 남는다. 여기서 전부 비워준다.
	case WM_KILLFOCUS:
		FInputSystem::ClearAllStates();
		break;

	case WM_ACTIVATEAPP:
		if (wParam == FALSE)
			FInputSystem::ClearAllStates();
		break;

	// 포커스가 없는 동안 커서가 이동했어도 델타가 튀지 않도록 현재 위치로 맞춰준다.
	case WM_SETFOCUS:
	{
		POINT Cursor;
		if (GetCursorPos(&Cursor) && ScreenToClient(hWnd, &Cursor))
			FInputSystem::SyncMousePosition(Cursor.x, Cursor.y);
		break;
	}

	case WM_CLOSE:
		if (bQuitOnClose)
			return DefWindowProc(hWnd, msg, wParam, lParam);
		// 소유자가 다음 프레임에 정리하도록 요청만 남기고 창은 아직 파괴하지 않는다.
		bCloseRequested = true;
		break;

	case WM_DESTROY:
		if (bQuitOnClose)
			PostQuitMessage(0);
		// 파괴 뒤에 오는 메시지가 해제된 FWindow를 가리키지 않게 연결을 끊는다.
		SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
		break;

	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			break;

		Width = LOWORD(lParam);
		Height = HIWORD(lParam);

		if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)
		{
			if (!bIsInSizeMove)
				bIsResized = true;
		}
		break;

	case WM_ENTERSIZEMOVE:
		bIsInSizeMove = true;
		break;
	case WM_EXITSIZEMOVE:
	{
		RECT rc;
		GetClientRect(hWnd, &rc);
		Width = rc.right - rc.left;
		Height = rc.bottom - rc.top;

		bIsResized = true;
		bIsInSizeMove = false;
		break;
	}
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}
