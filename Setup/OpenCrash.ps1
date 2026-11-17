param
(
    [Parameter(Mandatory)]
    [string] $DumpPath
)

$ErrorActionPreference = 'Stop'

$dump = Get-Item -LiteralPath $DumpPath
if ($dump.PSIsContainer -or $dump.Extension -ne '.dmp') 
{
    throw "Select a .dmp file: $DumpPath"
}

$symbolStore = '\\172.21.10.122\Symbols'
$symbolCache = Join-Path $env:LOCALAPPDATA 'Hitori\SymbolCache'
New-Item -ItemType Directory -Path $symbolCache -Force | Out-Null
$symbolPath = "srv*$symbolCache*$symbolStore"

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

$debuggerArgs = @('-z', $dump.FullName, '-y', $symbolPath)

$buildIdName = $dump.BaseName + '.build-id.txt'
$buildIdFile = @(
    (Join-Path $dump.DirectoryName $buildIdName)
    (Join-Path (Join-Path $symbolStore 'Crashes') $buildIdName)
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
$sourceRoots = @()
if ($buildIdFile)
{
    $buildId = (Get-Content -LiteralPath $buildIdFile -Raw).Trim()
    if ($buildId -match '^[0-9a-fA-F]{64}$') 
    {
        $imageFolder = Join-Path (Join-Path $symbolStore 'Builds') $buildId
        $buildSourceRoot = Join-Path (Join-Path $symbolStore 'Sources') $buildId
        $sourceRoots += $buildSourceRoot
        if (Test-Path -LiteralPath $imageFolder -PathType Container) 
        {
            $debuggerArgs += @('-i', $imageFolder)
        }
        else 
        {
            Write-Warning "Image folder not found: $imageFolder"
        }
    }
    else 
    {
        Write-Warning "Invalid build ID: $buildIdFile"
    }
}
else 
{
    Write-Warning "Build ID file not found: $buildIdName"
}

$sourceFolders = foreach ($sourceRoot in $sourceRoots)
{
    if (Test-Path -LiteralPath $sourceRoot -PathType Container)
    {
        $sourceRoot
        Get-ChildItem -LiteralPath $sourceRoot -Directory -Recurse | Select-Object -ExpandProperty FullName
    }
}

if ($sourceFolders)
{
    $debuggerArgs += @('-srcpath', ($sourceFolders -join ';'))
}
else 
{
    Write-Warning "Source folders not found: $($sourceRoots -join '; ')"
}

$debuggerArgs += @('-c', '.ecxr; k')

& $winDbg @debuggerArgs
