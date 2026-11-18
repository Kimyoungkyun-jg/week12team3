#include "EnginePCH.h"
#include "Core/Windows/WindowsCrashHandler.h"

#include <Windows.h>
#include <dbghelp.h>
#include <cwchar>
#include <filesystem>

#pragma comment(lib, "Dbghelp.lib")

namespace
{
	std::wstring CrashDirectory;
	std::wstring DumpPath;
	// 여러 스레드가 동시에 예외 필터에 들어오거나 필터가 재진입하는 경우 첫 덤프 작성만 허용
	volatile LONG DumpStarted = 0;

	void LogDumpError(const wchar_t* Operation, DWORD Error)
	{
		wchar_t Message[160]{};
		swprintf_s(Message, L"[Hitori] %ls failed (0x%08lX).\n", Operation, static_cast<unsigned long>(Error));
		OutputDebugStringW(Message);
	}

	// SetUnhandledExceptionFilter가 호출하는 콜백
	LONG WINAPI HandleUnhandledException(LPEXCEPTION_POINTERS ExceptionPointers)
	{
		if (InterlockedCompareExchange(&DumpStarted, 1, 0) != 0)
		{
			return EXCEPTION_EXECUTE_HANDLER;
		}

		SYSTEMTIME Time{};
		GetLocalTime(&Time);
		if (swprintf_s(DumpPath.data(), DumpPath.size() + 1,
				L"%ls\\Hitori-%04u-%02u-%02u_%02u-%02u-%lu.dmp",
				CrashDirectory.c_str(),
				unsigned(Time.wYear), unsigned(Time.wMonth), unsigned(Time.wDay),
				unsigned(Time.wHour), unsigned(Time.wMinute), GetCurrentProcessId()) < 0)
		{
			return EXCEPTION_EXECUTE_HANDLER;
		}

		// 같은 이름의 기존 덤프를 덮어쓰지 않고 새 파일일 때만 생성
		HANDLE DumpFile = CreateFileW(DumpPath.c_str(), GENERIC_WRITE, 0, nullptr, 
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (DumpFile == INVALID_HANDLE_VALUE)
		{
			LogDumpError(L"CreateFileW", GetLastError());
			return EXCEPTION_EXECUTE_HANDLER;
		}

		// 덤프에서 예외 당시 스레드와 CPU 문맥을 식별할 정보를 구성
		MINIDUMP_EXCEPTION_INFORMATION ExceptionInfo{};
		ExceptionInfo.ThreadId = GetCurrentThreadId();
		// Windows가 필터에 넘긴 예외 기록과 레지스터 문맥을 그대로 DbgHelp에 전달
		ExceptionInfo.ExceptionPointers = ExceptionPointers;
		ExceptionInfo.ClientPointers = FALSE;

		// 현재 프로세스를 대상으로 예외 스레드가 직접 .dmp를 작성
		// MiniDumpNormal은 스택 추적에 필요한 정보를, MiniDumpWithThreadInfo는 추가 스레드 상태를 포함
		const BOOL Written = MiniDumpWriteDump(
			GetCurrentProcess(),
			GetCurrentProcessId(),
			DumpFile,
			static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
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
			// 쓰기에 실패한 덤프를 제거
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

	CrashDirectory = CrashDir.wstring();
	DumpPath.resize(CrashDirectory.size() + 96);

	// 프로세스 전체의 미처리 예외 필터를 등록
	// 실제 호출은 예외가 난 스레드에서 발생
	SetUnhandledExceptionFilter(&HandleUnhandledException);
}
