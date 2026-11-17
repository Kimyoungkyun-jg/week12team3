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
	std::wstring PowerShellPath;
	std::wstring PublishCommandPrefix;
	std::wstring PublishCommandSuffix;
	std::wstring PublishCommandLine;
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
			static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
			&ExceptionInfo,
			nullptr,
			nullptr);

		// CloseHandle을 호출하기 전에 덤프 작성 오류를 보관
		const DWORD Error = Written ? ERROR_SUCCESS : GetLastError();
		CloseHandle(DumpFile);

		if (Written)
		{
			if (!PublishCommandLine.empty() &&
				swprintf_s(PublishCommandLine.data(), PublishCommandLine.size() + 1, L"%ls%ls%ls",
					PublishCommandPrefix.c_str(), DumpPath.c_str(), PublishCommandSuffix.c_str()) >= 0)
			{
				STARTUPINFOW StartupInfo{};
				StartupInfo.cb = sizeof(StartupInfo);

				PROCESS_INFORMATION ProcessInfo{};
				if (CreateProcessW(
						PowerShellPath.c_str(), PublishCommandLine.data(),
						nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
						nullptr, nullptr, &StartupInfo, &ProcessInfo))
				{
					CloseHandle(ProcessInfo.hThread);
					CloseHandle(ProcessInfo.hProcess);
				}
				else
				{
					LogDumpError(L"CreateProcessW (PublishCrash)", GetLastError());
				}
			}
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

void WindowsCrashHandler::Install(const std::filesystem::path& ExecutablePath)
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

	const auto ScriptPath = ExecutablePath.parent_path() / L"PublishCrash.ps1";

	auto PdbPath = ExecutablePath;
	PdbPath.replace_extension(L".pdb");

	wchar_t SystemDirectory[MAX_PATH]{};
	const UINT Length = GetSystemDirectoryW(SystemDirectory, MAX_PATH);

	if (!ExecutablePath.empty() && Length > 0 && Length < MAX_PATH)
	{
		PowerShellPath = (std::filesystem::path(SystemDirectory) /
			L"WindowsPowerShell\\v1.0\\powershell.exe")
							 .wstring();

		PublishCommandPrefix =
			L"\"" + PowerShellPath +
			L"\" -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File \"" +
			ScriptPath.wstring() +
			L"\" -DumpPath \"";
		PublishCommandSuffix = L"\" -PdbPath \"" + PdbPath.wstring() + L"\"";
		PublishCommandLine.resize(PublishCommandPrefix.size() + DumpPath.size() + PublishCommandSuffix.size());
	}

	SetUnhandledExceptionFilter(&HandleUnhandledException);
}
