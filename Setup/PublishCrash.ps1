param(
    [Parameter(Mandatory)] [string] $DumpPath,
    [Parameter(Mandatory)] [string] $PdbPath
)

$ErrorActionPreference = 'Stop'

$symstore = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symstore.exe'
$symbols = '\\172.21.10.122\Symbols'
$crashes = '\\172.21.10.122\Symbols\Crashes'

$dump = Get-Item -LiteralPath $DumpPath
$pdb = Get-Item -LiteralPath $PdbPath

& $symstore add /f $pdb.FullName /s $symbols /t Hitori
if ($LASTEXITCODE -ne 0) {
    throw "PDB 등록 실패 (종료 코드 $LASTEXITCODE)"
}

$destination = Join-Path $crashes $dump.Name
if (-not (Test-Path -LiteralPath $destination)) {
    $temporary = "$destination.$([guid]::NewGuid().ToString('N')).uploading"
    Copy-Item -LiteralPath $dump.FullName -Destination $temporary
    Move-Item -LiteralPath $temporary -Destination $destination
}

Write-Host "등록 완료 $destination"