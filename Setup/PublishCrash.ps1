param
(
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
if ($LASTEXITCODE -ne 0) 
{
    throw "PDB 등록 실패"
}

$destination = Join-Path $crashes $dump.Name
if (-not (Test-Path -LiteralPath $destination)) 
{
    $temporary = "$destination.$([guid]::NewGuid().ToString('N')).uploading"
    Copy-Item -LiteralPath $dump.FullName -Destination $temporary
    Move-Item -LiteralPath $temporary -Destination $destination
}

Write-Host "등록 완료 $destination"

# symstore.exe와 같은 Debugging Tools 폴더에 있다고 가정
$cdb = Join-Path (Split-Path -Parent $symstore) 'cdb.exe'

try 
{
    $sourceRoot = (Resolve-Path -LiteralPath (Join-Path $pdb.DirectoryName '..\..\..\Source')).ProviderPath.TrimEnd('\') + '\'

    $stack = & $cdb -z $dump.FullName -y $pdb.DirectoryName -lines -c '.ecxr; k 40; q'
    if ($LASTEXITCODE -ne 0) 
    {
        throw "CDB failed (exit code $LASTEXITCODE)"
    }

    $reportDir = Join-Path $crashes $dump.BaseName
    $sourceDestination = Join-Path $reportDir 'Source'
    $files = foreach ($line in $stack) 
    {
        if ($line -match '\[(?<path>[A-Z]:\\[^\]]+\.(?:cpp|h|hpp|inl))\s+@\s+\d+\]') 
        {
            $file = [IO.Path]::GetFullPath($Matches.path)
            if ($file.StartsWith($sourceRoot, [StringComparison]::OrdinalIgnoreCase)) 
            {
                $file
            }
        }
    }

    foreach ($file in ($files | Sort-Object -Unique)) 
    {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) 
        {
            Write-Warning "Source file missing: $file"
            continue
        }

        $target = Join-Path $sourceDestination $file.Substring($sourceRoot.Length)
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file -Destination $target
    }
}
catch 
{
    Write-Warning "Source export failed: $_"
}