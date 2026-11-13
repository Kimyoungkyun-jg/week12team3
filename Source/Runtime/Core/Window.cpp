#include "EnginePCH.h"

#include "Window.h"
#include "../resource.h"
#include "Input/InputSystem.h"
#include "Input/MouseCapture.h"

namespace
{
	// 등록 전에는 nullptr이며, 그 동안의 메시지는 창이 직접 처리한다.
	FWndProcHook GWndProcHook = nullptr;
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

	// 마우스 잠금 메시지는 ImGui 입력 상태에 섞이지 않게 훅보다 먼저 처리한다 (TEAM5). 잠금이 꺼져 있으면 아무 일도 하지 않는다.
	if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATEAPP && wParam == FALSE) || msg == WM_DESTROY)
	{
		FMouseCapture::Release(hWnd);
	}
	if (msg == WM_INPUT)
	{
		FMouseCapture::ProcessRawInput(hWnd, reinterpret_cast<HRAWINPUT>(lParam));
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT && FMouseCapture::IsActive(hWnd))
	{
		SetCursor(nullptr);
		return TRUE;
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

	// Alt를 누른 동안의 키(Alt 자신, Alt+P 등)는 WM_SYSKEY*로 온다. 받지 않으면 Alt 조합 단축키가 동작하지 않는다.
	// Alt+F4 같은 시스템 처리는 DefWindowProc에 그대로 넘긴다.
	case WM_SYSKEYDOWN:
		FInputSystem::OnKeyDown(wParam);
		return DefWindowProc(hWnd, msg, wParam, lParam);

	case WM_SYSKEYUP:
		FInputSystem::OnKeyUp(wParam);
		return DefWindowProc(hWnd, msg, wParam, lParam);

	// Alt를 눌렀다 떼면 창 메뉴 모드로 들어가 다음 키 입력을 먹는다. 메뉴바가 없으니 막는다.
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_KEYMENU)
			return 0;
		return DefWindowProc(hWnd, msg, wParam, lParam);

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
