#include "EnginePCH.h"
#include "Editor/HitoriEd/GamePackager.h"

#include <algorithm>

#include "Core/Misc/Paths.h"

FGamePackager::~FGamePackager()
{
	// 복사 중에 에디터를 닫으면 복사가 끝날 때까지 기다린다.
	if (CopyThread.joinable()) CopyThread.join();
	if (Process) CloseHandle(Process);
	if (OutputRead) CloseHandle(OutputRead);
}

bool FGamePackager::Start(const FString& Config)
{
	if (IsRunning()) return false;
	// 엔진(Package.bat, Resources)과 프로젝트(Assets 등)는 서로 다른 폴더일 수 있다.
	const std::filesystem::path Root = FPaths::EngineDir();
	std::error_code Ec;
	if (std::filesystem::exists(Root / "Package.bat", Ec) && std::filesystem::exists(Root / "premake5.lua", Ec))
		return StartBuildAndPackage(Root, Config);
	return StartCopyOnly(Root);
}

bool FGamePackager::StartBuildAndPackage(const std::filesystem::path& Root, const FString& Config)
{
	if (IsRunning()) return false;

	//Win32 API에서 커널 객체(파일, 프로세스, 스레드, 이벤트, 뮤텍스, 파이프 등)를 만들 때 넘기는 구조체로, 두 가지를 정합니다.

	//이 객체에 누가 접근할 수 있는지(보안 디스크립터)
	//이 객체의 핸들을 자식 프로세스가 물려받을 수 있는지(상속 여부)
	SECURITY_ATTRIBUTES Security{ sizeof(Security), nullptr, TRUE };
	HANDLE OutputWrite = nullptr;
	if (!CreatePipe(&OutputRead, &OutputWrite, &Security, 0)) return false;
	// 읽는 쪽 핸들은 자식에게 물려주지 않는다.
	SetHandleInformation(OutputRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA Startup{};
	Startup.cb = sizeof(Startup);
	Startup.dwFlags = STARTF_USESTDHANDLES;
	Startup.hStdOutput = OutputWrite;
	Startup.hStdError = OutputWrite;

	// 작업 디렉터리는 프로젝트 폴더이므로 엔진 폴더의 Package.bat을 절대 경로로 부르고 프로젝트 파일을 넘긴다.
	// cmd /c는 바깥 따옴표 한 쌍을 벗기므로 전체를 한 번 더 감싼다.
	const std::wstring BatchPath = (Root / L"Package.bat").wstring();
	const std::wstring ConfigW(Config.begin(), Config.end());
	std::wstring CommandLine = L"cmd.exe /c \"\"" + BatchPath + L"\" " + ConfigW + L" \"" + FPaths::ProjectFilePath().wstring() + L"\"\"";
	STARTUPINFOW StartupW{};
	StartupW.cb = sizeof(StartupW);
	StartupW.dwFlags = Startup.dwFlags;
	StartupW.hStdOutput = Startup.hStdOutput;
	StartupW.hStdError = Startup.hStdError;
	PROCESS_INFORMATION Info{};
	const BOOL bCreated = CreateProcessW(nullptr, CommandLine.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, Root.c_str(), &StartupW, &Info);
	// 쓰기 핸들은 자식만 갖고 있어야, 자식이 끝날 때 파이프가 닫힌다.
	CloseHandle(OutputWrite);
	if (!bCreated)
	{
		HTR_LOG(Error, "Failed to start Package.bat (error {})", GetLastError());
		CloseHandle(OutputRead);
		OutputRead = nullptr;
		return false;
	}
	CloseHandle(Info.hThread);
	Process = Info.hProcess;
	HTR_LOG(Info, "Packaging started ({})", Config);
	return true;
}

bool FGamePackager::StartCopyOnly(const std::filesystem::path& Root)
{
	wchar_t ExePath[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, ExePath, MAX_PATH);
	const std::filesystem::path GameExe = std::filesystem::path(ExePath).parent_path() / L"HitoriGame.exe";
	if (!std::filesystem::exists(GameExe))
	{
		HTR_LOG(Error, "Packaging canceled: no source tree to build from and no prebuilt HitoriGame.exe next to the editor.");
		return false;
	}

	const std::filesystem::path ProjectDir = FPaths::ProjectDir();
	const std::filesystem::path ProjectFile = FPaths::ProjectFilePath();
	const std::filesystem::path Out = ProjectDir / "Build" / "Package" / FPaths::FromUtf8(FPaths::ProjectName());
	bCopyDone = false;
	bCopyFailed = false;
	CopyResult = FPaths::ToUtf8(Out);
	HTR_LOG(Info, "Packaging with prebuilt HitoriGame.exe (no build) -> {}", CopyResult);

	// 콘텐츠가 100MB 넘게 복사되므로 에디터가 멈추지 않게 따로 돌린다. 로그는 메인 스레드의 Tick에서만 남긴다.
	CopyThread = std::thread([this, Root, ProjectDir, ProjectFile, GameExe, Out]
		{
			namespace fs = std::filesystem;
			std::error_code Ec;
			fs::remove_all(Out, Ec);
			fs::create_directories(Out, Ec);
			if (Ec || !fs::copy_file(GameExe, Out / L"HitoriGame.exe", fs::copy_options::overwrite_existing, Ec))
			{
				CopyResult = "HitoriGame.exe";
				bCopyFailed = true;
			}
			// 엔진 셰이더(Resources)와 엔진 콘텐츠(EngineContent)는 엔진 폴더에서, 프로젝트 콘텐츠는 프로젝트 폴더에서 가져온다.
			// 게임 exe는 옆에 있는 .hproject를 찾아 그 폴더를 프로젝트 폴더로 쓴다.
			const std::pair<std::filesystem::path, const char*> Folders[] =
			{
				{ Root, "Resources" }, { Root, "EngineContent" },
				{ ProjectDir, "Assets" }, { ProjectDir, "Scenes" }, { ProjectDir, "Scripts" }, { ProjectDir, "Config" },
			};
			for (const auto& [Base, Folder] : Folders)
			{
				if (bCopyFailed) break;
				if (!fs::exists(Base / Folder, Ec)) continue;
				fs::copy(Base / Folder, Out / Folder, fs::copy_options::recursive | fs::copy_options::overwrite_existing, Ec);
				if (Ec) { CopyResult = Folder; bCopyFailed = true; }
			}
			if (!bCopyFailed && !fs::copy_file(ProjectFile, Out / ProjectFile.filename(), fs::copy_options::overwrite_existing, Ec))
			{
				CopyResult = FPaths::ToUtf8(ProjectFile.filename());
				bCopyFailed = true;
			}
			bCopyDone = true;
		});
	return true;
}

void FGamePackager::Tick()
{
	if (CopyThread.joinable() && bCopyDone)
	{
		CopyThread.join();
		if (bCopyFailed) HTR_LOG(Error, "Packaging failed: could not copy {}", CopyResult);
		else HTR_LOG(Info, "Packaging finished: {}", CopyResult);
	}
	if (!Process) return;
	// 종료 여부를 먼저 본다. 끝났다면 출력은 이미 전부 파이프에 들어와 있으므로 아래에서 다 읽힌다.
	// DrainOutput이 먼저인 경우 생길 수 있는 문제
	// DrainOutput()으로 읽음 (이때까지의 출력)
	// 그 사이 자식이 마지막 줄을 쓰고 종료
	// 종료 확인 -> 끝났네 -> Finish()에서 파이프 닫음
	// 마지막 줄은 읽지 못하고 사라짐
	const bool bExited = WaitForSingleObject(Process, 0) == WAIT_OBJECT_0;
	DrainOutput();
	if (bExited) Finish();
}


void FGamePackager::DrainOutput()
{
	DWORD Available = 0;
	// ReadFile은 읽을 데이터가 없으면 
	// 메인 스레드에서 그냥 부르면 에디터가 멈추기 때문에 PeekNamedPipe로 지금 몇 바이트가 쌓여 있는지 먼저 보고, 그만큼만 읽음. 
	// 이름에 Named가 들어가지만 익명 파이프에도 사용 가능
	while (PeekNamedPipe(OutputRead, nullptr, 0, nullptr, &Available, nullptr) && Available > 0)
	{
		char Buffer[1024];
		DWORD Read = 0;
		if (!ReadFile(OutputRead, Buffer, std::min<DWORD>(Available, sizeof(Buffer)), &Read, nullptr) || Read == 0)
			break;
		Pending.append(Buffer, Read);
	}

	// 줄 단위로 Output Log에 넘긴다.
	size_t NewLine;
	while ((NewLine = Pending.find('\n')) != FString::npos)
	{
		FString Line = Pending.substr(0, NewLine);
		if (!Line.empty() && Line.back() == '\r') Line.pop_back();
		if (!Line.empty()) HTR_LOG(Info, "[Package] {}", Line);
		Pending.erase(0, NewLine + 1);
	}
}

void FGamePackager::Finish()
{
	if (!Pending.empty()) HTR_LOG(Info, "[Package] {}", Pending);
	Pending.clear();

	DWORD ExitCode = 1;
	GetExitCodeProcess(Process, &ExitCode);
	CloseHandle(Process);
	CloseHandle(OutputRead);
	Process = nullptr;
	OutputRead = nullptr;

	if (ExitCode == 0) HTR_LOG(Info, "Packaging finished: {}", FPaths::ToUtf8(FPaths::ProjectDir() / "Build" / "Package" / FPaths::FromUtf8(FPaths::ProjectName())));
	else HTR_LOG(Error, "Packaging failed (exit code {}). See the log above.", ExitCode);
}