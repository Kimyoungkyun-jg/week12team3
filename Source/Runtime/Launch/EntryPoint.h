#pragma once

#include "EnginePCH.h"
#include <Windows.h>
#include "LaunchEngineLoop.h"

// 실행 파일이 어떤 엔진 모드(UEngine 서브클래스)로 돌지 정한다. UE의 ini GameEngine= 설정 역할.
extern UClass* GetEngineClass();

// 실행 파일의 전체 경로. 긴 경로(longPathAware)에서도 잘리지 않도록 버퍼를 늘려 가며 다시 읽는다.
inline std::filesystem::path GetExecutablePath()
{
	std::wstring Buffer(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD Length = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
		if (Length == 0) return {};
		if (Length < Buffer.size()) { Buffer.resize(Length); return Buffer; }
		if (Buffer.size() >= 32768) return {};
		Buffer.resize(Buffer.size() * 2);
	}
}

// 배포 폴더의 exe는 작업 디렉터리가 exe 위치라는 보장이 없다.
// exe 옆에 Resources가 있으면 그곳을 콘텐츠 루트로 삼는다. 없으면 개발 환경이므로 debugdir을 그대로 쓴다.
inline void SetContentRootToExecutableDir()
{
	const std::filesystem::path ExePath = GetExecutablePath();
	if (ExePath.empty()) return;
	const std::filesystem::path ExeDir = ExePath.parent_path();
	if (std::filesystem::exists(ExeDir / L"Resources"))
		SetCurrentDirectoryW(ExeDir.c_str());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	SetContentRootToExecutableDir();
	{
		FEngineLoop EngineLoop;
		if (!EngineLoop.PreInit(hInstance, GetEngineClass())) return -1;

		while (EngineLoop.Tick())
		{
		}

		EngineLoop.Exit();
	}

	if (FEngineStatics::TotalAllocationCount > 0)
		OutputDebugStringA("leaked UObjects\n");

	return 0;
}
