param
(
    [string] $SymbolStore = (
        Join-Path $PSScriptRoot '..\Symbols'
    )
)

$ErrorActionPreference = 'Stop'

try
{
    $settingsPath = Join-Path $PSScriptRoot 'HitoriCrash.vssettings'

    $viewerPath = Join-Path $PSScriptRoot 'OpenCrash.ps1'
    $gitSourcePath = Join-Path $PSScriptRoot 'GitSource.ps1'
    $powerShellPath = Join-Path $PSHOME 'powershell.exe'

    foreach ($file in @($settingsPath, $viewerPath, $gitSourcePath))
    {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf))
        {
            throw "Required file is missing: $file"
        }
    }

    if (-not (Test-Path -LiteralPath $SymbolStore -PathType Container))
    {
        throw "Cannot access the symbol store: $SymbolStore"
    }

    Get-Command git.exe -ErrorAction Stop | Out-Null

    # VS 찾기
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'

    if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf))
    {
        throw 'Visual Studio Installer/vswhere.exe was not found.'
    }

    $installationsJson = & $vswherePath `
        -all -prerelease -products '*' -format json

    if ($LASTEXITCODE -ne 0)
    {
        throw 'Could not enumerate Visual Studio installations.'
    }

    $installations = @($installationsJson | ConvertFrom-Json)

    $visualStudioPath = $null

    foreach ($installation in $installations)
    {
        $candidate = Join-Path $installation.installationPath `
            'Common7\IDE\devenv.exe'

        if (Test-Path -LiteralPath $candidate -PathType Leaf)
        {
            $visualStudioPath = $candidate
            break
        }
    }

    if (-not $visualStudioPath)
    {
        throw 'A Visual Studio IDE installation was not found.'
    }

    # 사용자별 설정 및 캐시 생성
    $stateRoot = Join-Path $env:LOCALAPPDATA 'Hitori\Debugger'

    New-Item -ItemType Directory `
        -Path $stateRoot -Force | Out-Null

    $configPath = Join-Path $stateRoot 'Viewer.json'

    # 상단에서 지정한 HitoriCrash.vssettings를 읽음
    $settings = New-Object System.Xml.XmlDocument
    $settings.Load($settingsPath)

    if ($settings.DocumentElement.Name -ne 'UserSettings')
    {
        throw "Invalid Visual Studio settings file: $settingsPath"
    }

    # 등록할 때 받은 공유폴더 경로를 VS 심볼 설정에 반영
    $symbolPathNode = $settings.SelectSingleNode(
        "//PropertyValue[@name='SymbolPath']"
    )

    if ($null -eq $symbolPathNode)
    {
        throw 'SymbolPath was not found in HitoriCrash.vssettings.'
    }

    $symbolPathNode.InnerText = $SymbolStore

    # Setup의 원본 대신 사용자별로 생성한 설정 파일을 사용
    $generatedSettingsPath = Join-Path $stateRoot 'HitoriCrash.vssettings'
    $settings.Save($generatedSettingsPath)

    [ordered]@{
        VisualStudioPath = $visualStudioPath
        SettingsPath     = $generatedSettingsPath
        SymbolStore      = $SymbolStore
    } | ConvertTo-Json |
        Set-Content -LiteralPath $configPath -Encoding UTF8

    $classes = 'HKCU:\Software\Classes'
    $progId = 'Hitori.CrashDump'
    $handlerKey = "$classes\$progId"

    $openCommand = (
        '"{0}" -NoProfile -ExecutionPolicy RemoteSigned ' +
        '-File "{1}" -DumpPath "%1"'
    ) -f $powerShellPath, $viewerPath

    New-Item -Path $handlerKey -Force | Out-Null
    Set-Item -LiteralPath $handlerKey -Value 'Hitori Crash Dump'

    $applicationKey = "$handlerKey\Application"
    New-Item -Path $applicationKey -Force | Out-Null

    New-ItemProperty -Path $applicationKey `
        -Name 'ApplicationName' `
        -PropertyType String `
        -Value 'Hitori Dump Viewer' -Force | Out-Null

    $commandKey = "$handlerKey\shell\open\command"
    New-Item -Path $commandKey -Force | Out-Null
    Set-Item -LiteralPath $commandKey -Value $openCommand

    $openWithKey = "$classes\.dmp\OpenWithProgids"
    New-Item -Path $openWithKey -Force | Out-Null

    New-ItemProperty -Path $openWithKey `
        -Name $progId `
        -PropertyType String `
        -Value '' -Force | Out-Null

    # 파일 연결이 변경되었음을 탐색기에 알림
    Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class HitoriAssociationNotification
{
    [DllImport("shell32.dll")]
    public static extern void SHChangeNotify(
        uint eventId, uint flags, IntPtr item1, IntPtr item2);
}
'@

    [HitoriAssociationNotification]::SHChangeNotify(
        0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

    Write-Host 'Registration completed.'
    Write-Host "Visual Studio: $visualStudioPath"
    Write-Host "Settings: $generatedSettingsPath"
    Write-Host 'Choose Hitori Dump Viewer from Open with for a .dmp file.'
}
catch
{
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}