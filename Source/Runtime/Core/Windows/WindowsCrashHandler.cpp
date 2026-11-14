#include "EnginePCH.h"
#include "Core/Windows/WindowsCrashHandler.h"

#include <Windows.h>

namespace
{
	LONG WINAPI HandleUnhandledException(LPEXCEPTION_POINTERS ExceptionPointers)
	{
		if (ExceptionPointers && ExceptionPointers->ExceptionRecord && ExceptionPointers->ContextRecord)
		{
			OutputDebugStringA("[Hitori] Crash filter received the exception record and CPU context.\n");
		}
		else
		{
			OutputDebugStringA("[Hitori] Crash filter received incomplete context.\n");
		}

		return EXCEPTION_EXECUTE_HANDLER;
	}
}

void WindowsCrashHandler::Install()
{
	SetUnhandledExceptionFilter(&HandleUnhandledException);
}