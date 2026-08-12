# Mission Capture test harness -- shared helpers.
#
# Windows PowerShell 5.1 compatible: no &&, no ternary, no null-coalescing.
# See docs/subsea/testing.md.

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:TestRoot  = Split-Path -Parent $PSScriptRoot
$script:RepoRoot  = (Resolve-Path (Join-Path $script:TestRoot '..\..')).Path
$script:OutRoot   = Join-Path $script:TestRoot 'out'
$script:Failures  = New-Object System.Collections.ArrayList
$script:Results   = New-Object System.Collections.ArrayList
$script:StartedAt = Get-Date

# --- application layout ------------------------------------------------------
# Learned the hard way in task 0.4: the app resolves its data directory relative
# to the working directory, and in portable mode its config root is ../../config
# from the executable -- i.e. the rundir root, not the bin directory.

function Get-RunDir {
    $dir = Join-Path $script:RepoRoot 'build_x64\rundir\RelWithDebInfo'
    if (-not (Test-Path $dir)) { throw "Run directory not found: $dir. Build first." }
    return $dir
}

function Get-AppExe {
    $exe = Join-Path (Get-RunDir) 'bin\64bit\MissionCapture64.exe'
    if (-not (Test-Path $exe)) { throw "Executable not found: $exe. Build first." }
    return $exe
}

function Get-PortableConfigRoot { return Join-Path (Get-RunDir) 'config' }

# The branded subdirectory every config path lands under.
function Get-AppConfigDir {
    return Join-Path (Get-PortableConfigRoot) 'Cyberian Resources\Mission Capture'
}

function New-TestWorkspace {
    param([Parameter(Mandatory)][string]$Name)
    $ws = Join-Path $script:OutRoot ("{0}_{1}" -f $Name, (Get-Date -Format 'yyyyMMdd_HHmmss'))
    New-Item -ItemType Directory -Force -Path $ws | Out-Null
    return $ws
}

# Wipes the portable config so each run starts from a known-empty state.
function Reset-PortableConfig {
    $root = Get-PortableConfigRoot
    if (Test-Path $root) { Remove-Item -Recurse -Force $root }
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    return $root
}

function Copy-Fixture {
    param([Parameter(Mandatory)][string]$Relative)
    $src = Join-Path $script:TestRoot (Join-Path 'fixtures' $Relative)
    if (-not (Test-Path $src)) { throw "Fixture not found: $src" }
    $dst = Get-AppConfigDir
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item -Recurse -Force (Join-Path $src '*') $dst
}

# Runs the app to completion (for --dump-ui-manifest and similar one-shot modes).
function Invoke-App {
    param([string[]]$AppArgs = @(), [int]$TimeoutSec = 120)

    $exe = Get-AppExe
    $all = @('--portable', '--multi', '--disable-updater') + $AppArgs

    $proc = Start-Process -FilePath $exe -ArgumentList $all -PassThru -NoNewWindow `
                          -WorkingDirectory (Split-Path -Parent $exe)
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        Stop-Process -Id $proc.Id -Force
        throw "App did not exit within $TimeoutSec s: $($all -join ' ')"
    }
    # The timed WaitForExit overload can return before the exit code is
    # finalised; the parameterless call flushes it. Without this ExitCode reads
    # as empty and every exit-code assertion silently fails.
    $proc.WaitForExit()
    return [int]$proc.ExitCode
}

# Launches the app and leaves it running.
function Start-App {
    param([string[]]$AppArgs = @(), [int]$ReadyTimeoutSec = 90)

    $exe = Get-AppExe
    $all = @('--portable', '--multi', '--disable-updater') + $AppArgs

    $proc = Start-Process -FilePath $exe -ArgumentList $all -PassThru -NoNewWindow `
                          -WorkingDirectory (Split-Path -Parent $exe)

    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { throw "App exited during startup (code $($proc.ExitCode))" }
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne 0) { return $proc }
        Start-Sleep -Milliseconds 250
    }
    Stop-Process -Id $proc.Id -Force
    throw "App did not present a window within $ReadyTimeoutSec s"
}

function Stop-App {
    param([Parameter(Mandatory)]$Process, [int]$GraceSec = 30, [switch]$Force)
    if ($Process.HasExited) { return $Process.ExitCode }
    if ($Force) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit(10000) | Out-Null
        return -1
    }
    $Process.CloseMainWindow() | Out-Null
    if (-not $Process.WaitForExit($GraceSec * 1000)) {
        Stop-Process -Id $Process.Id -Force
        return -1
    }
    return $Process.ExitCode
}

function Get-LatestLog {
    $logDir = Join-Path (Get-AppConfigDir) 'logs'
    if (-not (Test-Path $logDir)) { return $null }
    $latest = Get-ChildItem $logDir -Filter '*.txt' | Sort-Object LastWriteTime -Desc | Select-Object -First 1
    if ($null -eq $latest) { return $null }
    return $latest.FullName
}

# Copies the whole config tree into the workspace so a failed run can be examined
# after the next test wipes it.
function Save-ConfigToWorkspace {
    param([Parameter(Mandatory)][string]$Workspace, [string]$Label = 'config')
    $src = Get-PortableConfigRoot
    if (-not (Test-Path $src)) { return }
    $dst = Join-Path $Workspace $Label
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item -Recurse -Force (Join-Path $src '*') $dst -ErrorAction SilentlyContinue
}

# Errors that are known, understood, and not ours. Anything else fails the run.
$script:DefaultAllowedLogErrors = @(
    # Upstream bug: the first-run migration renames basic/scenes.json without
    # checking it exists, so every fresh config logs this once. See OI-24.
    'Failed to rename basic scene collection file',
    # Peripheral DeckLink plugins with no locale. See OI-25.
    "Failed to load 'en-US' text for module: 'decklink-",
    'Failed to load .* module',
    'NVENC not available',
    # --dump-ui-manifest returns early from OBSBasic::OBSInit, so the crash
    # handler never records a sentinel location and logs this at shutdown.
    # Confirmed absent from normal runs, so it is an artifact of the test-only
    # flag rather than a product defect. See OI-26.
    'No crash sentinel location set for crash handler'
)

function Assert-NoLogErrors {
    param([string[]]$Allow = @(), [string]$Criterion)

    $log = Get-LatestLog
    if ($null -eq $log) { Add-Failure 'No log file produced' $Criterion; return }

    $patterns = $script:DefaultAllowedLogErrors + $Allow
    $bad = @()
    foreach ($line in (Get-Content $log)) {
        if ($line -notmatch '(?i)\berror\b|\bcrash\b|\bassert') { continue }
        $allowed = $false
        foreach ($p in $patterns) { if ($line -match $p) { $allowed = $true; break } }
        if (-not $allowed) { $bad += $line }
    }

    if ($bad.Count -eq 0) {
        Add-Pass "No unexpected errors in log" $Criterion
    } else {
        Add-Failure ("Log has {0} unexpected error line(s):`n    {1}" -f $bad.Count, ($bad -join "`n    ")) $Criterion
    }
}

# --- assertions --------------------------------------------------------------

function Add-Result {
    param([string]$Status, [string]$Message, [string]$Criterion)
    [void]$script:Results.Add([pscustomobject]@{
        status = $Status; message = $Message; criterion = $Criterion
        at = (Get-Date).ToUniversalTime().ToString('o')
    })
}

function Add-Failure {
    param([string]$Message, [string]$Criterion)
    [void]$script:Failures.Add($Message)
    Add-Result 'fail' $Message $Criterion
    Write-Host "  FAIL  $Message" -ForegroundColor Red
}
function Add-Pass {
    param([string]$Message, [string]$Criterion)
    Add-Result 'pass' $Message $Criterion
    Write-Host "  ok    $Message" -ForegroundColor Green
}
function Add-Skip {
    param([string]$Message, [string]$Criterion)
    Add-Result 'skip' $Message $Criterion
    Write-Host "  SKIP  $Message" -ForegroundColor Yellow
}

function Assert-True {
    param([bool]$Condition, [string]$What, [string]$Criterion)
    if ($Condition) { Add-Pass $What $Criterion } else { Add-Failure $What $Criterion }
}

function Assert-Near {
    param([double]$Actual, [double]$Expected, [double]$Tolerance, [string]$What, [string]$Criterion)
    if ([Math]::Abs($Actual - $Expected) -le $Tolerance) {
        Add-Pass ("{0} ({1} ~= {2})" -f $What, $Actual, $Expected) $Criterion
    } else {
        Add-Failure ("{0}: expected {1} +/- {2}, got {3}" -f $What, $Expected, $Tolerance, $Actual) $Criterion
    }
}

# --- run report --------------------------------------------------------------

function Get-ToolVersions {
    $v = [ordered]@{}
    try { $v.ffmpeg = ((& ffmpeg -version 2>&1) | Select-Object -First 1) } catch { $v.ffmpeg = 'not found' }
    try { $v.cmake  = ((& cmake --version 2>&1) | Select-Object -First 1) } catch { $v.cmake = 'not found' }
    try { $v.commit = (& git -C $script:RepoRoot rev-parse HEAD).Trim() } catch { $v.commit = 'unknown' }
    try { $v.branch = (& git -C $script:RepoRoot rev-parse --abbrev-ref HEAD).Trim() } catch { $v.branch = 'unknown' }
    try { $v.dirty  = [bool](& git -C $script:RepoRoot status --porcelain) } catch { $v.dirty = $null }
    try { $v.os     = (Get-CimInstance Win32_OperatingSystem).Caption } catch { $v.os = 'unknown' }
    try { $v.gpu    = ((Get-CimInstance Win32_VideoController).Name -join '; ') } catch { $v.gpu = 'unknown' }
    $v.psVersion = $PSVersionTable.PSVersion.ToString()
    try { $v.appVersion = (Get-Item (Get-AppExe)).VersionInfo.FileVersion } catch { $v.appVersion = 'not built' }
    return $v
}

function Write-TestSummary {
    param([Parameter(Mandatory)][string]$Suite, [string]$Workspace)

    Write-Host ''
    $failed = $script:Failures.Count
    if ($failed -eq 0) {
        Write-Host "PASS  $Suite" -ForegroundColor Green
    } else {
        Write-Host ("FAIL  {0} ({1} failure(s))" -f $Suite, $failed) -ForegroundColor Red
        foreach ($f in $script:Failures) { Write-Host "  - $f" -ForegroundColor Red }
    }

    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $slug  = ($Suite -replace '[^A-Za-z0-9]', '-').ToLower()
    $dir   = $Workspace
    if (-not $dir) { $dir = $script:OutRoot }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    $tools    = Get-ToolVersions
    $duration = ((Get-Date) - $script:StartedAt).TotalSeconds
    $passed   = @($script:Results | Where-Object { $_.status -eq 'pass' }).Count
    $skipped  = @($script:Results | Where-Object { $_.status -eq 'skip' }).Count

    $criteria = @()
    $tagged = @($script:Results | Where-Object { $_.criterion })
    foreach ($grp in ($tagged | Group-Object criterion)) {
        $status = 'skip'
        if (@($grp.Group | Where-Object { $_.status -eq 'pass' }).Count -gt 0) { $status = 'pass' }
        if (@($grp.Group | Where-Object { $_.status -eq 'fail' }).Count -gt 0) { $status = 'fail' }
        $criteria += [pscustomobject]@{ id = $grp.Name; status = $status; checks = $grp.Count }
    }

    $result = 'pass'
    if ($failed -gt 0) { $result = 'fail' }

    $report = [pscustomobject]@{
        suite = $Suite
        started_utc = $script:StartedAt.ToUniversalTime().ToString('o')
        duration_s = [math]::Round($duration, 1)
        result = $result
        counts = [pscustomobject]@{ pass = $passed; fail = $failed; skip = $skipped }
        environment = $tools
        assertions = @($script:Results)
        criteria = $criteria
    }

    $jsonPath = Join-Path $dir "report_${slug}_$stamp.json"
    $report | ConvertTo-Json -Depth 6 | Out-File -Encoding utf8 $jsonPath

    $md = New-Object System.Collections.ArrayList
    [void]$md.Add("# $Suite")
    [void]$md.Add('')
    [void]$md.Add("**Result:** $($result.ToUpper()) - $passed passed, $failed failed, $skipped skipped")
    [void]$md.Add(("**Started:** {0} - **Duration:** {1:N1}s" -f $report.started_utc, $duration))
    [void]$md.Add('')
    if ($tools.dirty) {
        [void]$md.Add('> **Warning:** the working tree was dirty. This run is not reproducible from the commit alone.')
        [void]$md.Add('')
    }
    [void]$md.Add('## Environment'); [void]$md.Add('')
    [void]$md.Add('| Item | Value |'); [void]$md.Add('|---|---|')
    foreach ($k in $tools.Keys) { [void]$md.Add("| $k | $($tools[$k]) |") }

    if ($criteria.Count -gt 0) {
        [void]$md.Add(''); [void]$md.Add('## Acceptance criteria covered'); [void]$md.Add('')
        [void]$md.Add('| Criterion | Status | Checks |'); [void]$md.Add('|---|---|---|')
        foreach ($c in ($criteria | Sort-Object id)) {
            $icon = '-'
            if ($c.status -eq 'pass') { $icon = 'PASS' }
            if ($c.status -eq 'fail') { $icon = 'FAIL' }
            [void]$md.Add("| $($c.id) | $icon | $($c.checks) |")
        }
    }

    [void]$md.Add(''); [void]$md.Add('## Assertions'); [void]$md.Add('')
    foreach ($r in $script:Results) {
        $icon = '-'
        if ($r.status -eq 'pass') { $icon = '[pass]' }
        if ($r.status -eq 'fail') { $icon = '[FAIL]' }
        if ($r.status -eq 'skip') { $icon = '[skip]' }
        $tag = ''
        if ($r.criterion) { $tag = " _($($r.criterion))_" }
        [void]$md.Add("- $icon $($r.message)$tag")
    }

    $mdPath = Join-Path $dir "report_${slug}_$stamp.md"
    ($md -join "`n") | Out-File -Encoding utf8 $mdPath

    Write-Host "  report: $mdPath" -ForegroundColor Cyan
    return $failed
}
