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

$reportFolder = Join-Path $dump.DirectoryName $dump.BaseName
$sourceRoot = Join-Path $reportFolder 'Source'

if (Test-Path -LiteralPath $sourceRoot -PathType Container) 
{
    $sourceFolders = @($sourceRoot)
    $sourceFolders += Get-ChildItem -LiteralPath $sourceRoot -Directory -Recurse |
        Select-Object -ExpandProperty FullName

    $debuggerArgs += @('-srcpath', ($sourceFolders -join ';'))
}
else 
{
    Write-Warning "Source folder not found: $sourceRoot"
}

$debuggerArgs += @('-c', '.ecxr; k')

& $winDbg @debuggerArgs