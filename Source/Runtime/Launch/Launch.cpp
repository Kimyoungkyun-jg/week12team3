#include "EnginePCH.h"

#include "Launch/EntryPoint.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/CoreGlobals.h"

// 플랫폼과 무관한 프로그램의 일생. UE의 GuardedMain(Launch.cpp)과 같은 자리다.
// 순서는 PreInit, Init, Tick 반복, Exit이다.
int32 GuardedMain(HINSTANCE hInstance)
{
	FEngineLoop EngineLoop;

	// 어떤 경로로 이 함수를 빠져나가든 소멸자에서 Exit가 불린다.
	// EngineLoop보다 뒤에 선언했으므로 EngineLoop가 파괴되기 전에 먼저 실행된다.
	struct FEngineLoopCleanupGuard
	{
		FEngineLoop& Loop;
		~FEngineLoopCleanupGuard() { Loop.Exit(); }
	} CleanupGuard{ EngineLoop };

	if (!EngineLoop.PreInit(hInstance, GetEngineClass())) return -1;
	if (!EngineLoop.Init()) return -1;

	while (!IsEngineExitRequested())
	{
		EngineLoop.Tick();
	}

	return 0;
}
