#pragma once

#include <Windows.h>
#include <filesystem>

class UClass;

// 실행 파일(에디터, 게임)이 각자 구현하는 두 함수. WinMain과 GuardedMain은 Launch 쪽 cpp에 있다.

// 실행 파일이 어떤 엔진 모드(UEngine 서브클래스)로 돌지 정한다. UE의 ini GameEngine= 설정 역할.
extern UClass* GetEngineClass();

// 커맨드라인에서 .hproject를 못 찾았을 때 실행 파일이 프로젝트를 정한다.
// 에디터는 프로젝트 브라우저 창을 띄우고, 게임은 exe 옆이나 엔진 폴더에서 찾는다.
// FromCommandLine은 커맨드라인으로 받은 경로(없으면 빈 경로). 쓸 .hproject 경로를 돌려주고, 빈 경로면 종료한다.
extern std::filesystem::path ResolveProjectFile(HINSTANCE hInstance, const std::filesystem::path& FromCommandLine);
