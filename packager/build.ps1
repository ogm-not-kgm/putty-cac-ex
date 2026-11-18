<#
.SYNOPSIS
    Builds x64 and arm64 release binaries, packages ZIP archives, and generates hashes for PuTTY CAC Ex.
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'arm64', 'both')]
    [string]$Arch = 'both',

    [string]$BuildDate = (Get-Date -Format 'yyyyMMdd'),

    [string]$MasterRef = 'master',

    [string]$D2ddwRef = 'mod-d2ddw',

    [switch]$Parallel
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Upstream PuTTY CAC version constant; update manually when upstream bumps.
$UpstreamVersion = '0.85.3'

$MasterTargets = @(
    'pageant',
    'plink',
    'pscp',
    'psftp',
    'pterm',
    'putty',
    'puttygen',
    'puttytab',
    'puttytel'
)
$D2ddwTargets = @('putty')

$ExpectedExeNames = @(
    'pageant.exe',
    'plink.exe',
    'pscp.exe',
    'psftp.exe',
    'pterm.exe',
    'putty-d2ddw.exe',
    'putty.exe',
    'puttygen.exe',
    'puttytab.exe',
    'puttytel.exe'
)

$ForbiddenRuntimePatterns = @(
    'vcruntime.*\.dll',
    'msvcp.*\.dll',
    'msvcr.*\.dll',
    'msvcm.*\.dll',
    'concrt.*\.dll',
    'ucrtbase.*\.dll',
    'api-ms-win-crt-.*\.dll'
)
$ConPtyMissingMessage = 'ConPTY not available; cannot build Windows pterm'

function Get-NormalizedPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$Arguments = @(),

        [switch]$CaptureOutput,

        [switch]$MergeStderr,

        [switch]$EchoCaptured
    )

    $prevPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $global:LASTEXITCODE = 0
        if ($CaptureOutput) {
            if ($MergeStderr) {
                $rawOutput = & $FilePath @Arguments 2>&1
            } else {
                $rawOutput = & $FilePath @Arguments
            }
            $exitCode = $LASTEXITCODE
            $lines = @($rawOutput | ForEach-Object {
                $line = if ($_ -is [System.Management.Automation.ErrorRecord]) {
                    $_.ToString()
                } else {
                    [string]$_
                }
                if ($EchoCaptured) {
                    Write-Host $line
                }
                $line
            })
            if ($exitCode -ne 0) {
                $tail = ($lines | Select-Object -Last 20) -join [Environment]::NewLine
                throw "Command failed with exit code ${exitCode}: $FilePath $($Arguments -join ' ')`n$tail"
            }
            return $lines
        } else {
            & $FilePath @Arguments
            $exitCode = $LASTEXITCODE
            if ($exitCode -ne 0) {
                throw "Command failed with exit code ${exitCode}: $FilePath $($Arguments -join ' ')"
            }
        }
    } finally {
        $ErrorActionPreference = $prevPreference
    }
}

function Import-VsDevEnvironment {
    $progFilesX86 = ${env:ProgramFiles(x86)}
    if ([string]::IsNullOrWhiteSpace($progFilesX86)) {
        $progFilesX86 = $env:ProgramFiles
    }
    $vswherePath = Join-Path $progFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
        throw "vswhere.exe not found at: $vswherePath"
    }

    $vswhereArgs = @(
        '-latest',
        '-products', '*',
        '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        'Microsoft.VisualStudio.Component.VC.Tools.ARM64',
        '-property', 'installationPath'
    )
    $installLines = @(Invoke-NativeCommand -FilePath $vswherePath -Arguments $vswhereArgs -CaptureOutput)
    $vsInstallPath = ($installLines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
        throw 'No Visual Studio installation found with both VC.Tools.x86.x64 and VC.Tools.ARM64 components.'
    }
    $vsInstallPath = $vsInstallPath.Trim()

    $vsDevCmdPath = Join-Path $vsInstallPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $vsDevCmdPath -PathType Leaf)) {
        throw "VsDevCmd.bat not found at: $vsDevCmdPath"
    }

    $hostArch = if (-not [string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITEW6432)) {
        $env:PROCESSOR_ARCHITEW6432
    } else {
        $env:PROCESSOR_ARCHITECTURE
    }

    # Force -arch=amd64 -host_arch=amd64 only on x86_64 hosts; avoid forcing -host_arch=amd64 on non-amd64 hosts.
    $vsDevCmdArgs = ''
    if ($hostArch -eq 'AMD64') {
        $vsDevCmdArgs = '-arch=amd64 -host_arch=amd64'
    } elseif ($hostArch -eq 'ARM64') {
        $vsDevCmdArgs = '-arch=arm64 -host_arch=arm64'
    }

    Write-Host "Using Visual Studio at: $vsInstallPath (host arch: $hostArch)"
    $marker = '__VSDEVCMD_ENV_START__'
    $tempCmd = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), "vsdevcmd-$([System.Guid]::NewGuid().ToString('N')).cmd")
    $callLine = if ([string]::IsNullOrWhiteSpace($vsDevCmdArgs)) {
        "call `"$vsDevCmdPath`""
    } else {
        "call `"$vsDevCmdPath`" $vsDevCmdArgs"
    }
    $cmdContent = @(
        '@echo off',
        "$callLine || exit /b 1",
        "echo $marker",
        'set'
    ) -join "`r`n"

    $envLines = @()
    try {
        [System.IO.File]::WriteAllText($tempCmd, $cmdContent, [System.Text.Encoding]::ASCII)
        $envLines = @(Invoke-NativeCommand -FilePath 'cmd.exe' -Arguments @('/d', '/c', $tempCmd) -CaptureOutput -MergeStderr)
    } finally {
        if (Test-Path -LiteralPath $tempCmd) {
            Remove-Item -LiteralPath $tempCmd -Force -ErrorAction SilentlyContinue
        }
    }

    $markerIndex = -1
    for ($i = 0; $i -lt $envLines.Count; $i++) {
        if ($envLines[$i].Trim() -eq $marker) {
            $markerIndex = $i
            break
        }
    }
    if ($markerIndex -lt 0) {
        throw 'Failed to capture environment variables from VsDevCmd.bat.'
    }

    for ($i = $markerIndex + 1; $i -lt $envLines.Count; $i++) {
        $line = $envLines[$i]
        $eqIdx = $line.IndexOf('=')
        if ($eqIdx -gt 0) {
            $name = $line.Substring(0, $eqIdx)
            $value = $line.Substring($eqIdx + 1)
            [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }

    foreach ($requiredTool in @('cmake.exe', 'dumpbin.exe')) {
        $cmd = Get-Command -Name $requiredTool -ErrorAction SilentlyContinue
        if ($null -eq $cmd) {
            throw "$requiredTool not found on PATH after importing VsDevCmd.bat environment."
        }
    }
}

function Get-RegisteredWorktrees {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    $lines = @(Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'worktree', 'list', '--porcelain') -CaptureOutput)
    $paths = @()
    foreach ($line in $lines) {
        if ($line -match '^worktree\s+(.+)$') {
            $paths += $Matches[1].Trim()
        }
    }
    return $paths
}

function Clear-OwnedWorktrees {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [Parameter(Mandatory = $true)]
        [string[]]$OwnedPaths,

        [Parameter(Mandatory = $true)]
        [string]$WorktreeBase,

        [switch]$IgnoreErrors
    )

    $normalizedOwned = @($OwnedPaths | ForEach-Object { Get-NormalizedPath -Path $_ })

    # Remove registered worktrees owned by this script before deleting residual directories.
    $registered = @()
    try {
        $registered = @(Get-RegisteredWorktrees -RepoRoot $RepoRoot)
    } catch {
        if (-not $IgnoreErrors) { throw }
        Write-Warning "Failed to list git worktrees during cleanup: $($_.Exception.Message)"
    }

    foreach ($regPath in $registered) {
        $normReg = Get-NormalizedPath -Path $regPath
        if ($normalizedOwned -contains $normReg) {
            Write-Host "Removing registered worktree: $regPath"
            try {
                Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'worktree', 'remove', '--force', $regPath)
            } catch {
                Write-Warning "git worktree remove failed for '$regPath': $($_.Exception.Message)"
            }
        }
    }

    foreach ($owned in $OwnedPaths) {
        if (Test-Path -LiteralPath $owned) {
            Write-Host "Deleting residual worktree directory: $owned"
            try {
                Remove-Item -LiteralPath $owned -Recurse -Force
            } catch {
                if (-not $IgnoreErrors) { throw }
                Write-Warning "Failed to delete directory '$owned': $($_.Exception.Message)"
            }
        }
    }

    try {
        Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'worktree', 'prune')
    } catch {
        if (-not $IgnoreErrors) { throw }
        Write-Warning "git worktree prune failed during cleanup: $($_.Exception.Message)"
    }

    if (Test-Path -LiteralPath $WorktreeBase -PathType Container) {
        $remaining = @(Get-ChildItem -LiteralPath $WorktreeBase -Force)
        if ($remaining.Count -eq 0) {
            try {
                Remove-Item -LiteralPath $WorktreeBase -Force
            } catch {
                if (-not $IgnoreErrors) { throw }
                Write-Warning "Failed to remove empty worktree base '$WorktreeBase': $($_.Exception.Message)"
            }
        }
    }
}

function Get-WorktreeTextVer {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorktreePath
    )

    $versionHeader = Join-Path $WorktreePath 'code\version.h'
    if (-not (Test-Path -LiteralPath $versionHeader -PathType Leaf)) {
        throw "version.h not found at: $versionHeader"
    }

    foreach ($line in (Get-Content -LiteralPath $versionHeader)) {
        if ($line -match '^\s*#define\s+TEXTVER\s+"([^"]+)"') {
            return $Matches[1]
        }
    }

    throw "TEXTVER macro not found in: $versionHeader"
}

function Invoke-WorktreeBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BuildName,

        [Parameter(Mandatory = $true)]
        [string]$WorktreePath,

        [Parameter(Mandatory = $true)]
        [string]$CMakePlatform,

        [Parameter(Mandatory = $true)]
        [string]$ReleaseBuildRoot,

        [Parameter(Mandatory = $true)]
        [string[]]$Targets,

        [switch]$UseParallel,

        [switch]$RequireConPty
    )

    $sourceDir = Join-Path $WorktreePath 'code'
    $chmPath = Join-Path $sourceDir 'doc\putty.chm'
    if (-not (Test-Path -LiteralPath $chmPath -PathType Leaf)) {
        throw "Embedded CHM file not found: $chmPath"
    }

    $buildDir = Join-Path $ReleaseBuildRoot $BuildName
    if (Test-Path -LiteralPath $buildDir) {
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }

    Write-Host "=== Configuring $BuildName ($CMakePlatform) ==="
    $configureArgs = @(
        '-S', $sourceDir,
        '-A', $CMakePlatform,
        '-B', $buildDir,
        '-D', 'PUTTY_CAC=1',
        '-D', "PUTTY_EMBEDDED_CHM_FILE=$chmPath"
    )
    $configureLog = @(Invoke-NativeCommand -FilePath 'cmake.exe' -Arguments $configureArgs -CaptureOutput -MergeStderr -EchoCaptured)

    foreach ($line in $configureLog) {
        if ($line.IndexOf($ConPtyMissingMessage, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "Configure check failed for ${BuildName}: $ConPtyMissingMessage"
        }
    }

    if ($RequireConPty) {
        $cachePath = Join-Path $buildDir 'CMakeCache.txt'
        if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
            throw "CMakeCache.txt not generated at: $cachePath"
        }
        $conPtyLine = @(Get-Content -LiteralPath $cachePath | Where-Object { $_ -match '^HAVE_CONPTY:INTERNAL=' })
        if ($conPtyLine.Count -eq 0 -or $conPtyLine[0] -ne 'HAVE_CONPTY:INTERNAL=1') {
            throw "HAVE_CONPTY is not enabled in $cachePath; cannot build Windows pterm."
        }
    }

    Write-Host "=== Building $BuildName (Targets: $($Targets -join ', ')) ==="
    $buildArgs = @('--build', $buildDir, '--config', 'Release')
    if ($UseParallel) {
        $buildArgs += '--parallel'
    }
    $buildArgs += '--target'
    $buildArgs += $Targets
    $buildArgs += '--'
    $buildArgs += '/nr:false'
    if (-not $UseParallel) {
        $buildArgs += '/m:1'
    }

    Invoke-NativeCommand -FilePath 'cmake.exe' -Arguments $buildArgs | Out-Host

    $releaseOutputDir = Join-Path $buildDir 'Release'
    if (-not (Test-Path -LiteralPath $releaseOutputDir -PathType Container)) {
        throw "Release output directory not found: $releaseOutputDir"
    }
    return $releaseOutputDir
}

function Test-StaticRuntimeLinkage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ArchBinDir
    )

    $exeFiles = @(Get-ChildItem -LiteralPath $ArchBinDir -Filter '*.exe' -File | Sort-Object Name)
    if ($exeFiles.Count -eq 0) {
        throw "No executables found for dumpbin verification in: $ArchBinDir"
    }

    $combinedPattern = '(' + ($ForbiddenRuntimePatterns -join '|') + ')'
    foreach ($exe in $exeFiles) {
        $dumpLines = @(Invoke-NativeCommand -FilePath 'dumpbin.exe' -Arguments @('/NOLOGO', '/DEPENDENTS', $exe.FullName) -CaptureOutput -MergeStderr)
        $inSummary = $false
        foreach ($line in $dumpLines) {
            $trimmed = $line.Trim()
            if ([string]::IsNullOrWhiteSpace($trimmed)) { continue }
            if ($trimmed -match '^Dump of file\b') { continue }
            if ($trimmed -match '^File Type:') { continue }
            if ($trimmed -eq 'Summary') {
                $inSummary = $true
                continue
            }
            if ($inSummary) { continue }

            if ($trimmed -imatch $combinedPattern) {
                throw "Static runtime verification failed: '$($exe.FullName)' depends on '$trimmed'. Rebuild with static runtime."
            }
        }
    }
    Write-Host "Static runtime verification passed for $($exeFiles.Count) executables in $ArchBinDir"
}

function New-ReleaseZip {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ArchBinDir,

        [Parameter(Mandatory = $true)]
        [string]$ZipPath,

        [Parameter(Mandatory = $true)]
        [string[]]$ExpectedFiles
    )

    $actualFiles = @(Get-ChildItem -LiteralPath $ArchBinDir -File | Sort-Object Name | ForEach-Object { $_.Name })
    $sortedExpected = @($ExpectedFiles | Sort-Object)
    $diff = Compare-Object -ReferenceObject $sortedExpected -DifferenceObject $actualFiles
    if ($null -ne $diff -and @($diff).Count -gt 0) {
        throw "Unexpected file set in ${ArchBinDir}. Expected: $($sortedExpected -join ', '); Actual: $($actualFiles -join ', ')"
    }

    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }

    Push-Location -LiteralPath $ArchBinDir
    try {
        Compress-Archive -LiteralPath $actualFiles -DestinationPath $ZipPath -CompressionLevel Optimal -Force
    } finally {
        Pop-Location
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName } | Sort-Object)
        $zipDiff = Compare-Object -ReferenceObject $sortedExpected -DifferenceObject $entryNames
        if ($null -ne $zipDiff -and @($zipDiff).Count -gt 0) {
            throw "ZIP root entries mismatch in ${ZipPath}. Actual: $($entryNames -join ', ')"
        }
    } finally {
        $archive.Dispose()
    }

    Write-Host "Created ZIP archive: $ZipPath ($($actualFiles.Count) files at root)"
}

function Write-ReleaseHashFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinariesDir,

        [Parameter(Mandatory = $true)]
        [string]$VersionToken,

        [Parameter(Mandatory = $true)]
        [string]$BuildDate,

        [Parameter(Mandatory = $true)]
        [string]$MasterRef,

        [Parameter(Mandatory = $true)]
        [string]$MasterSha,

        [Parameter(Mandatory = $true)]
        [string]$MasterTextVer,

        [Parameter(Mandatory = $true)]
        [string]$D2ddwRef,

        [Parameter(Mandatory = $true)]
        [string]$D2ddwSha,

        [Parameter(Mandatory = $true)]
        [string]$D2ddwTextVer,

        [Parameter(Mandatory = $true)]
        [string[]]$BuiltArches
    )

    $hashFilePath = Join-Path $BinariesDir "putty-cac-ex-$VersionToken-hash.txt"
    $binariesPrefix = (Get-NormalizedPath -Path $BinariesDir) + '\'

    $targetFiles = @()
    foreach ($a in @('arm64', 'x64')) {
        $zipCandidate = Join-Path $BinariesDir "putty-cac-ex-$VersionToken-$a.zip"
        $archDirCandidate = Join-Path $BinariesDir $a
        if (($BuiltArches -contains $a) -or (Test-Path -LiteralPath $zipCandidate -PathType Leaf)) {
            if (Test-Path -LiteralPath $archDirCandidate -PathType Container) {
                $targetFiles += @(Get-ChildItem -LiteralPath $archDirCandidate -Filter '*.exe' -File | Sort-Object Name)
            }
            if (Test-Path -LiteralPath $zipCandidate -PathType Leaf) {
                $targetFiles += Get-Item -LiteralPath $zipCandidate
            }
        }
    }

    if ($targetFiles.Count -eq 0) {
        throw "No release artifacts found in $BinariesDir to hash."
    }

    $headerLines = @(
        "PuTTY CAC Ex Release Hashes"
        "Version: $VersionToken"
        "BuildDate: $BuildDate"
        "Master: $MasterRef ($MasterSha) | TEXTVER: $MasterTextVer"
        "D2DDW: $D2ddwRef ($D2ddwSha) | TEXTVER: $D2ddwTextVer"
        "Note: puttytab.exe は putty.exe 専用です（putty-d2ddw.exe には使用できません）。"
        ""
    )

    $sb = New-Object System.Text.StringBuilder
    foreach ($hl in $headerLines) {
        [void]$sb.AppendLine($hl)
    }

    foreach ($algorithm in @('SHA256', 'SHA1', 'MD5')) {
        $hashRows = @(foreach ($file in $targetFiles) {
            $fh = Get-FileHash -LiteralPath $file.FullName -Algorithm $algorithm
            $relPath = $fh.Path
            if ($relPath.StartsWith($binariesPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                $relPath = $relPath.Substring($binariesPrefix.Length)
            }
            [PSCustomObject]@{
                Algorithm = $fh.Algorithm
                Hash      = $fh.Hash
                Path      = $relPath
            }
        })
        $tableText = ($hashRows | Format-Table -AutoSize | Out-String -Width 256).TrimEnd()
        [void]$sb.AppendLine($tableText)
        [void]$sb.AppendLine("")
    }

    # Write UTF-8 with BOM so Windows PowerShell 5.1 Get-Content/Select-String and Notepad read Japanese text cleanly.
    $utf8Bom = New-Object System.Text.UTF8Encoding($true)
    [System.IO.File]::WriteAllText($hashFilePath, $sb.ToString(), $utf8Bom)
    Write-Host "Wrote hash file: $hashFilePath"
}

# --- Main ---

if ($BuildDate -notmatch '^\d{8}$') {
    throw "Invalid -BuildDate '$BuildDate'. Expected yyyyMMdd."
}
$parsedDate = [DateTime]::MinValue
if (-not [DateTime]::TryParseExact($BuildDate, 'yyyyMMdd', [System.Globalization.CultureInfo]::InvariantCulture, [System.Globalization.DateTimeStyles]::None, [ref]$parsedDate)) {
    throw "Invalid calendar date for -BuildDate: '$BuildDate'."
}

$VersionToken = "$UpstreamVersion-ex$BuildDate"
$ScriptDir = $PSScriptRoot
$RepoRoot = Get-NormalizedPath -Path (Join-Path $ScriptDir '..')
$RepoParent = Split-Path -Parent $RepoRoot
$WorktreeBase = Join-Path $RepoParent '_release-wt'
$MasterWorktree = Join-Path $WorktreeBase 'master'
$D2ddwWorktree = Join-Path $WorktreeBase 'mod-d2ddw'
$ReleaseBuildRoot = Join-Path $RepoRoot 'build\release-bins'
$BinariesDir = Join-Path $RepoRoot 'binaries'

$TargetArches = switch ($Arch) {
    'x64'   { @('x64') }
    'arm64' { @('arm64') }
    'both'  { @('x64', 'arm64') }
}

Write-Host "PuTTY CAC Ex Release Build: $VersionToken (Arch: $($TargetArches -join ', '))"

# Log main working tree state without modifying it.
$mainBranch = ((Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'rev-parse', '--abbrev-ref', 'HEAD') -CaptureOutput) -join '').Trim()
$mainHead = ((Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'rev-parse', 'HEAD') -CaptureOutput) -join '').Trim()
$mainStatus = @(Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'status', '--porcelain') -CaptureOutput | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$dirtyDesc = if ($mainStatus.Count -gt 0) { "dirty ($($mainStatus.Count) changes)" } else { 'clean' }
Write-Host "Main working tree: $RepoRoot (branch: $mainBranch, HEAD: $mainHead, status: $dirtyDesc)"

# Verify requested git refs exist before creating worktrees.
$null = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'rev-parse', '--verify', "$MasterRef^{commit}") -CaptureOutput
$null = Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'rev-parse', '--verify', "$D2ddwRef^{commit}") -CaptureOutput

Import-VsDevEnvironment

# Startup worktree inspection and recovery from any interrupted previous run.
$initialWorktrees = @(Get-RegisteredWorktrees -RepoRoot $RepoRoot)
Write-Host "Registered git worktrees at startup: $($initialWorktrees -join '; ')"
Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'worktree', 'prune')
Clear-OwnedWorktrees -RepoRoot $RepoRoot -OwnedPaths @($MasterWorktree, $D2ddwWorktree) -WorktreeBase $WorktreeBase

if ($Arch -eq 'both' -and (Test-Path -LiteralPath $ReleaseBuildRoot)) {
    Remove-Item -LiteralPath $ReleaseBuildRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $ReleaseBuildRoot -Force | Out-Null
New-Item -ItemType Directory -Path $BinariesDir -Force | Out-Null

try {
    New-Item -ItemType Directory -Path $WorktreeBase -Force | Out-Null
    Write-Host "Creating detached worktree for '$MasterRef' at: $MasterWorktree"
    Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'worktree', 'add', '--detach', $MasterWorktree, $MasterRef)

    Write-Host "Creating detached worktree for '$D2ddwRef' at: $D2ddwWorktree"
    Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $RepoRoot, 'worktree', 'add', '--detach', $D2ddwWorktree, $D2ddwRef)

    $masterSha = ((Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $MasterWorktree, 'rev-parse', 'HEAD') -CaptureOutput) -join '').Trim()
    $d2ddwSha = ((Invoke-NativeCommand -FilePath 'git' -Arguments @('-C', $D2ddwWorktree, 'rev-parse', 'HEAD') -CaptureOutput) -join '').Trim()
    $masterTextVer = Get-WorktreeTextVer -WorktreePath $MasterWorktree
    $d2ddwTextVer = Get-WorktreeTextVer -WorktreePath $D2ddwWorktree

    Write-Host "Worktree '$MasterRef' HEAD: $masterSha | TEXTVER: $masterTextVer"
    Write-Host "Worktree '$D2ddwRef' HEAD: $d2ddwSha | TEXTVER: $d2ddwTextVer"

    foreach ($a in $TargetArches) {
        $cmakePlatform = if ($a -eq 'arm64') { 'ARM64' } else { 'x64' }

        $masterReleaseDir = Invoke-WorktreeBuild `
            -BuildName "master-$a" `
            -WorktreePath $MasterWorktree `
            -CMakePlatform $cmakePlatform `
            -ReleaseBuildRoot $ReleaseBuildRoot `
            -Targets $MasterTargets `
            -UseParallel:$Parallel `
            -RequireConPty

        $d2ddwReleaseDir = Invoke-WorktreeBuild `
            -BuildName "d2ddw-$a" `
            -WorktreePath $D2ddwWorktree `
            -CMakePlatform $cmakePlatform `
            -ReleaseBuildRoot $ReleaseBuildRoot `
            -Targets $D2ddwTargets `
            -UseParallel:$Parallel

        $archBinDir = Join-Path $BinariesDir $a
        if (Test-Path -LiteralPath $archBinDir) {
            Remove-Item -LiteralPath $archBinDir -Recurse -Force
        }
        New-Item -ItemType Directory -Path $archBinDir -Force | Out-Null

        foreach ($target in $MasterTargets) {
            $exeName = "$target.exe"
            $srcExe = Join-Path $masterReleaseDir $exeName
            if (-not (Test-Path -LiteralPath $srcExe -PathType Leaf)) {
                throw "Expected master executable not found: $srcExe"
            }
            Copy-Item -LiteralPath $srcExe -Destination (Join-Path $archBinDir $exeName) -Force
        }

        $d2ddwSrcExe = Join-Path $d2ddwReleaseDir 'putty.exe'
        if (-not (Test-Path -LiteralPath $d2ddwSrcExe -PathType Leaf)) {
            throw "Expected mod-d2ddw executable not found: $d2ddwSrcExe"
        }
        Copy-Item -LiteralPath $d2ddwSrcExe -Destination (Join-Path $archBinDir 'putty-d2ddw.exe') -Force

        Test-StaticRuntimeLinkage -ArchBinDir $archBinDir

        $zipPath = Join-Path $BinariesDir "putty-cac-ex-$VersionToken-$a.zip"
        New-ReleaseZip -ArchBinDir $archBinDir -ZipPath $zipPath -ExpectedFiles $ExpectedExeNames
    }

    Write-ReleaseHashFile `
        -BinariesDir $BinariesDir `
        -VersionToken $VersionToken `
        -BuildDate $BuildDate `
        -MasterRef $MasterRef `
        -MasterSha $masterSha `
        -MasterTextVer $masterTextVer `
        -D2ddwRef $D2ddwRef `
        -D2ddwSha $d2ddwSha `
        -D2ddwTextVer $d2ddwTextVer `
        -BuiltArches $TargetArches

    Write-Host "Release packaging completed successfully for $VersionToken ($($TargetArches -join ', '))."
} finally {
    Clear-OwnedWorktrees -RepoRoot $RepoRoot -OwnedPaths @($MasterWorktree, $D2ddwWorktree) -WorktreeBase $WorktreeBase -IgnoreErrors
}
