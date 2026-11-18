# 사용자가 전달한 덤프를 WinDbg로 열고 빌드에 맞는 심볼과 소스 검색 경로를 설정

param
(
    [Parameter(Mandatory)]
    [string] $DumpPath
)

# 파일이나 저장소를 읽지 못하면 중간 설정으로 디버거를 열지 않고 오류 반환
$ErrorActionPreference = 'Stop'

$stateRoot = Join-Path $env:LOCALAPPDATA 'Hitori\Debugger'
$logDirectory = Join-Path $stateRoot 'Logs'

New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

# 최근 로그 20개를 유지
Get-ChildItem -LiteralPath $logDirectory -Filter '*.log' -File |
    Sort-Object LastWriteTime -Descending |
    Select-Object -Skip 20 |
    Remove-Item -Force

$logName = '{0}-{1}.log' -f `
    (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $PID

$logPath = Join-Path $logDirectory $logName

function Write-ViewerLog
{
    param([string] $Message)

    $line = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss.fff'), $Message
    Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8
}

try
{
    $dump = Get-Item -LiteralPath $DumpPath
    if ($dump.PSIsContainer -or $dump.Extension -ne '.dmp') 
    {
        throw "Select a .dmp file: $DumpPath"
    }

    $stateRoot = Join-Path $env:LOCALAPPDATA 'Hitori\Debugger'
    $configPath = Join-Path $stateRoot 'Viewer.json'

    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf))
    {
        throw 'Run RegisterDumpViewer.bat first.'
    }

    $config = Get-Content -LiteralPath $configPath -Raw |
        ConvertFrom-Json

    $symbolStore = $config.SymbolStore
    if ([string]::IsNullOrWhiteSpace($symbolStore))
    {
        throw 'SymbolStore is missing. Run RegisterDumpViewer.bat again.'
    }

    Write-ViewerLog "OriginalDump=$($dump.FullName)"
    Write-ViewerLog "DumpSHA256=$((Get-FileHash -LiteralPath $dump.FullName -Algorithm SHA256).Hash)"
    Write-ViewerLog "ConfigPath=$configPath"
    Write-ViewerLog "VisualStudioPath=$($config.VisualStudioPath)"
    Write-ViewerLog "SettingsPath=$($config.SettingsPath)"
    Write-ViewerLog "SymbolStore=$symbolStore"
    Write-ViewerLog "WorkingDirectory=$PSScriptRoot"
    Write-ViewerLog "IsUNC=$($dump.FullName.StartsWith('\\'))"

    $gitSourcePath = Join-Path $PSScriptRoot 'GitSource.ps1'

    foreach ($file in @(
        $config.VisualStudioPath,
        $config.SettingsPath,
        $gitSourcePath
    ))
    {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf))
        {
            throw "Required file is missing: $file"
        }
    }

    $env:HITORI_GIT_SOURCE_SCRIPT = $gitSourcePath
    $env:DBGHELP_HOMEDIR = $stateRoot

    $repositoryCandidate = Split-Path -Parent $PSScriptRoot
    $gitEntry = Join-Path $repositoryCandidate '.git'

    $env:HITORI_SOURCE_REPOSITORY = $null

    if (Test-Path -LiteralPath $gitEntry)
    {
        $env:HITORI_SOURCE_REPOSITORY = $repositoryCandidate
    }

    $dumpPathForVs = $dump.FullName

    if ($dumpPathForVs.StartsWith('\\'))
    {
        $cacheDirectory = Join-Path $stateRoot (
            'Dumps\' + [guid]::NewGuid().ToString('N')
        )

        New-Item -ItemType Directory `
            -Path $cacheDirectory -Force | Out-Null

        $dumpPathForVs = Join-Path $cacheDirectory $dump.Name

        Copy-Item -LiteralPath $dump.FullName `
            -Destination $dumpPathForVs -ErrorAction Stop
    }

    $arguments = (
    '"{0}" /RootSuffix HitoriCrash /ResetSettings "{1}"'
    ) -f $dumpPathForVs, $config.SettingsPath

    Write-ViewerLog "OpenedDump=$dumpPathForVs"
    Write-ViewerLog "Arguments=$arguments"
    Write-ViewerLog "GitSourceScript=$env:HITORI_GIT_SOURCE_SCRIPT"
    Write-ViewerLog "SourceRepository=$env:HITORI_SOURCE_REPOSITORY"

    Start-Process `
        -FilePath $config.VisualStudioPath `
        -ArgumentList $arguments `
        -WorkingDirectory $PSScriptRoot
    }
catch
{
    Write-Host $_.Exception.Message -ForegroundColor Red

    Write-ViewerLog ($_ | Out-String)
    Write-Host "Log: $logPath"

    [void](Read-Host 'Press Enter to close')
    exit 1
}
