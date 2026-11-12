#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SdkRoot,
    [switch]$ValidateOnly,
    [switch]$GenerateProjects
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$expectedVersion = '2020.3.11'

try {
    if ($ValidateOnly -and $GenerateProjects) {
        throw 'Use either -ValidateOnly or -GenerateProjects, not both.'
    }
    # An explicit path wins; otherwise prefer the saved user setting over a stale shell.
    if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
        $SdkRoot = [Environment]::GetEnvironmentVariable('FBX_SDK_ROOT', 'User')
    }
    if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
        $SdkRoot = $env:FBX_SDK_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
        $SdkRoot = Join-Path $env:ProgramFiles "Autodesk\FBX\FBX SDK\$expectedVersion"
    }

    $SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).ProviderPath
    $versionHeader = Join-Path $SdkRoot 'include\fbxsdk\fbxsdk_version.h'
    $headerText = Get-Content -LiteralPath $versionHeader -Raw
    $versionParts = foreach ($part in @('MAJOR', 'MINOR', 'POINT')) {
        $match = [regex]::Match($headerText, "(?m)^\s*#define\s+FBXSDK_VERSION_$part\s+(\d+)\b")
        if (-not $match.Success) {
            throw "Cannot read FBXSDK_VERSION_$part from $versionHeader"
        }
        $match.Groups[1].Value
    }
    $actualVersion = $versionParts -join '.'
    if ($actualVersion -ne $expectedVersion) {
        throw "Expected SDK $expectedVersion (Windows VS2022), found $actualVersion."
    }

    # Verified against the 2020.3.11 VS2022 installer: lib\x64, not lib\vs2022\x64.
    $requiredFiles = @(
        'include\fbxsdk.h',
        'lib\x64\debug\libfbxsdk.lib',
        'lib\x64\debug\libfbxsdk.dll',
        'lib\x64\release\libfbxsdk.lib',
        'lib\x64\release\libfbxsdk.dll'
    )
    foreach ($relativePath in $requiredFiles) {
        $filePath = Join-Path $SdkRoot $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Required file missing: $filePath. Finish installing the Windows VS2022 SDK."
        }
        if ((Get-Item -LiteralPath $filePath).Length -eq 0) {
            throw "Required file is empty: $filePath"
        }
    }

    Write-Host "SDK version: $actualVersion"
    Write-Host "SDK root:    $SdkRoot"
    Write-Host 'Verified x64 Debug/Release dynamic-link files.'
    if ($ValidateOnly) {
        Write-Host 'Validation only: no environment settings changed.'
    }
    elseif (-not $GenerateProjects) {
        # User scope only. No administrator rights or machine-wide PATH changes.
        [Environment]::SetEnvironmentVariable('FBX_SDK_ROOT', $SdkRoot, 'User')
        $env:FBX_SDK_ROOT = $SdkRoot
        Write-Host 'Saved FBX_SDK_ROOT for the current Windows user.'
        Write-Host 'Run SlnGenerator.bat next. No terminal restart is required.'
    }
    if ($GenerateProjects) {
        # Read User settings above on every run, even from an already-open terminal.
        $env:FBX_SDK_ROOT = $SdkRoot
        Push-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
        try {
            & (Join-Path $PSScriptRoot 'premake5.exe') vs2026
            if ($LASTEXITCODE -ne 0) {
                throw "Premake failed with exit code $LASTEXITCODE."
            }
        }
        finally {
            Pop-Location
        }
        Write-Host 'Projects generated with FBX SDK support for HitoriEditor.'
    }
    exit 0
}
catch {
    [Console]::Error.WriteLine("FBX SDK setup failed: $($_.Exception.Message)")
    [Console]::Error.WriteLine('Install FBX SDK 2020.3.11 Windows VS2022, then retry with -SdkRoot "your SDK folder".')
    exit 1
}
