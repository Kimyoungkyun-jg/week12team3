#pragma once

namespace WindowsCrashHandler
{
// Saved/Crashes 출력 위치를 준비하고 프로세스 전체에 미처리 예외 콜백을 등록한다.
// 필터에 도달한 예외는 해당 스레드에서 처리하며 로컬 미니덤프를 작성한다.
void Install();
}
