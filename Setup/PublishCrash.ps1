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

$exePath = [IO.Path]::ChangeExtension($pdb.FullName, '.exe')
$exe = Get-Item -LiteralPath $exePath

$buildId = (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash
$imageFolder = Join-Path (Join-Path $symbols 'Builds') $buildId
New-Item -ItemType Directory -Path $imageFolder, $crashes -Force | Out-Null

$sharedExe = Join-Path $imageFolder $exe.Name
if (-not (Test-Path -LiteralPath $sharedExe -PathType Leaf)) 
{
    $temporaryExe = "$sharedExe.$([guid]::NewGuid().ToString('N')).uploading"
    try 
    {
        Copy-Item -LiteralPath $exe.FullName -Destination $temporaryExe
        try 
        {
            Move-Item -LiteralPath $temporaryExe -Destination $sharedExe
        }
        catch 
        {
            # 같은 빌드를 다른 게시 프로세스가 먼저 저장한 경우
            if (-not (Test-Path -LiteralPath $sharedExe -PathType Leaf)) { throw }
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporaryExe) 
        {
            Remove-Item -LiteralPath $temporaryExe
        }
    }
}

Set-Content -LiteralPath (Join-Path $crashes ($dump.BaseName + '.build-id.txt')) -Value $buildId -Encoding ASCII

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

    $stack = & $cdb -z $dump.FullName -y $pdb.DirectoryName -i $imageFolder -lines -c '.ecxr; k 40; q'
    if ($LASTEXITCODE -ne 0) 
    {
        throw "CDB failed (exit code $LASTEXITCODE)"
    }

    $sourceDestination = Join-Path (Join-Path $symbols 'Sources') $buildId
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
        if (-not (Test-Path -LiteralPath $target -PathType Leaf))
        {
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            $temporarySource = "$target.$([guid]::NewGuid().ToString('N')).uploading"
            try
            {
                Copy-Item -LiteralPath $file -Destination $temporarySource
                Move-Item -LiteralPath $temporarySource -Destination $target
            }
            catch
            {
                if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw }
            }
            finally
            {
                if (Test-Path -LiteralPath $temporarySource) { Remove-Item -LiteralPath $temporarySource }
            }
        }
    }
}
catch 
{
    Write-Warning "Source export failed: $_"
}
