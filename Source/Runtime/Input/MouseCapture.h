#pragma once

#include <Windows.h>

enum class ECursorMode
{
	Free,
	Locked,
};

class FMouseCapture final
{
public:
	static bool Update(HWND Window, const RECT& Bounds, bool Enabled);
	static void Release(HWND Window = nullptr);
	static bool IsActive(HWND Window = nullptr);
	static void Center();
	static void ProcessRawInput(HWND Window, HRAWINPUT InputHandle);

private:
	static HWND mWindow;
	static RECT mBounds;
	static POINT mRestorePosition;
	static int mVisibilityAdjustments;
};
