#include "EnginePCH.h"

#include "Launch/EntryPoint.h"
#include "Core/Misc/Paths.h"
#include "Engine/GameEngine.h"

UClass* GetEngineClass()
{
	return UGameEngine::StaticClass();
}
// 패키지된 게임은 exe 옆의 .hproject를 쓴다. 개발 중에는 엔진 폴더의 기본 프로젝트를 쓴다.
std::filesystem::path ResolveProjectFile(HINSTANCE hInstance, const std::filesystem::path& FromCommandLine)
{
	(void)hInstance;
	if (!FromCommandLine.empty()) return FromCommandLine;

	wchar_t ExePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, ExePath, MAX_PATH))
	{
		const std::filesystem::path Found = FPaths::FindProjectFileIn(std::filesystem::path(ExePath).parent_path());
		if (!Found.empty()) return Found;
	}

	const std::filesystem::path Found = FPaths::FindProjectFileIn(FPaths::EngineDir());
	if (Found.empty())
		MessageBoxW(nullptr, L"No .hproject file found. Pass one on the command line: HitoriGame.exe Path/To/Game.hproject",
			L"Hitori", MB_OK | MB_ICONERROR);
	return Found;
}
