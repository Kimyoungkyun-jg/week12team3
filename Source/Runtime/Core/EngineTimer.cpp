#include "EnginePCH.h"
#include "EngineTimer.h"

#include <algorithm>

void EngineTimer::Init()
{
	PrevTime = Clock::now();
}

void EngineTimer::Tick()
{
	Clock::time_point Current = Clock::now();
	// 경과 시간은 double로 구해 clamp하고, 공개하는 DeltaTime만 float로 변환한다.
	const std::chrono::duration<double> Elapsed = Current - PrevTime;
	PrevTime = Current;

	const double Delta = std::min(Elapsed.count(), 0.1);
	DeltaTime = static_cast<float>(Delta);
	TotalTime += Delta;
}
