#pragma once

#include "Core/Types.h"

// 엔진 종료 요청. UE의 CoreGlobals.h에 있는 같은 이름의 함수와 같은 역할이다.
// 어디서든 요청만 기록하고, 메인 루프(GuardedMain)가 다음 반복에서 확인해 빠져나간다.
void RequestEngineExit(const FString& Reason);
bool IsEngineExitRequested();
