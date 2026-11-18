# 빌드 성공 후 로컬 PDB를 인덱싱하고, 게시 옵션이 켜져 있으면 공유 저장소에도 게시
# 빌드 결과 사본 검증 → Git 소스 인덱스 작성 → 사본·로컬 PDB 삽입 → 선택적 SymStore 등록
# 공유 저장소에 올리는 빌드 산출물은 EXE와 PDB이며, 소스는 PDB가 가리키는 Git 커밋에서 복원

param
(
    [Parameter(Mandatory)] [string] $ExePath,
    [string] $PdbPath,
    [switch] $IndexOnly,
    [string] $SymbolStore = '\\172.21.10.122\Symbols',
    [string] $DebuggingTools = "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64"
)

# PowerShell 명령에서 오류가 나면 catch로 이동해 게시 실패를 빌드에 전달
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
# 체크섬·줄바꿈 변환·Git 객체 읽기 함수를 현재 스크립트 범위에 불러옴
. (Join-Path $PSScriptRoot 'GitSource.ps1')

# finally에서 정리할 임시 폴더
# Git 읽기 프로세스, 공유 저장소 잠금을 초기화
$stage = $null
$reader = $null
$storeLock = $null
try
{
    $exe = Get-Item -LiteralPath $ExePath
    if (-not $PdbPath) { $PdbPath = [IO.Path]::ChangeExtension($exe.FullName, '.pdb') }
    $pdb = Get-Item -LiteralPath $PdbPath
    if ($exe.PSIsContainer -or $exe.Extension -ne '.exe' -or $pdb.PSIsContainer -or $pdb.Extension -ne '.pdb')
    {
        throw 'Select an EXE and its matching linker PDB.'
    }

    $symstore = Join-Path $DebuggingTools 'symstore.exe'
    $srctool = Join-Path $DebuggingTools 'srcsrv\srctool.exe'
    $pdbstr = Join-Path $DebuggingTools 'srcsrv\pdbstr.exe'
    $requiredTools = @($srctool, $pdbstr)
    if (-not $IndexOnly) { $requiredTools += $symstore }
    foreach ($tool in $requiredTools)
    {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Debugging tool missing: $tool" }
    }

    $repositoryRoot = (& git.exe -C $PSScriptRoot rev-parse --show-toplevel)
    # Git 조회 실패를 종료 코드로 확인
    if ($LASTEXITCODE -ne 0) { throw 'Could not locate the Git repository.' }

    # Git이 반환한 슬래시를 Windows 경로 형식으로 맞춰 PDB의 절대 경로와 비교
    $repositoryRoot = [IO.Path]::GetFullPath($repositoryRoot).Replace('/', '\')
    # 이번 인덱스가 참조할 현재 커밋의 전체 해시를 고정
    $commit = (& git.exe -C $repositoryRoot rev-parse HEAD).Trim()

    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-fA-F]{40,64}$') { throw 'Could not resolve HEAD.' }

    # 다른 개발자 PC에서도 같은 커밋을 받을 수 있도록 origin 원격 주소를 조회
    $remote = (& git.exe -C $repositoryRoot remote get-url origin)
    if ($LASTEXITCODE -ne 0 -or -not $remote) { throw 'Configure the origin remote for Git source retrieval.' }

    $remote = $remote.Trim()
    $remoteBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($remote))

    $stageRoot = Join-Path $repositoryRoot 'Build\SymbolPublishing'
    $stage = Join-Path $stageRoot ([guid]::NewGuid().ToString('N'))
    $files = Join-Path $stage 'Files'

    New-Item -ItemType Directory -Path $files -Force | Out-Null
    $stagedExe = Join-Path $files $exe.Name
    $stagedPdb = Join-Path $files $pdb.Name

    $exeHash = (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash
    $pdbHash = (Get-FileHash -LiteralPath $pdb.FullName -Algorithm SHA256).Hash

    Copy-Item -LiteralPath $exe.FullName -Destination $stagedExe
    Copy-Item -LiteralPath $pdb.FullName -Destination $stagedPdb
    if ((Get-FileHash -LiteralPath $stagedExe -Algorithm SHA256).Hash -ne $exeHash -or
        (Get-FileHash -LiteralPath $stagedPdb -Algorithm SHA256).Hash -ne $pdbHash -or
        (Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash -ne $exeHash -or
        (Get-FileHash -LiteralPath $pdb.FullName -Algorithm SHA256).Hash -ne $pdbHash)
    {
        throw 'Build outputs changed during publication. Retry after the build completes.'
    }

    # Windows dbghelp.dll의 심볼 식별정보 조회 함수를 호출
    if (-not ('Hitori.Build.Symbols' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'SymbolFileInfo.cs') }
    $exeInfo = [Hitori.Build.Symbols]::Read($stagedExe)
    $pdbInfo = [Hitori.Build.Symbols]::Read($stagedPdb)
    if ($exeInfo.Guid -eq [guid]::Empty -or $exeInfo.Guid -ne $pdbInfo.Guid -or
        $exeInfo.Age -ne $pdbInfo.Age -or [IO.Path]::GetFileName($exeInfo.PdbFile) -ne $pdb.Name)
    {
        throw 'The EXE and PDB do not match. Publish outputs from the same link.'
    }

    # 이미 인덱싱한 로컬 PDB를 재게시할 때도 최초 원본 해시를 유지
    # 현재 파일 해시($pdbHash)는 게시 도중 파일이 바뀌었는지 검사하는 데 사용
    $originalPdbHash = $pdbHash
    $previousIndex = @(& $pdbstr -r "-p:$stagedPdb" -s:srcsrv)
    if ($LASTEXITCODE -eq 0)
    {
        foreach ($line in $previousIndex)
        {
            if ($line -match '^HITORI_PDB_SHA256=([0-9A-Fa-f]{64})$')
            {
                $originalPdbHash = $Matches[1].ToUpperInvariant()
                break
            }
        }
    }

    $trackedFiles = @{}
    # 고정한 커밋의 파일 목록을 재귀적으로 읽음
    $tree = @(& git.exe -C $repositoryRoot -c core.quotePath=false ls-tree -r --full-tree $commit)
    if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate committed sources.' }

    foreach ($line in $tree)
    {
        if ($line -match '^\d+ blob ([0-9a-f]+)\t(.+)$')
        {
            # 이후 PDB 소스 경로를 찾을 때 해당 커밋의 blob을 바로 조회하도록 저장
            $trackedFiles[$Matches[2]] = $Matches[1]
        }
    }

    $sourceFiles = @(& $srctool -r -h -z $stagedPdb)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read PDB source checksums. srctool must support -h.' }

    # 여러 blob을 읽을 Git 프로세스를 하나 열어 파일마다 프로세스를 새로 실행하는 비용을 줄임
    $reader = Open-GitObjectReader $repositoryRoot
    $rootPrefix = $repositoryRoot.TrimEnd('\', '/') + '\'
    $entries = [Collections.Generic.List[string]]::new()
    $skipped = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $sourceRecords = @{}
    foreach ($line in $sourceFiles)
    {
        if ($line -match '^(?<path>.+?)\s+Checksum (?<algorithm>MD5|SHA1|SHA256):\s*(?<hash>[0-9a-fA-F]+)\s*$')
        {
            $sourcePath = $Matches.path.TrimEnd()
            if (-not $sourcePath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { continue }
            $record = [pscustomobject]@{ Path = $sourcePath; Algorithm = $Matches.algorithm; Hash = $Matches.hash }
            # 같은 경로가 서로 다른 내용으로 컴파일된 기록이 있으면 하나의 Git 파일로 연결할 수 없다고 판단
            if ($sourceRecords.ContainsKey($sourcePath) -and $sourceRecords[$sourcePath].Hash -ne $record.Hash)
            {
                [void]$skipped.Add($sourcePath)
            }
            $sourceRecords[$sourcePath] = $record
        }
    }
    # 같은 입력에서 매핑 순서가 일정하도록 소스 경로를 정렬한 뒤 인덱싱
    foreach ($record in ($sourceRecords.Values | Sort-Object Path))
    {
        $relativePath = $record.Path.Substring($rootPrefix.Length).Replace('\', '/')
        if ($skipped.Contains($record.Path) -or -not $trackedFiles.ContainsKey($relativePath))
        {
            [void]$skipped.Add($record.Path)
            continue
        }
        $bytes = Read-GitObject $reader $trackedFiles[$relativePath]
        $matchingMode = $null

        foreach ($mode in @('Raw', 'CRLF', 'LF'))
        {
            $candidate = Convert-SourceLineEndings $bytes $mode
            if ((Get-SourceChecksum $candidate $record.Algorithm) -eq $record.Hash)
            {
                $matchingMode = $mode
                break
            }
        }

        if (-not $matchingMode -or $record.Path -match '[*\r\n]')
        {
            [void]$skipped.Add($record.Path)
            continue
        }
        $pathBase64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($relativePath))

        # 별표로 구분한 7개 필드를 기록
        # 원본 경로, 원격 주소, 커밋, 상대 경로, 줄바꿈, 알고리즘, 체크섬 순서
        $entries.Add("$($record.Path)*$remoteBase64*$commit*$pathBase64*$matchingMode*$($record.Algorithm)*$($record.Hash)")
    }

    Close-GitObjectReader $reader       
    $reader = $null               
    foreach ($path in ($skipped | Sort-Object))
    {
        Write-Warning "Git source unavailable or differs from the compiled source: $path"
    }
    if ($entries.Count -eq 0) { Write-Warning 'No compiled sources matched this Git commit. Source retrieval will be unavailable.' }

    # PDB에 넣을 SrcSrv 인덱스 텍스트를 임시 폴더에 만듦
    $streamPath = Join-Path $stage 'srcsrv.txt'
    # SrcSrv가 인식하는 헤더·변수·파일 목록·종료 마커를 차례로 구성
    $stream = @(
        'SRCSRV: ini ------------------------------------------------'
        'VERSION=2'
        'VERCTRL=Git'
        'SRCSRV: variables ------------------------------------------'
        # 인덱스 삽입 전 PDB 해시를 남겨 재게시 때 같은 원본 PDB인지 확인
        "HITORI_PDB_SHA256=$originalPdbHash"
        # 복원 캐시는 커밋과 체크섬으로 구분
        'SRCSRVTRG=%targ%\Hitori\%var3%\%var7%\%fnfile%(%var1%)'
        # OpenCrash.ps1이 지정한 GitSource.ps1에 해당 파일의 7개 매핑 값을 전달
        'SRCSRVCMD=powershell.exe -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -Command "& ([Environment]::GetEnvironmentVariable(''HITORI_GIT_SOURCE_SCRIPT'')) -RepositoryBase64 ''%var2%'' -Commit ''%var3%'' -PathBase64 ''%var4%'' -LineEndings ''%var5%'' -Algorithm ''%var6%'' -Checksum ''%var7%'' -Destination ''%SRCSRVTRG%''"'
        'SRCSRV: source files ---------------------------------------'
    ) + $entries.ToArray() + @('SRCSRV: end ------------------------------------------------')

    [IO.File]::WriteAllLines($streamPath, $stream, [Text.UTF8Encoding]::new($false))
    & $pdbstr -w "-p:$stagedPdb" "-i:$streamPath" -s:srcsrv
    if ($LASTEXITCODE -ne 0) { throw 'Could not write the Git source index to the staged PDB.' }

    $readbackPath = Join-Path $stage 'srcsrv-readback.txt'
    & $pdbstr -r "-p:$stagedPdb" "-i:$readbackPath" -s:srcsrv
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $readbackPath) -or
        (Get-FileHash -LiteralPath $readbackPath).Hash -ne (Get-FileHash -LiteralPath $streamPath).Hash)
    {
        throw 'The staged PDB source index could not be verified.'
    }

    # 공유 저장소 접근 전에 로컬 원본에 같은 인덱스를 기록
    if ((Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash -ne $exeHash -or
        (Get-FileHash -LiteralPath $pdb.FullName -Algorithm SHA256).Hash -ne $pdbHash)
    {
        throw 'Build outputs changed during source indexing. Retry after the build completes.'
    }
    & $pdbstr -w "-p:$($pdb.FullName)" "-i:$streamPath" -s:srcsrv
    if ($LASTEXITCODE -ne 0) { throw 'Could not write the Git source index to the local PDB.' }

    $localReadbackPath = Join-Path $stage 'srcsrv-local-readback.txt'
    & $pdbstr -r "-p:$($pdb.FullName)" "-i:$localReadbackPath" -s:srcsrv
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $localReadbackPath) -or
        (Get-FileHash -LiteralPath $localReadbackPath).Hash -ne (Get-FileHash -LiteralPath $streamPath).Hash)
    {
        throw 'The local PDB source index could not be verified.'
    }
    $localPdbInfo = [Hitori.Build.Symbols]::Read($pdb.FullName)
    if ($localPdbInfo.Guid -ne $pdbInfo.Guid -or $localPdbInfo.Age -ne $pdbInfo.Age)
    {
        throw 'The local PDB symbol identity changed during source indexing.'
    }
    Write-Host "[Hitori] Local PDB indexed: $($pdb.FullName). Git sources indexed: $($entries.Count)."
    if ($IndexOnly) { return }

    if (-not $SymbolStore.StartsWith('\\') -and -not (Test-Path -LiteralPath $SymbolStore))
    {
        New-Item -ItemType Directory -Path $SymbolStore -Force | Out-Null
    }
    $storeDirectory = Get-Item -LiteralPath $SymbolStore
    if (-not $storeDirectory.PSIsContainer) { throw "Select a symbol store directory: $SymbolStore" }
    $SymbolStore = $storeDirectory.FullName
    if ($SymbolStore.StartsWith('\\')) { $SymbolStore = $SymbolStore.TrimEnd('\') }

    $admin = Join-Path $SymbolStore '000Admin'
    if (-not (Test-Path -LiteralPath $admin))
    {
        New-Item -ItemType Directory -Path $admin -Force | Out-Null
    }

    $lockPath = Join-Path $admin 'HitoriPublish.lock'
    # 공유 저장소 잠금을 얻기 위해 기다릴 최대 시간
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ($null -eq $storeLock)
    {
        try { $storeLock = [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
        catch [IO.IOException]
        {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'The symbol store is busy. Build again to retry publication.' }
            Start-Sleep -Milliseconds 250
        }
    }

    # 저장소 키　생성
    $exeKey = '{0:X8}{1:x}' -f $exeInfo.Timestamp, $exeInfo.ImageSize
    # PDB 조회 키는 GUID의 하이픈을 제거한 문자열 뒤에 Age를 16진수로 붙인 값
    $pdbKey = $pdbInfo.Guid.ToString('N').ToUpperInvariant() + $pdbInfo.Age.ToString('x')
    $publishedFiles = @(
        @{ Local = $stagedExe; Remote = Join-Path $SymbolStore "$($exe.Name)\$exeKey\$($exe.Name)" },
        @{ Local = $stagedPdb; Remote = Join-Path $SymbolStore "$($pdb.Name)\$pdbKey\$($pdb.Name)" }
    )

    foreach ($file in $publishedFiles)
    {
        if (Test-Path -LiteralPath $file.Remote -PathType Leaf)
        {
            if ((Get-FileHash -LiteralPath $file.Remote -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $file.Local -Algorithm SHA256).Hash)
            {
                $sameOriginalPdb = $false
                if ($file.Local -eq $stagedPdb)
                {
                    $existingInfo = [Hitori.Build.Symbols]::Read($file.Remote)
                    $existingIndex = @(& $pdbstr -r "-p:$($file.Remote)" -s:srcsrv)
                    $sameOriginalPdb = $LASTEXITCODE -eq 0 -and
                        $existingInfo.Guid -eq $pdbInfo.Guid -and $existingInfo.Age -eq $pdbInfo.Age -and
                        $existingIndex -contains "HITORI_PDB_SHA256=$originalPdbHash"
                }

                if (-not $sameOriginalPdb)
                {
                    throw "A different file already uses this symbol identity: $($file.Remote). Rebuild to create a new identity."
                }
            }
            continue
        }

        # 최종 심볼 경로의 폴더를 준비
        New-Item -ItemType Directory -Path (Split-Path -Parent $file.Remote) -Force | Out-Null
        $upload = $file.Remote + '.' + [guid]::NewGuid().ToString('N') + '.uploading'
        try
        {
            Copy-Item -LiteralPath $file.Local -Destination $upload
            if ((Get-FileHash -LiteralPath $upload -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $file.Local -Algorithm SHA256).Hash)
            {
                throw "Upload verification failed: $upload"
            }
            [IO.File]::Move($upload, $file.Remote)
        }
        finally
        {
            if (Test-Path -LiteralPath $upload) { Remove-Item -LiteralPath $upload }
        }

        & $symstore add /f $file.Local /s $SymbolStore /t Hitori /v $commit
        if ($LASTEXITCODE -ne 0) { throw "SymStore publication failed: $($file.Local)" }
        if (-not (Test-Path -LiteralPath $file.Remote -PathType Leaf) -or
            (Get-FileHash -LiteralPath $file.Remote -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $file.Local -Algorithm SHA256).Hash)
        {
            throw "Published file verification failed: $($file.Remote)"
        }
    }
    Write-Host "[Hitori] Published $($exe.Name) and $($pdb.Name). Local PDB indexed. Git sources indexed: $($entries.Count). Store: $SymbolStore"
}
catch
{
    $failure = $_
    Write-Error -Message "[Hitori] Source indexing or symbol publication failed. $($failure.Exception.Message)`n$($failure.ScriptStackTrace)" -ErrorAction Continue
    exit 1
}
finally
{
    # Git 읽기 프로세스를 정리
    Close-GitObjectReader $reader
    if ($null -ne $storeLock) { $storeLock.Dispose() }
    if ($stage -and (Test-Path -LiteralPath $stage))
    {
        $resolvedStage = [IO.Path]::GetFullPath($stage)
        $stagePrefix = [IO.Path]::GetFullPath($stageRoot).TrimEnd('\') + '\'
        if ($resolvedStage.StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase))
        {
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force
        }
    }
}
