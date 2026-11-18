# PDB의 srcsrv 명령이 실행하는 Git 소스 복원 스크립트
# OpenCrash.ps1이 전달한 로컬 저장소를 먼저 조회
# PublishSymbols.ps1은 이 파일을 dot-source해서 체크섬·줄바꿈·Git 객체 읽기 함수를 함께 사용

param
(
    # PDB에 저장한 원격 저장소 주소
    [string] $RepositoryBase64,
    # 소스를 가져올 Git 커밋의 전체 해시
    [string] $Commit,
    # 저장소 루트를 기준으로 한 소스 파일 경로
    [string] $PathBase64,
    [ValidateSet('Raw', 'LF', 'CRLF')] [string] $LineEndings = 'Raw',
    [ValidateSet('MD5', 'SHA1', 'SHA256')] [string] $Algorithm = 'SHA256',
    # 복원한 소스가 컴파일 당시 소스와 일치하는지 검증할 기준 해시
    [string] $Checksum,     
    # SrcSrv가 정한 로컬 소스 캐시 파일 경로
    [string] $Destination    
)

Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1')

function Get-SourceChecksum([byte[]] $Bytes, [string] $HashAlgorithm)
{
    $hasher = [Security.Cryptography.HashAlgorithm]::Create($HashAlgorithm)
    try { return [BitConverter]::ToString($hasher.ComputeHash($Bytes)).Replace('-', '') }
    finally { $hasher.Dispose() }
}

function Convert-SourceLineEndings([byte[]] $Bytes, [string] $Mode)
{
    if ($Mode -eq 'Raw') { return ,$Bytes }
    $encoding = [Text.Encoding]::GetEncoding(28591)
    $text = $encoding.GetString($Bytes).Replace("`r`n", "`n")
    if ($Mode -eq 'CRLF') { $text = $text.Replace("`n", "`r`n") }
    return ,$encoding.GetBytes($text)
}

# git cat-file을 한 번 실행하고 여러 소스 파일을 연속해서 읽을 통로를 생성
function Open-GitObjectReader([string] $Repository)
{
    $git = (Get-Command git.exe -ErrorAction Stop).Source
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $git
    $startInfo.WorkingDirectory = $Repository
    $startInfo.Arguments = 'cat-file --batch'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [Diagnostics.Process]::Start($startInfo)
    return [pscustomobject]@{
        Process = $process
        Errors = $process.StandardError.ReadToEndAsync()
    }
}

# 열려 있는 Git 프로세스로 blob 하나를 요청하고 파일의 원본 바이트를 읽음
function Read-GitObject($Reader, [string] $Object)
{
    if ($Object -match '[\r\n]') { throw 'Invalid Git object name.' }

    $Reader.Process.StandardInput.WriteLine($Object)
    $Reader.Process.StandardInput.Flush()

    $stream = $Reader.Process.StandardOutput.BaseStream
    $header = New-Object Text.StringBuilder

    while (($value = $stream.ReadByte()) -ne 10)
    {
        if ($value -lt 0) { throw 'Git closed its object stream.' }
        [void]$header.Append([char]$value)
    }

    if ($header.ToString() -notmatch '^\S+ blob (\d+)$')
    {
        throw "Cannot read Git source object: $Object ($header)"
    }

    $bytes = New-Object byte[] ([int]$Matches[1])
    $offset = 0

    while ($offset -lt $bytes.Length)
    {
        $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
        if ($read -eq 0) { throw 'Git returned an incomplete source file.' }
        $offset += $read
    }

    if ($stream.ReadByte() -ne 10) { throw 'Invalid Git object terminator.' }
    return ,$bytes
}

function Close-GitObjectReader($Reader)
{
    if ($null -eq $Reader) { return }
    $Reader.Process.StandardInput.Close()
    if (-not $Reader.Process.WaitForExit(5000)) { $Reader.Process.Kill() }
    $Reader.Process.Dispose()
}

# PDB에 기록된 커밋의 소스를 복원하고 체크섬 검증을 거쳐 SrcSrv 캐시에 저장
function Export-GitSource
{
    if ($Commit -notmatch '^[0-9a-fA-F]{40,64}$' -or $Checksum -notmatch '^[0-9a-fA-F]{32,64}$')
    {
        throw 'Invalid source revision or checksum.'
    }

    # 원래 문자열 복구
    $repository = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($RepositoryBase64))
    $relativePath = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($PathBase64))
    if ($relativePath -match '[\r\n]' -or [string]::IsNullOrWhiteSpace($Destination))
    {
        throw 'Invalid source path or destination.'
    }

    # 개발자 저장소에 이미 있는 커밋의 객체부터 조회
    # OpenCrash.ps1이 지정한 개발자 저장소 위치를 읽음
    $localRepository = $env:HITORI_SOURCE_REPOSITORY

    $reader = $null
    $bytes = $null
    try
    {
        if ($localRepository -and (Test-Path -LiteralPath $localRepository -PathType Container))
        {
            # 로컬 작업 파일 대신 저장소에 보관된 커밋의 내용을 조회
            $reader = Open-GitObjectReader $localRepository
            # 요청한 커밋에서 정확한 파일 경로의 blob을 읽음
            try { $bytes = Read-GitObject $reader "${Commit}:$relativePath" }
            catch { Write-Verbose 'Source revision is unavailable in the local checkout.' }
        }
    }
    finally { Close-GitObjectReader $reader }

    if ($null -eq $bytes)
    {
        $repositoryId = Get-SourceChecksum ([Text.Encoding]::UTF8.GetBytes($repository)) 'SHA256'
        $cache = Join-Path $env:LOCALAPPDATA "Hitori\GitCache\$repositoryId"
        $mutex = New-Object Threading.Mutex($false, "Local\HitoriGit-$repositoryId")
        $locked = $false
        try
        {
            try { $locked = $mutex.WaitOne(60000) }
            catch [Threading.AbandonedMutexException] { $locked = $true }
            if (-not $locked) { throw 'Timed out waiting for the Git source cache.' }
            if (-not (Test-Path -LiteralPath (Join-Path $cache 'HEAD')))
            {
                New-Item -ItemType Directory -Path $cache -Force | Out-Null
                & git.exe init --bare $cache | Out-Null
                if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Git source cache.' }
            }

            & git.exe -C $cache cat-file -e "${Commit}^{commit}" 2>$null

            if ($LASTEXITCODE -ne 0)
            {
                $env:GIT_TERMINAL_PROMPT = '0'
                & git.exe -C $cache fetch --no-tags --depth=1 -- $repository $Commit
                if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the indexed Git commit. Check repository access.' }
            }
            $reader = Open-GitObjectReader $cache
            try { $bytes = Read-GitObject $reader "${Commit}:$relativePath" }
            finally { Close-GitObjectReader $reader }
        }
        finally
        {
            if ($locked) { $mutex.ReleaseMutex() }
            $mutex.Dispose()
        }
    }

    $bytes = Convert-SourceLineEndings $bytes $LineEndings
    if ((Get-SourceChecksum $bytes $Algorithm) -ne $Checksum)
    {
        throw 'Git source does not match the checksum recorded by the compiler.'
    }

    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temporary = Join-Path $parent ([guid]::NewGuid().ToString('N') + '.source')
    try
    {
        [IO.File]::WriteAllBytes($temporary, $bytes)
        try { [IO.File]::Move($temporary, $Destination) }
        catch [IO.IOException]
        {
            # 최종 파일이 실제로 존재하고 체크섬도 일치할 때만 동시 저장에 성공한 것으로 처리
            if (-not (Test-Path -LiteralPath $Destination -PathType Leaf) -or
                (Get-FileHash -LiteralPath $Destination -Algorithm $Algorithm).Hash -ne $Checksum) { throw }
        }
    }
    finally
    {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}

# 직접 실행된 경우에만 소스 복원을 시작
if ($MyInvocation.InvocationName -ne '.')
{
    $ErrorActionPreference = 'Stop'
    try { Export-GitSource }
    catch { Write-Error $_; exit 1 }
}
