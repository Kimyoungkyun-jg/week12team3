#include "EnginePCH.h"
#include "Core/Windows/WindowsCrashHandler.h"

#include <Windows.h>
#include <dbghelp.h>
#include <cwchar>
#include <filesystem>

#pragma comment(lib, "Dbghelp.lib")

namespace
{
	std::filesystem::path DumpPath;
	volatile LONG DumpStarted = 0;

	void LogDumpError(const wchar_t* Operation, DWORD Error)
	{
		wchar_t Message[160]{};
		swprintf_s(Message, L"[Hitori] %ls failed (0x%08lX).\n", Operation, static_cast<unsigned long>(Error));
		OutputDebugStringW(Message);
	}

	LONG WINAPI HandleUnhandledException(LPEXCEPTION_POINTERS ExceptionPointers)
	{
		if (InterlockedCompareExchange(&DumpStarted, 1, 0) != 0)
		{
			return EXCEPTION_EXECUTE_HANDLER;
		}

		HANDLE DumpFile = CreateFileW(DumpPath.c_str(), GENERIC_WRITE, 0, nullptr, 
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (DumpFile == INVALID_HANDLE_VALUE)
		{
			LogDumpError(L"CreateFileW", GetLastError());
			return EXCEPTION_EXECUTE_HANDLER;
		}

		MINIDUMP_EXCEPTION_INFORMATION ExceptionInfo{};
		ExceptionInfo.ThreadId = GetCurrentThreadId();
		ExceptionInfo.ExceptionPointers = ExceptionPointers;
		ExceptionInfo.ClientPointers = FALSE;

		const BOOL Written = MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			DumpFile,
			MiniDumpNormal,
			&ExceptionInfo,
			nullptr,
			nullptr);

		// CloseHandle을 호출하기 전에 덤프 작성 오류를 보관
		const DWORD Error = Written ? ERROR_SUCCESS : GetLastError();
		CloseHandle(DumpFile);

		if (Written)
		{
			OutputDebugStringW(L"[Hitori] Minidump written to:\n");
			OutputDebugStringW(DumpPath.c_str());
			OutputDebugStringW(L"\n");
		}
		else
		{
			DeleteFileW(DumpPath.c_str());
			LogDumpError(L"MiniDumpWriteDump", Error);
		}

		return EXCEPTION_EXECUTE_HANDLER;
	}
}

void WindowsCrashHandler::Install()
{
	std::error_code Error;
	const auto CrashDir = std::filesystem::current_path(Error) / L"Saved" / L"Crashes";
	if (Error)
	{
		OutputDebugStringW(L"[Hitori] Could not resolve the crash directory.\n");
		return;
	}

	std::filesystem::create_directories(CrashDir, Error);
	if (Error)
	{
		OutputDebugStringW(L"[Hitori] Could not create the crash directory.\n");
		return;
	}

	SYSTEMTIME Time{};
	GetSystemTime(&Time);

	wchar_t FileName[96]{};
	swprintf_s(FileName, L"Hitori-%04u%02u%02u-%02u%02u%02u-%03u-%lu.dmp",
		unsigned(Time.wYear), unsigned(Time.wMonth), unsigned(Time.wDay),
		unsigned(Time.wHour), unsigned(Time.wMinute), unsigned(Time.wSecond),
		unsigned(Time.wMilliseconds), GetCurrentProcessId());

	DumpPath = CrashDir / FileName;
	SetUnhandledExceptionFilter(&HandleUnhandledException);
}