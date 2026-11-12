# FBX SDK 팀원 설치 및 셋업

# 1. SDK 설치

[Autodesk 공식 다운로드](https://aps.autodesk.com/developer/overview/fbx-sdk)에서 **FBX SDK 2020.3.11 / Windows / VS2022**를 설치합니다. UAP, Python SDK, Extensions가 아닌 일반 C++ SDK입니다. 설치 경로는 자유이며, 설치를 끝낸 후 아래 스크립트를 실행합니다.

# 2. 셋업 실행

기본 경로에 설치했다면 `SetupFbxSdk.bat`을 더블클릭합니다. 관리자 권한은 필요하지 않습니다.

다른 경로라면 PowerShell에서 다음처럼 실행합니다(저장소 루트 기준).

```powershell
.\Setup\SetupFbxSdk.bat -SdkRoot 'D:\SDK\FBX\2020.3.11'
```

스크립트는 다음 작업을 합니다.

- 명시한 경로 → 사용자 환경 변수 → 현재 프로세스 환경 변수 → 기본 설치 경로 순서로 SDK 루트를 선택합니다.
- SDK Header의 버전과 x64 Debug/Release 동적 링크용 Library·DLL의 존재 및 빈 파일 여부를 확인합니다.
- 모든 검사가 성공한 뒤 사용자 환경 변수 `FBX_SDK_ROOT`를 저장합니다. 검증 실패 시 기존 값은 유지합니다.
- 시스템 PATH, 관리자 설정, SDK 파일, 엔진 코드는 변경하지 않습니다.

셋업 성공 후 저장소 루트의 **`SlnGenerator.bat`을 바로 실행합니다.** 생성 배치가 저장된 사용자 환경 변수를 직접 읽으므로 터미널·Codex를 재시작할 필요가 없습니다. Visual Studio에서 솔루션을 열어두었다면 생성 후 프로젝트 변경을 다시 로드합니다.

저장된 환경 변수가 오래된 경로를 가리키면 `-SdkRoot`로 새 경로를 명시하여 다시 실행합니다. 성공 시 같은 변수 값을 갱신하므로 여러 번 실행할 수 있습니다.

# 3. 설정 변경 없이 확인하기

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Setup\SetupFbxSdk.ps1 -ValidateOnly
```

다른 경로도 `-SdkRoot`와 함께 확인할 수 있습니다. 성공은 종료 코드 0, 실패는 1입니다. `-ExecutionPolicy Bypass`는 이 실행에만 적용하며 시스템 실행 정책을 저장·변경하지 않습니다.

# 4. 프로젝트 연결과 Git 공유 범위

`SetupFbxSdk.bat`은 SDK를 검증하고 사용자 환경 변수를 저장합니다. `SlnGenerator.bat`은 같은 스크립트의 `-GenerateProjects` 경로로 SDK를 다시 검증하고, 저장된 경로를 Premake에 전달하여 프로젝트를 생성합니다. 다른 작업 폴더에서 배치를 실행해도 저장소 루트를 사용합니다. 실패하면 종료 코드 1로 중단합니다. 자동 실행에서는 `SlnGenerator.bat --no-pause`로 마지막 대기를 생략할 수 있습니다.

생성된 **HitoriEditor**에는 Include 경로, `FBXSDK_SHARED`, `libfbxsdk.lib` 링크와 DLL 복사 규칙이 들어갑니다. Debug·Benchmark_Debug는 SDK debug, 나머지는 release를 사용합니다. 빌드 성공 후 해당 `libfbxsdk.dll`이 실행 파일 옆으로 복사됩니다. HitoriEngine·HitoriGame에는 SDK 의존성을 추가하지 않습니다. FBX 파일을 실제로 읽는 Importer 구현은 별도 단계입니다.

2020.3.11 VS2022 설치본에서 확인한 Library 위치는 `FBX_SDK_ROOT\lib\x64\debug`와 `release`입니다. 이전 안내 예시의 `lib\vs2022\x64`와 다릅니다.

팀에는 `SetupFbxSdk.bat`, `SetupFbxSdk.ps1`, `README_FBX.md`, 루트의 `SlnGenerator.bat`과 `premake5.lua` 변경을 함께 공유합니다. SDK 설치본·개인 설치 경로·생성 파일은 저장소에 추가하지 않습니다.
