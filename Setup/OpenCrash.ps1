# 사용자가 전달한 덤프를 WinDbg로 열고 빌드에 맞는 심볼과 소스 검색 경로를 설정

param
(
    [Parameter(Mandatory)]
    [string] $DumpPath,
    [string] $SymbolStore = '\\172.21.10.122\Symbols'
)

# 파일이나 저장소를 읽지 못하면 중간 설정으로 디버거를 열지 않고 오류 반환
$ErrorActionPreference = 'Stop'

$dump = Get-Item -LiteralPath $DumpPath
if ($dump.PSIsContainer -or $dump.Extension -ne '.dmp') 
{
    throw "Select a .dmp file: $DumpPath"
}

$storeDirectory = Get-Item -LiteralPath $SymbolStore
if (-not $storeDirectory.PSIsContainer)
{
    throw "Select a symbol store directory: $SymbolStore"
}

$SymbolStore = $storeDirectory.FullName
# WinDbg의 srv*으로 심볼 저장소를 검색하도록 설정
$symbolPath = "srv*$SymbolStore"
if ($SymbolStore.StartsWith('\\'))
{
    $symbolCache = Join-Path $env:LOCALAPPDATA 'Hitori\SymbolCache'
    New-Item -ItemType Directory -Path $symbolCache -Force | Out-Null
    $symbolPath = "srv*$symbolCache*$SymbolStore"
}

# PATH에서 최신 WinDbg 실행 파일을 찾음
$modernWinDbg = Get-Command WinDbgX.exe -CommandType Application -ErrorAction SilentlyContinue
$classicWinDbg = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\windbg.exe'

if ($modernWinDbg) 
{
    $winDbg = $modernWinDbg.Source
}
elseif (Test-Path -LiteralPath $classicWinDbg -PathType Leaf) 
{
    $winDbg = $classicWinDbg
}
else 
{
    throw 'WinDbg was not found. Install WinDbg or update its path in OpenCrash.ps1.'
}

# -z로 덤프, -y로 PDB 검색 경로, -i로 EXE 이미지 검색 경로를 전달
$debuggerArgs = @('-z', $dump.FullName, '-y', $symbolPath, '-i', $symbolPath)

# Git 복원 스크립트를 호출
$env:HITORI_GIT_SOURCE_SCRIPT = Join-Path $PSScriptRoot 'GitSource.ps1'
$env:HITORI_SOURCE_REPOSITORY = Split-Path -Parent $PSScriptRoot
$debuggerHome = Join-Path $env:LOCALAPPDATA 'Hitori\Debugger'
New-Item -ItemType Directory -Path $debuggerHome -Force | Out-Null
$env:DBGHELP_HOMEDIR = $debuggerHome

# SrcSrv 검색 활성화
$debuggerArgs += @('-srcpath', 'srv*')

# .ecxr로 예외 당시 레지스터 문맥을 선택하고 k로 해당 스레드의 콜스택을 표시
$debuggerArgs += @('-c', '.ecxr; k')
& $winDbg @debuggerArgs
