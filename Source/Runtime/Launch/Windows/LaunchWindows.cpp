#include "EnginePCH.h"

#include "Core/EngineStatics.h"

// Launch.cpp에 있다.
extern int32 GuardedMain(HINSTANCE hInstance);

// Windows 전용 진입점. 플랫폼 일만 하고 나머지는 GuardedMain에 맡긴다. UE의 LaunchWindows.cpp와 같은 자리다.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	const int32 ErrorLevel = GuardedMain(hInstance);

	// GuardedMain이 끝나면 엔진 루프와 모든 UObject가 정리된 상태여야 한다.
	if (FEngineStatics::TotalAllocationCount > 0)
		OutputDebugStringA("leaked UObjects\n");

	return ErrorLevel;
}
