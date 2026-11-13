#pragma once

#include <chrono>


class EngineTimer
{
public:
	static void Init();
	static void Tick();

	static float GetDeltaTime() { return DeltaTime; };
	// 프로세스가 켜진 동안 누적한 시간(초). float으로 누적하면 값이 커질수록 덧셈이 반올림되어 시간이 어긋나므로 double로 둔다.
	// float이 필요한 곳(셰이더 상수 등)은 주기로 줄인 뒤에 변환한다.
	static double GetTotalTime() { return TotalTime; }
private:
	using Clock = std::chrono::high_resolution_clock;

	inline static Clock::time_point PrevTime;
	inline static float DeltaTime = 0.0f;
	inline static double TotalTime = 0.0;
};
