#include "EnginePCH.h"
#include "MouseCapture.h"
#include "InputSystem.h"

HWND FMouseCapture::mWindow{};
RECT FMouseCapture::mBounds{};
POINT FMouseCapture::mRestorePosition{};
int FMouseCapture::mVisibilityAdjustments{};

bool FMouseCapture::Update(HWND Window, const RECT& Bounds, bool Enabled)
{
	if (!Enabled || !Window || !::IsWindow(Window) || ::GetForegroundWindow() != Window || ::IsIconic(Window) || Bounds.right <= Bounds.left || Bounds.bottom <= Bounds.top)
	{
		Release(Window);
		return false;
	}
	if (mWindow != Window)
	{
		Release();
		const RAWINPUTDEVICE Device{ 0x01, 0x02, 0, Window };
		if (!::RegisterRawInputDevices(&Device, 1, sizeof(Device)))
		{
			return false;
		}
		mWindow = Window;
		mBounds = Bounds;
		::GetCursorPos(&mRestorePosition);
		FInputSystem::SetRelativeMouseMode(true);
		int Visibility{ ::ShowCursor(FALSE) };
		mVisibilityAdjustments = 1;
		while (Visibility >= 0)
		{
			Visibility = ::ShowCursor(FALSE);
			++mVisibilityAdjustments;
		}
		Center();
	}
	mBounds = Bounds;
	if (!::ClipCursor(&mBounds))
	{
		Release(Window);
		return false;
	}
	if (::GetForegroundWindow() == Window)
	{
		const POINT Position{ (mBounds.left + mBounds.right) / 2, (mBounds.top + mBounds.bottom) / 2 };
		::SetCursorPos(Position.x, Position.y);
	}
	::SetCursor(nullptr);
	return true;
}

void FMouseCapture::Release(HWND Window)
{
	if (!mWindow || (Window && Window != mWindow))
	{
		return;
	}
	const HWND PreviousWindow{ mWindow };
	mWindow = nullptr;
	::ClipCursor(nullptr);
	const RAWINPUTDEVICE Device{ 0x01, 0x02, RIDEV_REMOVE, nullptr };
	::RegisterRawInputDevices(&Device, 1, sizeof(Device));
	FInputSystem::SetRelativeMouseMode(false);
	while (mVisibilityAdjustments > 0)
	{
		::ShowCursor(TRUE);
		--mVisibilityAdjustments;
	}
	if (::GetForegroundWindow() == PreviousWindow && ::IsWindow(PreviousWindow))
	{
		::SetCursorPos(mRestorePosition.x, mRestorePosition.y);
	}
	::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
	POINT Position{};
	if (::GetCursorPos(&Position) && ::ScreenToClient(PreviousWindow, &Position))
	{
		FInputSystem::SyncMousePosition(Position.x, Position.y);
	}
}

bool FMouseCapture::IsActive(HWND Window)
{
	return mWindow && (!Window || Window == mWindow);
}

void FMouseCapture::Center()
{
	if (!mWindow)
	{
		return;
	}
	if (::GetForegroundWindow() != mWindow || ::IsIconic(mWindow))
	{
		Release();
		return;
	}
	POINT Position{ (mBounds.left + mBounds.right) / 2, (mBounds.top + mBounds.bottom) / 2 };
	::SetCursorPos(Position.x, Position.y);
	if (::ScreenToClient(mWindow, &Position))
	{
		FInputSystem::SyncMousePosition(Position.x, Position.y);
	}
	::SetCursor(nullptr);
}

void FMouseCapture::ProcessRawInput(HWND Window, HRAWINPUT InputHandle)
{
	if (!IsActive(Window) || ::GetForegroundWindow() != Window)
	{
		return;
	}
	RAWINPUT Input{};
	UINT Size{ sizeof(Input) };
	if (::GetRawInputData(InputHandle, RID_INPUT, &Input, &Size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) || Input.header.dwType != RIM_TYPEMOUSE || (Input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
	{
		return;
	}
	FInputSystem::OnRawMouseMove(Input.data.mouse.lLastX, Input.data.mouse.lLastY);
}
