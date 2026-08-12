# Testing strategy and scripts

Every phase doc references a test script by name. This document holds the strategy, the tooling
prerequisites, and the full text of those scripts, ready to drop into `tools/subsea-tests/`.

Scripts are written for **Windows PowerShell 5.1** (no `&&`, no ternary, no null-coalescing) so
they run on a bare target machine without installing PowerShell 7.

---

## Test levels

| Level | Tool | Runs where | Speed | What it catches |
|---|---|---|---|---|
| **Unit** | cmocka + ctest | Every build, CI | seconds | Parser, registry, format engine, log writer logic |
| **Integration** | PowerShell + the real app | Pre-merge, nightly | minutes | Wiring, persistence, recording correctness |
| **Soak** | PowerShell, long-running | Weekly, pre-release | hours | Leaks, drift, file corruption, thermal effects |
| **Field QA** | A human with a checklist | Pre-release | — | Usability, the things scripts cannot judge |

The integration layer leans on three assertion channels, in order of preference:

1. **obs-websocket vendor API** (`mc-data`, Phase 3 task 3.7) — structured, fast, reliable
2. **`ffprobe`** — the ground truth for anything written to disk
3. **Screenshots compared to golden images** — only for rendering correctness, because golden
   images are brittle. Crop tightly, use a tolerance, and re-bless deliberately

Never assert by scraping the UI with screen automation. It is slow and it breaks constantly.

---

## Tooling prerequisites

| Tool | Why | Install |
|---|---|---|
| **CMocka** | Unit tests (already used by `test/cmocka`) | `vcpkg install cmocka:x64-windows` |
| **ffmpeg / ffprobe** | Verify recordings, generate fixtures | `winget install Gyan.FFmpeg` |
| **com0com** | Virtual COM port pair for serial tests (Phase 5) | [com0com on SourceForge] — needs an unsigned-driver allowance or a signed build |
| **obs-websocket client** | Assertion channel; a small PowerShell wrapper over `System.Net.WebSockets` suffices | In-repo (`lib/websocket.ps1`) |
| **ImageMagick** *(optional)* | `magick compare` for golden-image diffs; a pure-PowerShell pixel comparator is the fallback | `winget install ImageMagick.ImageMagick` |
| **MediaMTX** | Serves the RTSP test-pattern fixture (Phase 2) **and** the WHIP endpoint (Phase 8) | Single Go binary from [bluenviron/mediamtx] releases |

MediaMTX earns its place twice: it gives Phase 2 a deterministic RTSP camera that needs no
hardware, and Phase 8 a WHIP endpoint to stream at. Start it once, use it for both.

Record versions of all of these in the harness output — a test that fails only on one ffmpeg
version is a genuinely confusing afternoon.

---

## Layout

```
tools/subsea-tests/
  run-tests.ps1               Orchestrator
  lib/
    common.ps1                Shared helpers (launch, log parsing, ffprobe, assertions)
    websocket.ps1             Minimal obs-websocket client
    mediamtx.ps1              Start/stop MediaMTX; RTSP fixture and WHIP endpoint helpers
  fixtures/
    jobs/                     Jobs (scene collections) used by tests
    rigs/                     Rigs (profiles), incl. features.ini variants
    data/                     Simulator input files
    golden/                   Golden screenshots and UI manifests
  t0-foundation.ps1
  t1-shell.ps1
  t2-video-elements.ps1
  t3-data-core.ps1
  t4-overlay.ps1
  t5-transports.ps1
  t6-multirecord.ps1
  t7-clips.ps1
  t7-snapshots.ps1
  t8-sidecar.ps1
  t9-streaming.ps1
  out/                        Test artifacts (gitignored)
```

---

## Run reports

**This is the project's only systematic written record of work done**, which makes it worth more
than a pass/fail log. Every suite run writes two files next to its artifacts:

| File | For |
|---|---|
| `report_<suite>_<timestamp>.json` | Tooling, CI, diffing two runs |
| `report_<suite>_<timestamp>.md` | Reading |

Each contains:

- **Result and counts** — pass / fail / skip
- **Environment** — commit, branch, **whether the working tree was dirty**, app version, OS, GPU,
  ffmpeg/ffprobe versions. A run against a dirty tree is not reproducible from the commit alone and
  the report says so in a warning box
- **Acceptance criteria covered** — see below
- **Every assertion**, in order, with its message and timestamp

`run-tests.ps1` additionally writes `out/run_<timestamp>/index.md` rolling up all suites in that
invocation.

### Criterion IDs — what makes these reports evidence

Assertions can carry a `-Criterion` tag naming an acceptance criterion from a phase doc:

```powershell
Assert-Near ([double]$info.format.duration) 30 0.5 "$name duration" -Criterion 'P6-AC3'
```

The report then contains a criteria table — which acceptance criteria have passing evidence from
this run, which failed, which were skipped. That turns "is Phase 6 done?" from an archaeology
exercise into reading one table.

**Convention:** `P<phase>-AC<n>`, where *n* is the position of the checkbox in that phase doc's
**Acceptance criteria** section. IDs get stamped into the phase doc's checkboxes when that phase's
tests are written — about five minutes per phase — rather than now, while the lists are still
moving. Untagged assertions are perfectly fine; they just don't appear in the criteria table.

Since you've opted out of phase completion reports, this table is the closest thing to one. It
covers verification and deliberately says nothing about what was cut, deferred, or changed from the
plan — that gap is covered instead by [PROGRESS.md](PROGRESS.md)'s Notes column and open-items
register, which is updated after every task.

---

**Every script must run against a throwaway portable config directory** (`--portable --multi`
with a temp working directory) so a test run can never damage the developer's real profiles or
scene collections. `common.ps1` enforces this.

---

## `lib/common.ps1`

```powershell
# tools/subsea-tests/lib/common.ps1
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:TestRoot  = Split-Path -Parent $PSScriptRoot
$script:RepoRoot  = Resolve-Path (Join-Path $script:TestRoot '..\..')
$script:OutRoot   = Join-Path $script:TestRoot 'out'
$script:Failures  = New-Object System.Collections.ArrayList
$script:StartedAt = Get-Date

function Get-AppExe {
    $candidates = @(
        (Join-Path $script:RepoRoot 'build_x64\rundir\RelWithDebInfo\bin\64bit'),
        (Join-Path $script:RepoRoot 'build_x64\rundir\Debug\bin\64bit')
    )
    foreach ($dir in $candidates) {
        if (Test-Path $dir) {
            $exe = Get-ChildItem -Path $dir -Filter '*.exe' |
                   Where-Object { $_.Name -notmatch 'crash|helper' } |
                   Select-Object -First 1
            if ($exe) { return $exe.FullName }
        }
    }
    throw "Application executable not found. Build first."
}

function New-TestWorkspace {
    param([Parameter(Mandatory)][string]$Name)
    $ws = Join-Path $script:OutRoot ("{0}_{1}" -f $Name, (Get-Date -Format 'yyyyMMdd_HHmmss'))
    New-Item -ItemType Directory -Force -Path $ws | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $ws 'config') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $ws 'rec') | Out-Null
    return $ws
}

function Copy-Fixture {
    param([Parameter(Mandatory)][string]$Relative,
          [Parameter(Mandatory)][string]$Workspace)
    $src = Join-Path $script:TestRoot (Join-Path 'fixtures' $Relative)
    if (-not (Test-Path $src)) { throw "Fixture not found: $src" }
    Copy-Item -Recurse -Force $src (Join-Path $Workspace 'config')
}

# Launches the app in portable mode against the workspace config dir.
# Portable mode keeps the developer's real %APPDATA% untouched.
function Start-App {
    param(
        [Parameter(Mandatory)][string]$Workspace,
        [string[]]$AppArgs = @(),
        [int]$ReadyTimeoutSec = 60
    )
    $exe = Get-AppExe
    $exeDir = Split-Path -Parent $exe

    # Portable mode reads config from a 'config' dir beside the exe; symlink ours in.
    $portableCfg = Join-Path $exeDir 'config'
    if (Test-Path $portableCfg) { Remove-Item -Recurse -Force $portableCfg }
    New-Item -ItemType SymbolicLink -Path $portableCfg -Target (Join-Path $Workspace 'config') | Out-Null

    $all = @('--portable', '--multi', '--verbose', '--disable-updater') + $AppArgs
    $proc = Start-Process -FilePath $exe -ArgumentList $all -PassThru -WorkingDirectory $exeDir

    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { throw "App exited during startup (code $($proc.ExitCode))." }
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne 0) { return $proc }
        Start-Sleep -Milliseconds 250
    }
    throw "App did not present a window within $ReadyTimeoutSec s."
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
        throw "App did not exit gracefully within $GraceSec s."
    }
    return $Process.ExitCode
}

function Get-AppLog {
    param([Parameter(Mandatory)][string]$Workspace)
    $logDir = Join-Path $Workspace 'config\Mission Capture\logs'
    if (-not (Test-Path $logDir)) { return $null }
    $latest = Get-ChildItem $logDir -Filter '*.txt' | Sort-Object LastWriteTime -Desc | Select-Object -First 1
    if ($null -eq $latest) { return $null }
    return Get-Content $latest.FullName -Raw
}

function Assert-NoLogErrors {
    param([Parameter(Mandatory)][string]$Workspace, [string[]]$Allow = @())
    $log = Get-AppLog -Workspace $Workspace
    if ($null -eq $log) { Add-Failure 'No log file produced'; return }
    $bad = $log -split "`n" | Where-Object {
        $line = $_
        if ($line -notmatch '(?i)\b(error|failed|crash|assert)\b') { return $false }
        foreach ($a in $Allow) { if ($line -match $a) { return $false } }
        return $true
    }
    if ($bad.Count -gt 0) {
        Add-Failure ("Log contains {0} error line(s):`n  {1}" -f $bad.Count, ($bad -join "`n  "))
    }
}

# --- ffprobe helpers -------------------------------------------------------

function Get-MediaInfo {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path $Path)) { throw "Media file not found: $Path" }
    $json = & ffprobe -v error -print_format json -show_format -show_streams -- "$Path"
    if ($LASTEXITCODE -ne 0) { throw "ffprobe failed for $Path" }
    return $json | ConvertFrom-Json
}

function Get-VideoFrameCount {
    param([Parameter(Mandatory)][string]$Path)
    $n = & ffprobe -v error -select_streams v:0 -count_frames `
                   -show_entries stream=nb_read_frames -of csv=p=0 -- "$Path"
    return [int]$n
}

# Average colour of a single extracted frame; used to prove content routing.
function Get-FrameDominantColor {
    param([Parameter(Mandatory)][string]$Path,
          [Parameter(Mandatory)][double]$AtSeconds,
          [int[]]$Crop = $null)      # x,y,w,h
    $tmp = Join-Path $script:OutRoot ("frame_{0}.png" -f ([guid]::NewGuid().ToString('N')))
    $filters = @()
    if ($Crop) { $filters += ("crop={0}:{1}:{2}:{3}" -f $Crop[2], $Crop[3], $Crop[0], $Crop[1]) }
    $filters += 'scale=1:1'
    $vf = $filters -join ','
    & ffmpeg -v error -ss $AtSeconds -i "$Path" -frames:v 1 -vf $vf -y "$tmp" | Out-Null
    if (-not (Test-Path $tmp)) { throw "Frame extraction failed at ${AtSeconds}s of $Path" }
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($tmp)
    try { $px = $bmp.GetPixel(0, 0) } finally { $bmp.Dispose() }
    Remove-Item $tmp -Force
    return @{ R = $px.R; G = $px.G; B = $px.B }
}

function Assert-ColorNear {
    param($Actual, [int[]]$Expected, [int]$Tolerance = 24, [string]$What = 'colour')
    $d = [Math]::Max([Math]::Abs($Actual.R - $Expected[0]),
         [Math]::Max([Math]::Abs($Actual.G - $Expected[1]),
                     [Math]::Abs($Actual.B - $Expected[2])))
    if ($d -gt $Tolerance) {
        Add-Failure ("{0}: expected RGB({1}) got RGB({2},{3},{4}), delta {5} > {6}" -f `
                     $What, ($Expected -join ','), $Actual.R, $Actual.G, $Actual.B, $d, $Tolerance)
    }
}

# --- assertions ------------------------------------------------------------
# Every assertion records a structured result. -Criterion optionally ties it to
# an acceptance-criterion ID from a phase doc (e.g. 'P6-AC3'), which is what
# makes the run report double as evidence of a criterion being met.

$script:Results = New-Object System.Collections.ArrayList

function Add-Result {
    param([string]$Status, [string]$Message, [string]$Criterion)
    [void]$script:Results.Add([pscustomobject]@{
        status    = $Status
        message   = $Message
        criterion = $Criterion
        at        = (Get-Date).ToUniversalTime().ToString('o')
    })
}

function Add-Failure {
    param([string]$Message, [string]$Criterion)
    [void]$script:Failures.Add($Message)
    Add-Result -Status 'fail' -Message $Message -Criterion $Criterion
    Write-Host "  FAIL  $Message" -ForegroundColor Red
}
function Add-Pass {
    param([string]$Message, [string]$Criterion)
    Add-Result -Status 'pass' -Message $Message -Criterion $Criterion
    Write-Host "  ok    $Message" -ForegroundColor Green
}
function Add-Skip {
    param([string]$Message, [string]$Criterion)
    Add-Result -Status 'skip' -Message $Message -Criterion $Criterion
    Write-Host "  SKIP  $Message" -ForegroundColor Yellow
}

function Assert-True {
    param([bool]$Condition, [string]$What, [string]$Criterion)
    if ($Condition) { Add-Pass $What $Criterion } else { Add-Failure $What $Criterion }
}

function Assert-Near {
    param([double]$Actual, [double]$Expected, [double]$Tolerance, [string]$What, [string]$Criterion)
    $d = [Math]::Abs($Actual - $Expected)
    if ($d -le $Tolerance) { Add-Pass ("{0} ({1} ~= {2})" -f $What, $Actual, $Expected) $Criterion }
    else { Add-Failure ("{0}: expected {1} +/- {2}, got {3}" -f $What, $Expected, $Tolerance, $Actual) $Criterion }
}

# --- helpers still to be written -------------------------------------------
# The image and websocket helpers below are referenced by the later suites and
# are implemented as part of the phase that first needs each:
#   Connect-ObsWebSocket / Invoke-ObsRequest / Invoke-ObsVendor /
#   Get-SourceScreenshot / Disconnect-ObsWebSocket
#       -> Phase 3 (lib/websocket.ps1, over System.Net.WebSockets.ClientWebSocket)
#   Assert-ImageMatch / Test-ImageIdentical / Get-ImageHash / Test-ColorNear
#       -> Phase 4 (golden-image comparison, System.Drawing or `magick compare`)
#   Read-BurnedTimestamp / Read-BurnedClock
#       -> Phase 2 (template-match pre-rendered glyphs at a known font/size)
#   Get-ImageRegionColor / Get-PngTextChunks
#       -> Phase 7 (crop-average a PNG on disk; read PNG tEXt metadata)

function Get-FailureCount { return $script:Failures.Count }

# --- run report -------------------------------------------------------------
# Writes JSON (for tooling) and Markdown (for humans) next to the run's
# artifacts, then returns the failure count as the exit code.

function Get-ToolVersions {
    $v = @{}
    try { $v.ffmpeg = (& ffmpeg  -version 2>&1 | Select-Object -First 1) } catch { $v.ffmpeg = 'not found' }
    try { $v.ffprobe = (& ffprobe -version 2>&1 | Select-Object -First 1) } catch { $v.ffprobe = 'not found' }
    try { $v.commit = (& git -C $script:RepoRoot rev-parse HEAD).Trim() } catch { $v.commit = 'unknown' }
    try { $v.branch = (& git -C $script:RepoRoot rev-parse --abbrev-ref HEAD).Trim() } catch { $v.branch = 'unknown' }
    try { $v.dirty  = [bool](& git -C $script:RepoRoot status --porcelain) } catch { $v.dirty = $null }
    $v.os        = (Get-CimInstance Win32_OperatingSystem).Caption
    $v.gpu       = ((Get-CimInstance Win32_VideoController).Name -join '; ')
    $v.psVersion = $PSVersionTable.PSVersion.ToString()
    try { $v.appExe = Get-AppExe; $v.appVersion = (Get-Item $v.appExe).VersionInfo.FileVersion } catch { }
    return $v
}

function Write-TestSummary {
    param(
        [Parameter(Mandatory)][string]$Suite,
        [string]$Workspace,
        [datetime]$StartedAt = $script:StartedAt
    )
    Write-Host ''
    $failed = $script:Failures.Count
    if ($failed -eq 0) {
        Write-Host "PASS  $Suite" -ForegroundColor Green
    } else {
        Write-Host ("FAIL  {0} ({1} failure(s))" -f $Suite, $failed) -ForegroundColor Red
        foreach ($f in $script:Failures) { Write-Host "  - $f" -ForegroundColor Red }
    }

    $stamp    = (Get-Date -Format 'yyyyMMdd_HHmmss')
    $slug     = ($Suite -replace '[^A-Za-z0-9]', '-').ToLower()
    $reportIn = if ($Workspace) { $Workspace } else { $script:OutRoot }
    New-Item -ItemType Directory -Force -Path $reportIn | Out-Null

    $tools    = Get-ToolVersions
    $duration = if ($StartedAt) { ((Get-Date) - $StartedAt).TotalSeconds } else { $null }
    $passed   = ($script:Results | Where-Object status -eq 'pass').Count
    $skipped  = ($script:Results | Where-Object status -eq 'skip').Count

    $report = [pscustomobject]@{
        suite       = $Suite
        started_utc = if ($StartedAt) { $StartedAt.ToUniversalTime().ToString('o') } else { $null }
        duration_s  = $duration
        result      = if ($failed -eq 0) { 'pass' } else { 'fail' }
        counts      = @{ pass = $passed; fail = $failed; skip = $skipped }
        environment = $tools
        workspace   = $Workspace
        assertions  = @($script:Results)
        criteria    = @($script:Results | Where-Object { $_.criterion } |
                        Group-Object criterion | ForEach-Object {
                            [pscustomobject]@{
                                id     = $_.Name
                                status = if ($_.Group | Where-Object status -eq 'fail') { 'fail' }
                                         elseif ($_.Group | Where-Object status -eq 'pass') { 'pass' }
                                         else { 'skip' }
                                checks = $_.Count
                            }
                        })
    }

    $jsonPath = Join-Path $reportIn "report_${slug}_$stamp.json"
    $report | ConvertTo-Json -Depth 6 | Out-File -Encoding utf8 $jsonPath

    $md = New-Object System.Collections.ArrayList
    [void]$md.Add("# $Suite")
    [void]$md.Add('')
    [void]$md.Add("**Result:** $($report.result.ToUpper()) — $passed passed, $failed failed, $skipped skipped")
    [void]$md.Add(("**Started:** {0}  ·  **Duration:** {1:N1}s" -f $report.started_utc, $duration))
    [void]$md.Add('')
    [void]$md.Add('## Environment')
    [void]$md.Add('')
    [void]$md.Add('| Item | Value |'); [void]$md.Add('|---|---|')
    foreach ($k in ($tools.Keys | Sort-Object)) { [void]$md.Add("| $k | $($tools[$k]) |") }
    if ($tools.dirty) { [void]$md.Add(''); [void]$md.Add('> ⚠ Working tree was dirty — this run is not reproducible from the commit alone.') }

    if ($report.criteria.Count -gt 0) {
        [void]$md.Add(''); [void]$md.Add('## Acceptance criteria covered'); [void]$md.Add('')
        [void]$md.Add('| Criterion | Status | Checks |'); [void]$md.Add('|---|---|---|')
        foreach ($c in ($report.criteria | Sort-Object id)) {
            $icon = switch ($c.status) { 'pass' { '✅' } 'fail' { '❌' } default { '⏭' } }
            [void]$md.Add("| $($c.id) | $icon $($c.status) | $($c.checks) |")
        }
    }

    [void]$md.Add(''); [void]$md.Add('## Assertions'); [void]$md.Add('')
    foreach ($r in $script:Results) {
        $icon = switch ($r.status) { 'pass' { '✅' } 'fail' { '❌' } default { '⏭' } }
        $tag  = if ($r.criterion) { " _($($r.criterion))_" } else { '' }
        [void]$md.Add("- $icon $($r.message)$tag")
    }

    $mdPath = Join-Path $reportIn "report_${slug}_$stamp.md"
    $md -join "`n" | Out-File -Encoding utf8 $mdPath

    Write-Host "  report: $mdPath" -ForegroundColor Cyan
    return $failed
}
```

---

## `run-tests.ps1`

```powershell
# tools/subsea-tests/run-tests.ps1
[CmdletBinding()]
param(
    [ValidateSet('0','1','2','3','4','5','6','7','8','9','all','unit','quick')]
    [string]$Suite = 'quick',
    [switch]$SkipBuild,
    [switch]$IncludeSoak
)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo   = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$build  = Join-Path $repo 'build_x64'
$total  = 0

$runStamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$runDir   = Join-Path (Join-Path $PSScriptRoot 'out') "run_$runStamp"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$runLog   = New-Object System.Collections.ArrayList

Write-Host "=== Environment ===" -ForegroundColor Cyan
Write-Host ("  ffmpeg : {0}" -f ((& ffmpeg -version 2>&1 | Select-Object -First 1)))
Write-Host ("  cmake  : {0}" -f ((& cmake --version 2>&1 | Select-Object -First 1)))
Write-Host ("  commit : {0}" -f (& git -C $repo rev-parse --short HEAD))

if (-not $SkipBuild) {
    Write-Host "`n=== Build ===" -ForegroundColor Cyan
    & cmake --preset windows-subsea-x64
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    & cmake --build --preset windows-subsea-x64 --config RelWithDebInfo
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
}

Write-Host "`n=== Unit tests (ctest) ===" -ForegroundColor Cyan
Push-Location $build
try {
    & ctest --output-on-failure -C RelWithDebInfo
    if ($LASTEXITCODE -ne 0) { $total += 1; Write-Host 'ctest FAILED' -ForegroundColor Red }
} finally { Pop-Location }
if ($Suite -eq 'unit') { exit $total }

$map = @{
    '0' = 't0-foundation.ps1';     '1' = 't1-shell.ps1'
    '2' = 't2-video-elements.ps1'; '3' = 't3-data-core.ps1'
    '4' = 't4-overlay.ps1';        '5' = 't5-transports.ps1'
    '6' = 't6-multirecord.ps1';    '7' = @('t7-clips.ps1','t7-snapshots.ps1')
    '8' = 't8-sidecar.ps1';        '9' = 't9-streaming.ps1'
}
if ($Suite -eq 'all')        { $run = '0','1','2','3','4','5','6','7','8','9' }
elseif ($Suite -eq 'quick')  { $run = '0','1','3','4','6' }   # no hardware, no long soaks
else                         { $run = @($Suite) }

foreach ($s in $run) {
    foreach ($name in @($map[$s])) {          # some suites have more than one script
        $script = Join-Path $PSScriptRoot $name
        if (-not (Test-Path $script)) {
            Write-Host "skip: $name not present yet" -ForegroundColor Yellow
            [void]$runLog.Add([pscustomobject]@{ suite = $s; script = $name; result = 'absent'; failures = 0 })
            continue
        }
        Write-Host "`n=== Suite $s : $name ===" -ForegroundColor Cyan
        $t0 = Get-Date
        & $script -IncludeSoak:$IncludeSoak
        $rc = $LASTEXITCODE
        $total += $rc
        [void]$runLog.Add([pscustomobject]@{
            suite = $s; script = $name
            result = $(if ($rc -eq 0) { 'pass' } else { 'fail' })
            failures = $rc; duration_s = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
        })
    }
}

# --- roll-up index ---------------------------------------------------------
$idx = @("# Test run $runStamp", '',
         ("Commit ``{0}`` on ``{1}``" -f (& git -C $repo rev-parse --short HEAD),
                                          (& git -C $repo rev-parse --abbrev-ref HEAD)), '',
         '| Suite | Script | Result | Failures | Duration |', '|---|---|---|---|---|')
foreach ($r in $runLog) {
    $icon = switch ($r.result) { 'pass' { '✅' } 'fail' { '❌' } default { '⏭' } }
    $idx += "| $($r.suite) | $($r.script) | $icon $($r.result) | $($r.failures) | $($r.duration_s)s |"
}
$idx += @('', "**Total failures: $total**", '',
          'Per-suite reports are written alongside each suite''s workspace under `out/`.')
$idx -join "`n" | Out-File -Encoding utf8 (Join-Path $runDir 'index.md')
$runLog | ConvertTo-Json -Depth 4 | Out-File -Encoding utf8 (Join-Path $runDir 'index.json')

Write-Host ''
Write-Host "roll-up: $(Join-Path $runDir 'index.md')" -ForegroundColor Cyan
if ($total -eq 0) { Write-Host 'ALL SUITES PASSED' -ForegroundColor Green }
else              { Write-Host "$total FAILURE(S)" -ForegroundColor Red }
exit $total
```

---

## T0 — Foundation smoke

`tools/subsea-tests/t0-foundation.ps1`

```powershell
param([switch]$IncludeSoak)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$ws = New-TestWorkspace -Name 't0'

# --- 1. Branding: no 'OBS' in the shipped product identity -----------------
$exe = Get-AppExe
$vi  = (Get-Item $exe).VersionInfo
Assert-True ($vi.ProductName   -notmatch '(?i)obs') "ProductName is rebranded ('$($vi.ProductName)')"
Assert-True ($vi.FileDescription -notmatch '(?i)obs') "FileDescription is rebranded"
Assert-True ((Split-Path -Leaf $exe) -notmatch '(?i)^obs') "Executable name is rebranded"

# --- 2. Launch smoke -------------------------------------------------------
$proc = Start-App -Workspace $ws
Start-Sleep -Seconds 5
$code = Stop-App -Process $proc
Assert-True ($code -eq 0) "Clean exit (code $code)"
Assert-NoLogErrors -Workspace $ws -Allow @(
    'Failed to load .* module',
    'NVENC not available',
    # Upstream bug: the first-run scene-collection migration renames basic/scenes.json
    # without checking it exists, so a fresh config always logs this once. Left
    # unpatched deliberately -- it is cosmetic and patching it would touch an
    # upstream file for no functional gain. Candidate for an upstream PR.
    'Failed to rename basic scene collection file',
    "Failed to load 'en-US' text for module: 'decklink-(captions|output-ui)"
)

# --- 3. Config isolation ---------------------------------------------------
Assert-True (Test-Path (Join-Path $ws 'config')) 'Portable config directory was used'

# --- 4. UI manifest dump ---------------------------------------------------
$manifest = Join-Path $ws 'ui-manifest.json'
$p = Start-App -Workspace $ws -AppArgs @('--dump-ui-manifest', $manifest)
$p.WaitForExit(30000) | Out-Null
Assert-True (Test-Path $manifest) 'UI manifest produced'
if (Test-Path $manifest) {
    $m = Get-Content $manifest -Raw | ConvertFrom-Json
    Assert-True ($m.actions.Count -gt 0) "Manifest lists $($m.actions.Count) actions"
}

# --- 5. Feature flags actually flag ----------------------------------------
foreach ($state in @('true','false')) {
    Set-Content -Encoding utf8 -Path (Join-Path $ws 'config\features.ini') `
                -Value "[Features]`nStudioMode=$state`n"
    $out = Join-Path $ws "ui-manifest-$state.json"
    $p = Start-App -Workspace $ws -AppArgs @('--dump-ui-manifest', $out)
    $p.WaitForExit(30000) | Out-Null
}
$on  = (Get-Content (Join-Path $ws 'ui-manifest-true.json')  -Raw | ConvertFrom-Json).actions
$off = (Get-Content (Join-Path $ws 'ui-manifest-false.json') -Raw | ConvertFrom-Json).actions
Assert-True (($on  | Where-Object { $_.name -eq 'actionStudioMode' }).Count -eq 1) 'StudioMode visible when enabled'
Assert-True (($off | Where-Object { $_.name -eq 'actionStudioMode' }).Count -eq 0) 'StudioMode hidden when disabled'

exit (Write-TestSummary -Suite 'T0 Foundation')
```

---

## T1 — Shell and Layers

`tools/subsea-tests/t1-shell.ps1` — abbreviated; the golden-diff and crash-recovery cases are the
ones that matter.

```powershell
param([switch]$IncludeSoak)
Set-StrictMode -Version 2.0; $ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$ws = New-TestWorkspace -Name 't1'
Copy-Fixture -Relative 'rigs\inspection' -Workspace $ws
Copy-Fixture -Relative 'jobs\three-canvas' -Workspace $ws   # 3 Canvases x 2 Elements

# --- 0a. Layers tree structure --------------------------------------------
# The tree is the phase's central deliverable, so assert its shape directly
# rather than inferring it from screenshots.
$proc = Start-App -Workspace $ws -AppArgs @('--collection','three-canvas')
$ws4  = Connect-ObsWebSocket -Port 4455
try {
    $tree = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'GetTree'
    Assert-True ($tree.canvases.Count -eq 3) "Three Canvas rows (got $($tree.canvases.Count))"
    foreach ($c in $tree.canvases) {
        Assert-True ($c.elements.Count -eq 2) "$($c.name) has 2 Element rows"
    }

    # --- 0b. Z-order: tree row 0 must be the TOPMOST drawn element ---------
    # libobs stores items bottom-first; the tree shows top-first. This assert
    # is the guard against the classic inverted-index bug.
    $first = $tree.canvases[0]
    Assert-True ($first.elements[0].name -eq 'Overlay') 'Tree row 0 is the topmost (overlay) Element'
    $obsOrder = Invoke-ObsRequest -Socket $ws4 -Type 'GetSceneItemList' `
                                  -Data @{ sceneName = $first.name }
    Assert-True ($obsOrder.sceneItems[-1].sourceName -eq $first.elements[0].name) `
                'Tree row 0 corresponds to the LAST libobs scene item'

    # --- 0c. Reorder round-trips ------------------------------------------
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'MoveElement' `
                     -Data @{ canvas = $first.name; from = 0; to = 1 } | Out-Null
    $tree2 = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'GetTree'
    Assert-True ($tree2.canvases[0].elements[1].name -eq 'Overlay') 'Reorder reflected in the tree'

    # --- 0d. Terminology sweep --------------------------------------------
    $strings = (Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'GetVisibleStrings').strings
    foreach ($banned in @('Scene', 'Source', 'Scene Collection', 'Profile')) {
        $hits = $strings | Where-Object { $_ -match [regex]::Escape($banned) }
        Assert-True ($hits.Count -eq 0) ("No visible string contains '$banned'" +
                    $(if ($hits) { ": " + ($hits -join '; ') } else { '' }))
    }
}
finally { Disconnect-ObsWebSocket -Socket $ws4; Stop-App -Process $proc | Out-Null }

# --- 1. UI manifest golden diff -------------------------------------------
$manifest = Join-Path $ws 'ui.json'
$p = Start-App -Workspace $ws -AppArgs @('--dump-ui-manifest', $manifest); $p.WaitForExit(30000) | Out-Null
$actual = (Get-Content $manifest -Raw | ConvertFrom-Json).actions.name | Sort-Object
$golden = (Get-Content (Join-Path $PSScriptRoot 'fixtures\golden\ui-manifest.golden.json') -Raw |
           ConvertFrom-Json).actions.name | Sort-Object
$extra   = Compare-Object $golden $actual | Where-Object { $_.SideIndicator -eq '=>' }
$missing = Compare-Object $golden $actual | Where-Object { $_.SideIndicator -eq '<=' }
Assert-True ($extra.Count -eq 0)   ("No unexpected UI actions" + $(if ($extra) { ": " + ($extra.InputObject -join ', ') } else { '' }))
Assert-True ($missing.Count -eq 0) ("No missing UI actions"    + $(if ($missing) { ": " + ($missing.InputObject -join ', ') } else { '' }))

# --- 2. Hotkey leakage -----------------------------------------------------
$hk = Join-Path $ws 'config\Mission Capture\basic\profiles\inspection\basic.ini'
$hkText = if (Test-Path $hk) { Get-Content $hk -Raw } else { '' }
foreach ($hidden in @('ReplayBuffer','VirtualCam','StudioMode')) {
    Assert-True ($hkText -notmatch $hidden) "No hotkey binding persisted for hidden feature $hidden"
}

# --- 3. Defaults -----------------------------------------------------------
$ini = Get-Content $hk -Raw
Assert-True ($ini -match 'RecFormat2?=mkv') 'MKV container by default'
Assert-True ($ini -match 'FilenameFormatting=.*%CANVAS%|FilenameFormatting=.*%JOB%')   'Job-aware filename template'

# --- 4. Crash recovery: the highest-value test in this suite ---------------
$recDir = Join-Path $ws 'rec'
$proc = Start-App -Workspace $ws -AppArgs @('--startrecording')
Start-Sleep -Seconds 20
Stop-App -Process $proc -Force | Out-Null
Start-Sleep -Seconds 2
$orphan = Get-ChildItem $recDir -File | Sort-Object LastWriteTime -Desc | Select-Object -First 1
Assert-True ($null -ne $orphan) 'A recording file exists after a hard kill'
if ($orphan) {
    $info = Get-MediaInfo -Path $orphan.FullName
    Assert-Near ([double]$info.format.duration) 20 5 'Killed recording has a plausible duration'
    Assert-True ($info.streams.Count -ge 1) 'Killed recording is probe-able and has streams'
}

exit (Write-TestSummary -Suite 'T1 Shell and Layers')
```

---

## T2 — Video elements

`tools/subsea-tests/t2-video-elements.ps1`

**Fixture:** MediaMTX plus an `ffmpeg` test pattern gives a deterministic RTSP camera with no
hardware. The pattern carries a burned-in timer, which is what makes the latency test possible.

```powershell
param([switch]$IncludeSoak)
Set-StrictMode -Version 2.0; $ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')
. (Join-Path $PSScriptRoot 'lib\websocket.ps1')
. (Join-Path $PSScriptRoot 'lib\mediamtx.ps1')

$ws  = New-TestWorkspace -Name 't2'
Copy-Fixture -Relative 'rigs\inspection' -Workspace $ws
$mtx = Start-MediaMTX -RtspPort 8554

# A 25 fps test pattern with a burned-in wall-clock timer.
$src = Start-Process -PassThru -WindowStyle Hidden ffmpeg -ArgumentList @(
    '-re','-f','lavfi','-i','testsrc=size=1280x720:rate=25',
    '-vf', "drawtext=text='%{localtime\:%H\\\:%M\\\:%S}':fontsize=48:fontcolor=white:x=20:y=20",
    '-c:v','libx264','-preset','ultrafast','-tune','zerolatency',
    '-f','rtsp','rtsp://127.0.0.1:8554/test')

$proc = Start-App -Workspace $ws
$ws4  = Connect-ObsWebSocket -Port 4455
try {
    # --- 1. RTSP connects quickly -----------------------------------------
    $t0 = Get-Date
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'AddElement' -Data @{
        canvas = 'Camera 1'; type = 'rtsp'
        settings = @{ url = 'rtsp://127.0.0.1:8554/test'; preset = 'lowest' }
    } | Out-Null

    $connected = $false
    while (((Get-Date) - $t0).TotalSeconds -lt 10) {
        $h = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'GetElementHealth' `
                              -Data @{ canvas = 'Camera 1'; element = 'RTSP Camera' }
        if ($h.state -eq 'connected') { $connected = $true; break }
        Start-Sleep -Milliseconds 250
    }
    Assert-True $connected ("RTSP connected in {0:N1}s" -f ((Get-Date) - $t0).TotalSeconds)
    Assert-True (((Get-Date) - $t0).TotalSeconds -lt 3) 'RTSP connected within 3 s'

    Start-Sleep -Seconds 3
    $h = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'GetElementHealth' `
                          -Data @{ canvas = 'Camera 1'; element = 'RTSP Camera' }
    Assert-Near ([double]$h.fps) 25 1 'Received frame rate'

    # --- 2. Latency --------------------------------------------------------
    # The pattern burns in the sender's wall clock; compare against ours.
    $shot = Join-Path $ws 'latency.png'
    $capturedAt = Get-Date
    Get-SourceScreenshot -Socket $ws4 -SourceName 'RTSP Camera' -Path $shot
    $shown = Read-BurnedClock -Path $shot -Crop @(20, 20, 400, 60)
    $latency = ($capturedAt - $shown).TotalMilliseconds
    Assert-True ($latency -lt 500) ("RTSP latency {0:N0} ms (target < 500)" -f $latency)

    # --- 3. Reconnect ------------------------------------------------------
    Stop-Process -Id $src.Id -Force
    Start-Sleep -Seconds 4
    $h = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'GetElementHealth' `
                          -Data @{ canvas = 'Camera 1'; element = 'RTSP Camera' }
    Assert-True ($h.state -in @('reconnecting','no_signal')) "Signal loss detected (state=$($h.state))"
    Assert-True ($h.banner_visible -eq $true) 'Loss banner shown in the preview'

    $src = Start-Process -PassThru -WindowStyle Hidden ffmpeg -ArgumentList @(
        '-re','-f','lavfi','-i','testsrc=size=1280x720:rate=25',
        '-c:v','libx264','-preset','ultrafast','-tune','zerolatency',
        '-f','rtsp','rtsp://127.0.0.1:8554/test')
    Start-Sleep -Seconds 7
    $h = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'GetElementHealth' `
                          -Data @{ canvas = 'Camera 1'; element = 'RTSP Camera' }
    Assert-True ($h.state -eq 'connected') 'Recovered without operator action'

    # --- 4. Dropout during a recording must not break the file ------------
    Invoke-ObsRequest -Socket $ws4 -Type 'StartRecord' | Out-Null
    Start-Sleep -Seconds 20
    Stop-Process -Id $src.Id -Force
    Start-Sleep -Seconds 15
    $src = Start-Process -PassThru -WindowStyle Hidden ffmpeg -ArgumentList @(
        '-re','-f','lavfi','-i','testsrc=size=1280x720:rate=25',
        '-c:v','libx264','-preset','ultrafast','-tune','zerolatency',
        '-f','rtsp','rtsp://127.0.0.1:8554/test')
    Start-Sleep -Seconds 25
    $rec = (Invoke-ObsRequest -Socket $ws4 -Type 'StopRecord').outputPath
    Start-Sleep -Seconds 2

    $info   = Get-MediaInfo -Path $rec
    $frames = Get-VideoFrameCount -Path $rec
    Assert-Near ([double]$info.format.duration) 60 1.0 'Recording spans the dropout'
    Assert-Near $frames ([double]$info.format.duration * 30) 15 'Continuous timeline across the dropout'

    # --- 5. Credentials never reach the log --------------------------------
    $secret = 'Hunter2SecretPw'
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'AddElement' -Data @{
        canvas = 'Camera 2'; type = 'rtsp'
        settings = @{ url = 'rtsp://127.0.0.1:8554/test'; username = 'admin'; password = $secret }
    } | Out-Null
    Start-Sleep -Seconds 3
}
finally {
    Disconnect-ObsWebSocket -Socket $ws4
    Stop-App -Process $proc | Out-Null
    if (-not $src.HasExited) { Stop-Process -Id $src.Id -Force }
    Stop-MediaMTX -Handle $mtx
}

$logs = Get-ChildItem (Join-Path $ws 'config') -Recurse -Filter '*.txt' -ErrorAction SilentlyContinue
$leaked = $logs | Where-Object { (Get-Content $_.FullName -Raw) -match 'Hunter2SecretPw' }
Assert-True ($leaked.Count -eq 0) 'RTSP password never appears in any log file'

# --- 6. Add Element picker offers exactly three types ---------------------
$manifest = Join-Path $ws 'elements.json'
$p = Start-App -Workspace $ws -AppArgs @('--dump-ui-manifest', $manifest); $p.WaitForExit(30000) | Out-Null
$types = (Get-Content $manifest -Raw | ConvertFrom-Json).elementTypes
Assert-True ($types.Count -eq 3) "Add Element offers 3 types (got $($types.Count): $($types -join ', '))"

exit (Write-TestSummary -Suite 'T2 Video elements')
```

**Capture-device tests (6–7 in the phase doc) need real hardware.** Guard them behind a
`-WithHardware` switch and emit a visible SKIP in CI rather than silently passing — a suite that
quietly stops testing DeckLink is worse than one that admits it.

---

## T3-unit — Parser and registry

`test/cmocka/test_mc_parser.c` — register in `test/cmocka/CMakeLists.txt` following the existing
pattern (see `test_darray` there).

```c
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
#include <math.h>

#include "mc-data/mc-parser.h"
#include "mc-data/mc-channel.h"

/* ---- delimited parser: the headline case ------------------------------- */

struct delim_case {
	const char *line;
	const char *separator;
	size_t      expect_fields;
	double      expect[8];      /* NAN marks a field we don't check */
};

static const struct delim_case delim_cases[] = {
	{"1.031,5.132,6.122",        ",",  3, {1.031, 5.132, 6.122}},
	{"1.031;5.132;6.122",        ";",  3, {1.031, 5.132, 6.122}},
	{"1.031\t5.132\t6.122",      "\t", 3, {1.031, 5.132, 6.122}},
	{"1.031|5.132|6.122",        "|",  3, {1.031, 5.132, 6.122}},
	{"1.031 , 5.132 , 6.122",    ",",  3, {1.031, 5.132, 6.122}},  /* whitespace trimmed */
	{"-12.5,0,+3.25e2",          ",",  3, {-12.5, 0.0, 325.0}},
	{"1.031,,6.122",             ",",  3, {1.031, NAN, 6.122}},    /* empty field */
	{"1.031",                    ",",  1, {1.031}},                /* single field, no sep */
	{"",                         ",",  0, {0}},                    /* empty line */
	{"1.031,5.132,6.122,",       ",",  4, {1.031, 5.132, 6.122, NAN}}, /* trailing sep */
	{"::a::b",                   "::", 3, {NAN, NAN, NAN}},        /* multi-char separator */
};

static void test_delimited(void **state)
{
	(void)state;
	for (size_t i = 0; i < sizeof(delim_cases) / sizeof(delim_cases[0]); i++) {
		const struct delim_case *c = &delim_cases[i];
		mc_parser_t *p = mc_parser_create_delimited(c->separator, true);
		assert_non_null(p);

		mc_field_t fields[16];
		size_t n = mc_parser_parse(p, c->line, strlen(c->line), fields, 16);
		assert_int_equal(n, c->expect_fields);

		for (size_t f = 0; f < n && f < 8; f++) {
			if (isnan(c->expect[f]))
				continue;
			assert_true(fabs(fields[f].numeric - c->expect[f]) < 1e-9);
		}
		mc_parser_destroy(p);
	}
}

/* ---- malformed input must not lose good fields ------------------------- */

static void test_malformed_preserves_good_fields(void **state)
{
	(void)state;
	mc_parser_t *p = mc_parser_create_delimited(",", true);
	mc_field_t fields[16];

	/* Field 1 is garbage; fields 0 and 2 must still parse. */
	size_t n = mc_parser_parse(p, "1.031,NOTANUMBER,6.122", 22, fields, 16);
	assert_int_equal(n, 3);
	assert_true(fabs(fields[0].numeric - 1.031) < 1e-9);
	assert_int_equal(fields[1].quality, MC_QUALITY_BAD);
	assert_true(fabs(fields[2].numeric - 6.122) < 1e-9);
	assert_int_equal(fields[2].quality, MC_QUALITY_GOOD);

	mc_parser_destroy(p);
}

/* ---- key/value --------------------------------------------------------- */

static void test_keyvalue(void **state)
{
	(void)state;
	mc_parser_t *p = mc_parser_create_keyvalue(";", "=");
	mc_field_t fields[16];
	size_t n = mc_parser_parse(p, "CP=1.031;DEP=5.132;HDG=6.122", 28, fields, 16);
	assert_int_equal(n, 3);
	assert_string_equal(fields[0].key, "CP");
	assert_true(fabs(fields[1].numeric - 5.132) < 1e-9);
	mc_parser_destroy(p);
}

/* ---- NMEA: checksum validation is the part people get wrong ------------ */

static void test_nmea_checksum(void **state)
{
	(void)state;
	mc_parser_t *p = mc_parser_create_nmea("DBT");
	mc_field_t fields[16];

	assert_true(mc_parser_parse(p, "$SDDBT,12.3,f,3.7,M,2.0,F*2A", 28, fields, 16) > 0);
	/* A corrupted checksum must be rejected, not silently accepted. */
	assert_int_equal(mc_parser_parse(p, "$SDDBT,12.3,f,3.7,M,2.0,F*FF", 28, fields, 16), 0);
	/* A different sentence type must be ignored by this parser. */
	assert_int_equal(mc_parser_parse(p, "$GPGGA,123519,4807.038,N*47", 27, fields, 16), 0);

	mc_parser_destroy(p);
}

/* ---- transforms -------------------------------------------------------- */

static void test_transforms(void **state)
{
	(void)state;
	mc_channel_def_t def = {.name = "DEPTH", .index = 0, .scale = 0.3048, .offset = 0.0,
	                        .min = 0.0, .max = 4000.0, .has_limits = true};
	mc_value_t v;

	mc_transform_apply(&def, 100.0, &v);              /* feet -> metres */
	assert_true(fabs(v.numeric - 30.48) < 1e-9);
	assert_int_equal(v.quality, MC_QUALITY_GOOD);

	mc_transform_apply(&def, 99999.0, &v);            /* out of range */
	assert_int_equal(v.quality, MC_QUALITY_BAD);
}

/* ---- separator auto-detection (feeds the Phase 5 wizard) --------------- */

static void test_separator_detect(void **state)
{
	(void)state;
	const char *sample_csv[]  = {"1,2,3", "4,5,6", "7,8,9"};
	const char *sample_semi[] = {"1;2;3", "4;5;6", "7;8;9"};
	/* Values contain '.' and ':' as decoys; must still pick ','. */
	const char *sample_tricky[] = {"1.0,12:30,3", "4.5,12:31,6"};

	assert_string_equal(mc_detect_separator(sample_csv, 3), ",");
	assert_string_equal(mc_detect_separator(sample_semi, 3), ";");
	assert_string_equal(mc_detect_separator(sample_tricky, 2), ",");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_delimited),
		cmocka_unit_test(test_malformed_preserves_good_fields),
		cmocka_unit_test(test_keyvalue),
		cmocka_unit_test(test_nmea_checksum),
		cmocka_unit_test(test_transforms),
		cmocka_unit_test(test_separator_detect),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
```

`test/cmocka/test_mc_frame.c` follows the same shape, and its most important case is
**chunk-invariance**: for a fixed byte stream, feed it in chunks of every size from 1 to the full
length and assert the resulting frame sequence is identical every time. That single loop finds
most framing bugs.

---

## T3 — Data core

`tools/subsea-tests/t3-data-core.ps1`

```powershell
param([switch]$IncludeSoak)
Set-StrictMode -Version 2.0; $ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')
. (Join-Path $PSScriptRoot 'lib\websocket.ps1')

$ws = New-TestWorkspace -Name 't2'
Copy-Fixture -Relative 'rigs\sim-basic' -Workspace $ws   # sim device @10Hz, 3 channels
$proc = Start-App -Workspace $ws
$ws4  = Connect-ObsWebSocket -Port 4455

try {
    Start-Sleep -Seconds 3

    # --- 1. Values match the fixture -------------------------------------
    $ch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetChannels'
    Assert-True ($ch.channels.Count -eq 3) "Three channels declared"
    Assert-Near ([double]($ch.channels | Where-Object name -eq 'CP').value)    1.031 0.0005 'CP value'
    Assert-Near ([double]($ch.channels | Where-Object name -eq 'DEPTH').value) 5.132 0.0005 'DEPTH value'
    foreach ($c in $ch.channels) { Assert-True ($c.quality -eq 'GOOD') "$($c.name) quality GOOD" }

    # --- 2. Staleness ------------------------------------------------------
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'SetSimulatorPaused' -Data @{ paused = $true } | Out-Null
    Start-Sleep -Seconds 5      # fixture stale_ms = 3000
    $ch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetChannels'
    foreach ($c in $ch.channels) { Assert-True ($c.quality -eq 'STALE') "$($c.name) went STALE" }

    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'SetSimulatorPaused' -Data @{ paused = $false } | Out-Null
    Start-Sleep -Seconds 2
    $ch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetChannels'
    foreach ($c in $ch.channels) { Assert-True ($c.quality -eq 'GOOD') "$($c.name) recovered to GOOD" }

    # --- 3. Malformed input resilience -------------------------------------
    $before = (Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetDevices').devices[0].parse_errors
    for ($i = 0; $i -lt 1000; $i++) {
        Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'SetSimulatorData' `
                         -Data @{ line = 'GARBAGE,,,,NOT_A_NUMBER' } | Out-Null
    }
    Start-Sleep -Seconds 2
    $after = (Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetDevices').devices[0].parse_errors
    Assert-True (-not $proc.HasExited)          'App survived 1000 malformed lines'
    Assert-Near ($after - $before) 1000 0       'Every malformed line was counted'

    # --- 4. Load / leak (soak only) ---------------------------------------
    if ($IncludeSoak) {
        Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'SetSimulatorRate' -Data @{ hz = 50; channels = 50 } | Out-Null
        $proc.Refresh(); $mem0 = $proc.WorkingSet64
        Start-Sleep -Seconds 300
        $proc.Refresh(); $mem1 = $proc.WorkingSet64
        $growthMB = ($mem1 - $mem0) / 1MB
        Assert-True ($growthMB -lt 25) ("Memory growth over 5 min at 50ch/50Hz: {0:N1} MB" -f $growthMB)

        $seq = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetChannels'
        foreach ($c in $seq.channels) { Assert-True ($c.dropped -eq 0) "$($c.name) dropped no updates" }
    }
}
finally {
    Disconnect-ObsWebSocket -Socket $ws4
    Stop-App -Process $proc | Out-Null
}
Assert-NoLogErrors -Workspace $ws -Allow @('parse error')
exit (Write-TestSummary -Suite 'T3 Data core')
```

---

## T4-unit — Format engine

`test/cmocka/test_mc_format.c`

```c
static void test_format_tokens(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_create_for_test();
	mc_test_publish(reg, "CP",    1.0312, MC_QUALITY_GOOD);
	mc_test_publish(reg, "DEPTH", 5.1,    MC_QUALITY_GOOD);
	mc_test_publish(reg, "OLD",   9.9,    MC_QUALITY_STALE);

	char out[256];

	/* plain token */
	mc_format(reg, "CP {CP}", out, sizeof out);
	assert_string_equal(out, "CP 1.0312");

	/* precision spec */
	mc_format(reg, "{CP:0.00} V", out, sizeof out);
	assert_string_equal(out, "1.03 V");

	/* zero-padded width */
	mc_format(reg, "{DEPTH:000.0}", out, sizeof out);
	assert_string_equal(out, "005.1");

	/* multiple tokens and literal text */
	mc_format(reg, "D {DEPTH:0.0}m  CP {CP:0.000}V", out, sizeof out);
	assert_string_equal(out, "D 5.1m  CP 1.031V");

	/* escaped braces */
	mc_format(reg, "{{literal}} {CP:0.0}", out, sizeof out);
	assert_string_equal(out, "{literal} 1.0");

	/* unknown channel is visibly marked, not silently blank */
	mc_format(reg, "{NOPE}", out, sizeof out);
	assert_string_equal(out, "{?NOPE}");

	/* stale channel uses the placeholder */
	mc_format_opts_t opts = {.stale_placeholder = "---"};
	mc_format_ex(reg, "{OLD:0.0}", &opts, out, sizeof out);
	assert_string_equal(out, "---");

	/* malformed format strings must not crash or overrun */
	mc_format(reg, "{unterminated", out, sizeof out);
	mc_format(reg, "{CP:}", out, sizeof out);
	mc_format(reg, "{:0.0}", out, sizeof out);

	/* truncation is safe */
	char tiny[8];
	mc_format(reg, "{CP:0.0000000000}", tiny, sizeof tiny);
	assert_true(strlen(tiny) < sizeof tiny);

	mc_registry_destroy(reg);
}
```

---

## T4 — Overlay editor

`tools/subsea-tests/t4-overlay.ps1` — the interesting assertions; boilerplate matches T2.

```powershell
# --- 1. Golden render ------------------------------------------------------
# Fixture: template 'TestBanner' with DEPTH/CP fields, assigned to 8 scenes.
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'SetSimulatorData' `
                 -Data @{ line = '1.031,5.132,6.122' } | Out-Null
Start-Sleep -Milliseconds 500      # > one 4 Hz update period

$shot = Join-Path $ws 'scene1.png'
Get-SourceScreenshot -Socket $ws4 -SourceName 'Camera 1' -Path $shot
Assert-ImageMatch -Actual $shot `
                  -Golden (Join-Path $PSScriptRoot 'fixtures\golden\banner-1.031.png') `
                  -Crop @(0, 980, 1920, 100) -Tolerance 0.02 -What 'Overlay banner render'

# --- 2. Values propagate ---------------------------------------------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'SetSimulatorData' `
                 -Data @{ line = '2.500,99.900,180.000' } | Out-Null
Start-Sleep -Milliseconds 500
$shot2 = Join-Path $ws 'scene1-b.png'
Get-SourceScreenshot -Socket $ws4 -SourceName 'Camera 1' -Path $shot2
Assert-True (-not (Test-ImageIdentical $shot $shot2 -Crop @(0,980,1920,100))) 'Overlay updated with new data'

# --- 3. Same template across 8 scenes, differing only in {@scene} ----------
$crops = @()
foreach ($n in 1..8) {
    $f = Join-Path $ws "cam$n.png"
    Get-SourceScreenshot -Socket $ws4 -SourceName "Camera $n" -Path $f
    # Crop the data region only, excluding the scene-name field at x<400.
    $crops += (Get-ImageHash -Path $f -Crop @(400, 980, 1520, 100))
}
Assert-True (($crops | Select-Object -Unique).Count -eq 1) 'Data region identical across all 8 assigned scenes'

# --- 4. Editing during a recording drops no frames -------------------------
# The most important test in this suite.
Invoke-ObsRequest -Socket $ws4 -Type 'StartRecord' | Out-Null
Start-Sleep -Seconds 5
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-overlay' -Request 'EnterOverlayMode' -Data @{ template = 'TestBanner' } | Out-Null
Start-Sleep -Seconds 2
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-overlay' -Request 'MoveItem' -Data @{ item = 'DepthField'; x = 100; y = 900 } | Out-Null
Start-Sleep -Seconds 2
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-overlay' -Request 'ExitOverlayMode' | Out-Null
Start-Sleep -Seconds 5
$rec = (Invoke-ObsRequest -Socket $ws4 -Type 'StopRecord').outputPath
Start-Sleep -Seconds 2

$info   = Get-MediaInfo -Path $rec
$frames = Get-VideoFrameCount -Path $rec
$fps    = [double]($info.streams | Where-Object codec_type -eq 'video').r_frame_rate.Split('/')[0] /
          [double]($info.streams | Where-Object codec_type -eq 'video').r_frame_rate.Split('/')[1]
$expected = [double]$info.format.duration * $fps
Assert-Near $frames $expected ($fps * 0.5) 'No frames dropped while entering/leaving overlay mode'

$stats = Invoke-ObsRequest -Socket $ws4 -Type 'GetStats'
Assert-True ($stats.outputSkippedFrames -eq 0) 'Encoder skipped zero frames'
```

---

## T5 — Transports

`tools/subsea-tests/t5-transports.ps1` — the serial and reconnect cases.

```powershell
# Requires a com0com virtual pair: COM20 <-> COM21. App reads COM21.
$writer = New-Object System.IO.Ports.SerialPort 'COM20', 115200, 'None', 8, 'One'
$writer.NewLine = "`r`n"
$writer.Open()
try {
    # --- 1. Serial loopback ------------------------------------------------
    1..20 | ForEach-Object { $writer.WriteLine('1.031,5.132,6.122'); Start-Sleep -Milliseconds 50 }
    Start-Sleep -Seconds 1
    $ch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetChannels'
    Assert-Near ([double]($ch.channels | Where-Object name -eq 'CP').value) 1.031 0.0005 'Serial CP value'

    # --- 2. Reconnect after adapter drop -----------------------------------
    $writer.Close(); Start-Sleep -Seconds 3
    $st = (Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetDevices').devices[0]
    Assert-True ($st.state -in @('reconnecting','error')) "Disconnect detected (state=$($st.state))"

    $writer.Open()
    1..20 | ForEach-Object { $writer.WriteLine('2.000,6.000,7.000'); Start-Sleep -Milliseconds 50 }
    Start-Sleep -Seconds 6      # allow backoff
    $ch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetChannels'
    Assert-Near ([double]($ch.channels | Where-Object name -eq 'CP').value) 2.000 0.0005 'Recovered after reconnect'

    # --- 3. Handle-leak check over 20 disconnect cycles --------------------
    $proc.Refresh(); $h0 = $proc.HandleCount
    for ($i = 0; $i -lt 20; $i++) {
        $writer.Close(); Start-Sleep -Milliseconds 400
        $writer.Open();  Start-Sleep -Milliseconds 400
    }
    Start-Sleep -Seconds 5
    $proc.Refresh(); $h1 = $proc.HandleCount
    Assert-True (($h1 - $h0) -lt 50) ("Handle growth over 20 reconnects: {0}" -f ($h1 - $h0))
}
finally { if ($writer.IsOpen) { $writer.Close() }; $writer.Dispose() }

# --- 4. UDP unicast --------------------------------------------------------
$udp = New-Object System.Net.Sockets.UdpClient
$bytes = [Text.Encoding]::ASCII.GetBytes("3.140,1.590,2.650`r`n")
1..20 | ForEach-Object { $udp.Send($bytes, $bytes.Length, '127.0.0.1', 9000) | Out-Null; Start-Sleep -Milliseconds 50 }
$udp.Close()
Start-Sleep -Seconds 1
$ch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'GetChannels' -Data @{ device = 'UDP Test' }
Assert-Near ([double]($ch.channels | Where-Object name -eq 'CP').value) 3.140 0.0005 'UDP unicast value'
```

---

## T6 — Multi-Canvas recording

`tools/subsea-tests/t6-multirecord.ps1` — the most important integration script in the project.

**Fixture design:** `fixtures/jobs/multirec.json` defines three Canvases, each holding a solid
`color_source` Element of a distinct colour plus an overlay Element:

| Canvas | Colour | RGB |
|---|---|---|
| Camera 1 | red | 200, 30, 30 |
| Camera 2 | green | 30, 200, 30 |
| Camera 3 | blue | 30, 30, 200 |

Distinct flat colours make content routing verifiable with one pixel.

```powershell
param([switch]$IncludeSoak)
Set-StrictMode -Version 2.0; $ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')
. (Join-Path $PSScriptRoot 'lib\websocket.ps1')

$expected = @{
    'Camera 1' = @(200, 30, 30)
    'Camera 2' = @(30, 200, 30)
    'Camera 3' = @(30, 30, 200)
}

$ws = New-TestWorkspace -Name 't5'
Copy-Fixture -Relative 'jobs\multirec' -Workspace $ws
Copy-Fixture -Relative 'rigs\sim-basic'   -Workspace $ws
$recDir = Join-Path $ws 'rec'

$proc = Start-App -Workspace $ws -AppArgs @('--collection','multirec','--profile','sim-basic')
$ws4  = Connect-ObsWebSocket -Port 4455
try {
    Start-Sleep -Seconds 3

    # === 1. Three scenes, 30 seconds =====================================
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'ArmScenes' `
                     -Data @{ scenes = @('Camera 1','Camera 2','Camera 3') } | Out-Null
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
    Start-Sleep -Seconds 30
    $result = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StopAll'
    Start-Sleep -Seconds 3

    Assert-True ($result.files.Count -eq 3) "Three files produced (got $($result.files.Count))"

    foreach ($f in $result.files) {
        $name = Split-Path -Leaf $f.path
        $info = Get-MediaInfo -Path $f.path
        $v    = $info.streams | Where-Object codec_type -eq 'video'

        Assert-Near ([double]$info.format.duration) 30 0.5 "$name duration"
        Assert-True ($v.width -eq 1920 -and $v.height -eq 1080) "$name resolution 1920x1080"

        $parts = $v.r_frame_rate.Split('/')
        $fps   = [double]$parts[0] / [double]$parts[1]
        $frames = Get-VideoFrameCount -Path $f.path
        Assert-Near $frames ([double]$info.format.duration * $fps) ($fps * 0.5) "$name frame count"

        # === 2. Content routing — the bug this suite exists to catch =====
        # Sample below the overlay band so the banner does not skew the colour.
        $c = Get-FrameDominantColor -Path $f.path -AtSeconds 15 -Crop @(0, 0, 1920, 900)
        Assert-ColorNear -Actual $c -Expected $expected[$f.scene] -Tolerance 30 `
                         -What "$name contains '$($f.scene)' content"

        # === 3. Overlay is burned in =====================================
        $band = Get-FrameDominantColor -Path $f.path -AtSeconds 15 -Crop @(0, 980, 1920, 100)
        Assert-True (-not (Test-ColorNear $band $expected[$f.scene] 30)) `
                    "$name overlay band differs from flat scene colour (overlay burned in)"
    }

    # === 4. Independent control ==========================================
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
    Start-Sleep -Seconds 10
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'Stop' -Data @{ scene = 'Camera 2' } | Out-Null
    Start-Sleep -Seconds 20
    $r2 = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StopAll'
    Start-Sleep -Seconds 3

    foreach ($f in $r2.files) {
        $expDur = 30; if ($f.scene -eq 'Camera 2') { $expDur = 10 }
        $info = Get-MediaInfo -Path $f.path
        Assert-Near ([double]$info.format.duration) $expDur 0.5 "$($f.scene) independent stop duration"
    }
    $stats = Invoke-ObsRequest -Socket $ws4 -Type 'GetStats'
    Assert-True ($stats.outputSkippedFrames -eq 0) 'Stopping one recorder skipped no frames in the others'

    # === 5. Atomic rollback ==============================================
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'SetScenePath' `
                     -Data @{ scene = 'Camera 2'; path = 'Z:\does\not\exist' } | Out-Null
    $before = (Get-ChildItem $recDir -File).Count
    $fail = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll'
    Start-Sleep -Seconds 2
    $after = (Get-ChildItem $recDir -File).Count
    Assert-True ($fail.success -eq $false)        'Group start reported failure'
    Assert-True ($fail.error -match 'Camera 2')   'Error names the failing recorder'
    Assert-True ($after -eq $before)              'Rollback left no partial files'
    Assert-True ((Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'GetStatus').anyActive -eq $false) `
                'No recorder left running after rollback'
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'SetScenePath' `
                     -Data @{ scene = 'Camera 2'; path = $recDir } | Out-Null

    # === 6. Auto-split alignment =========================================
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'SetSplit' -Data @{ minutes = 1 } | Out-Null
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
    Start-Sleep -Seconds 210    # 3.5 minutes
    $r3 = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StopAll'
    Start-Sleep -Seconds 3

    $partSets = @{}
    foreach ($f in $r3.files) {
        Assert-True ($f.parts.Count -eq 4) "$($f.scene) produced 4 parts (got $($f.parts.Count))"
        $durs = @()
        foreach ($p in $f.parts) { $durs += [double](Get-MediaInfo -Path $p).format.duration }
        $partSets[$f.scene] = $durs
        Assert-Near ($durs | Measure-Object -Sum).Sum 210 1.0 "$($f.scene) parts sum to full duration"
    }
    # Boundaries must line up across cameras, within one frame.
    foreach ($i in 0..2) {
        $vals = $partSets.Values | ForEach-Object { $_[$i] }
        $spread = ($vals | Measure-Object -Max).Maximum - ($vals | Measure-Object -Min).Minimum
        Assert-True ($spread -lt 0.05) ("Part {0} boundary aligned across cameras (spread {1:N3}s)" -f ($i+1), $spread)
    }

    # === 7. Encoder-limit guard ==========================================
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'SetEncoderLimit' -Data @{ limit = 2 } | Out-Null
    $guard = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll'
    Assert-True ($guard.success -eq $false)          'Guard refused to exceed the encoder limit'
    Assert-True ($guard.error -match '(?i)limit|encoder') 'Guard error explains why'
    Assert-True ((Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'GetStatus').anyActive -eq $false) `
                'Nothing started when the guard refused'
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'SetEncoderLimit' -Data @{ limit = 8 } | Out-Null

    # === 8. Kill test ====================================================
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
    Start-Sleep -Seconds 20
    Stop-App -Process $proc -Force | Out-Null
    Start-Sleep -Seconds 3
    $orphans = Get-ChildItem $recDir -File -Filter '*.mp4' |
               Where-Object { $_.LastWriteTime -gt (Get-Date).AddSeconds(-40) }
    Assert-True ($orphans.Count -eq 3) "Three files survived the kill (got $($orphans.Count))"
    foreach ($o in $orphans) {
        $ok = $true
        try { $i = Get-MediaInfo -Path $o.FullName } catch { $ok = $false }
        Assert-True $ok "$($o.Name) is playable after a hard kill"
        if ($ok) { Assert-Near ([double]$i.format.duration) 20 5 "$($o.Name) duration after kill" }
    }
}
finally {
    try { Disconnect-ObsWebSocket -Socket $ws4 } catch { }
    if (-not $proc.HasExited) { Stop-App -Process $proc -Force | Out-Null }
}

# === 9. Soak =============================================================
if ($IncludeSoak) {
    $ws2 = New-TestWorkspace -Name 't5-soak'
    Copy-Fixture -Relative 'jobs\multirec' -Workspace $ws2
    $p = Start-App -Workspace $ws2 -AppArgs @('--collection','multirec')
    $s = Connect-ObsWebSocket -Port 4455
    Invoke-ObsVendor -Socket $s -Vendor 'mc-record' -Request 'ArmScenes' `
                     -Data @{ scenes = @('Camera 1','Camera 2','Camera 3') } | Out-Null
    Invoke-ObsVendor -Socket $s -Vendor 'mc-record' -Request 'StartAll' | Out-Null

    $p.Refresh(); $mem0 = $p.WorkingSet64; $h0 = $p.HandleCount
    $samples = @()
    for ($m = 0; $m -lt 240; $m++) {
        Start-Sleep -Seconds 60
        $p.Refresh()
        $samples += [pscustomobject]@{ Minute = $m; MemMB = [int]($p.WorkingSet64 / 1MB); Handles = $p.HandleCount }
        if ($p.HasExited) { Add-Failure "App exited during soak at minute $m"; break }
    }
    $samples | Export-Csv -NoTypeInformation -Path (Join-Path $ws2 'soak.csv')
    $p.Refresh()
    Assert-True ((($p.WorkingSet64 - $mem0) / 1MB) -lt 200) 'Memory flat over 4-hour soak'
    Assert-True (($p.HandleCount - $h0) -lt 200)            'Handle count flat over 4-hour soak'

    $r = Invoke-ObsVendor -Socket $s -Vendor 'mc-record' -Request 'StopAll'
    Disconnect-ObsWebSocket -Socket $s; Stop-App -Process $p | Out-Null
    foreach ($f in $r.files) { foreach ($part in $f.parts) {
        $ok = $true; try { Get-MediaInfo -Path $part | Out-Null } catch { $ok = $false }
        Assert-True $ok "Soak part $(Split-Path -Leaf $part) is valid"
    } }
}

exit (Write-TestSummary -Suite 'T6 Multi-Canvas recording')
```

---

## T7 — Secondary capture

Two scripts, `t7-clips.ps1` and `t7-snapshots.ps1`. Both reuse the distinct-colour Canvas fixture
from [T6](#t6--multi-canvas-recording). (The `$map` entry for suite 7 is a list; the orchestrator
loops it.)

### Clips — `t7-clips.ps1`

```powershell
# --- 4. Independence — write this one FIRST -------------------------------
# The primary recording is the deliverable; a clip is a convenience. Prove the
# asymmetry is structural before trusting the rest of the feature.
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'ArmCanvases' `
                 -Data @{ canvases = @('Camera 1') } | Out-Null
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
Start-Sleep -Seconds 10

Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'SetClipPath' `
                 -Data @{ canvas = 'Camera 1'; path = 'Z:\does\not\exist' } | Out-Null
$clip = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'Start' -Data @{ canvas = 'Camera 1' }
Assert-True ($clip.success -eq $false)      'Clip start failed as forced'
Assert-True ($clip.error -match '(?i)clip') 'Error identifies the clip, not the recording'

Start-Sleep -Seconds 20
$r = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StopAll'
Start-Sleep -Seconds 2
$info = Get-MediaInfo -Path $r.files[0].path
Assert-Near ([double]$info.format.duration) 30 0.5 'Primary recording unaffected by clip failure'
$stats = Invoke-ObsRequest -Socket $ws4 -Type 'GetStats'
Assert-True ($stats.outputSkippedFrames -eq 0) 'Clip failure skipped no primary frames'

# --- 2. Encoder session count — the feature's economic case ---------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'SetClipPath' `
                 -Data @{ canvas = 'Camera 1'; path = $recDir } | Out-Null
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
Start-Sleep -Seconds 10
$before = (Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'GetStatus').activeVideoEncoders
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'Start' -Data @{ canvas = 'Camera 1' } | Out-Null
Start-Sleep -Seconds 3
$after = (Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'GetStatus').activeVideoEncoders
Assert-True ($after -eq $before) "Clip added no encoder session ($before -> $after)"

# --- 1/3. Clip duration and preroll correctness ---------------------------
# The fixture Canvas carries an overlay field bound to {@utc}, so every frame
# is self-timestamping.
$pressedAt = Get-Date
Start-Sleep -Seconds 10
$stopped = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'Stop' -Data @{ canvas = 'Camera 1' }
Start-Sleep -Seconds 2

$preroll = [double](Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'GetConfig').preroll_seconds
$ci = Get-MediaInfo -Path $stopped.path
Assert-Near ([double]$ci.format.duration) (10 + $preroll) 2.0 'Clip duration includes preroll'

$firstFrame = Join-Path $ws 'clip-first.png'
& ffmpeg -v error -i "$($stopped.path)" -frames:v 1 -vf 'crop=400:60:20:980' -y "$firstFrame" | Out-Null
$shown = Read-BurnedClock -Path $firstFrame
$lead = ($pressedAt - $shown).TotalSeconds
Assert-True ($lead -gt 0) 'Clip starts BEFORE the button press, never after'
Assert-Near $lead $preroll 2.0 'Clip lead-in matches the configured preroll'

# --- 7. Timestamps normalised to zero -------------------------------------
$startPts = & ffprobe -v error -select_streams v:0 -show_entries packet=pts_time `
                      -of csv=p=0 -read_intervals '%+#1' -- "$($stopped.path)"
Assert-True ([double]$startPts -lt 0.5) "Clip first PTS is ~0 (got $startPts), not the parent's elapsed time"

# --- 5. Concurrent clips ---------------------------------------------------
foreach ($n in 1..3) {
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'Start' -Data @{ canvas = 'Camera 1' } | Out-Null
    Start-Sleep -Seconds 3
}
Start-Sleep -Seconds 5
$all = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'StopAll' -Data @{ canvas = 'Camera 1' }
Assert-True ($all.files.Count -eq 3) 'Three concurrent clips produced three files'
$durs = $all.files | ForEach-Object { [double](Get-MediaInfo -Path $_.path).format.duration }
Assert-True ((($durs | Select-Object -Unique).Count) -eq 3) 'The three clips have distinct windows'

# --- 6. Clip All across Canvases ------------------------------------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'ArmCanvases' `
                 -Data @{ canvases = @('Camera 1','Camera 2','Camera 3') } | Out-Null
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
Start-Sleep -Seconds 10
$batch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'ClipAll'
Start-Sleep -Seconds 8
$batch = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'StopAllClips'
Assert-True ($batch.files.Count -eq 3) 'Clip All produced one clip per recording Canvas'
$starts = $batch.files | ForEach-Object { [datetime]$_.started_utc }
$spread = (($starts | Measure-Object -Max).Maximum - ($starts | Measure-Object -Min).Minimum).TotalMilliseconds
Assert-True ($spread -lt 40) ("Clip All starts agree within one frame (spread {0:N0} ms)" -f $spread)

# --- 8. Parent stop cascades ----------------------------------------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'Start' -Data @{ canvas = 'Camera 1' } | Out-Null
Start-Sleep -Seconds 5
$r = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StopAll'
Start-Sleep -Seconds 3
$orphanClip = $r.clips | Select-Object -First 1
$ok = $true; try { Get-MediaInfo -Path $orphanClip.path | Out-Null } catch { $ok = $false }
Assert-True $ok 'Clip finalised cleanly when its parent recording stopped'

# --- 10. Ring memory is bounded -------------------------------------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'SetPreroll' -Data @{ seconds = 30 } | Out-Null
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
Start-Sleep -Seconds 60
$rep = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-clip' -Request 'GetRingStats'
Assert-True ($rep.bytes_used -le $rep.bytes_limit) 'Packet ring stays within its configured bound'
$proc.Refresh(); $m0 = $proc.WorkingSet64
Start-Sleep -Seconds 1800
$proc.Refresh()
Assert-True ((($proc.WorkingSet64 - $m0) / 1MB) -lt 100) 'Process memory flat over 30 min with rings full'
```

Test 9 (hard-kill with primary + 2 clips running) follows the same shape as
[T6](#t6--multi-canvas-recording) test 8 and is omitted here for brevity.

### Snapshots — `t7-snapshots.ps1`

```powershell
param([switch]$IncludeSoak)
Set-StrictMode -Version 2.0; $ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')
. (Join-Path $PSScriptRoot 'lib\websocket.ps1')

$expected = @{
    'Camera 1' = @(200, 30, 30); 'Camera 2' = @(30, 200, 30); 'Camera 3' = @(30, 30, 200)
}
$ws = New-TestWorkspace -Name 't7-snap'
Copy-Fixture -Relative 'jobs\multirec'   -Workspace $ws
Copy-Fixture -Relative 'rigs\sim-basic'  -Workspace $ws
$snapDir = Join-Path $ws 'rec\snapshots'

$proc = Start-App -Workspace $ws -AppArgs @('--collection','multirec','--profile','sim-basic')
$ws4  = Connect-ObsWebSocket -Port 4455
try {
    Start-Sleep -Seconds 3
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-data' -Request 'SetSimulatorData' `
                     -Data @{ line = '1.031,5.132,6.122' } | Out-Null
    Start-Sleep -Seconds 1

    # === B1. Every Canvas gets a tile, both variants =======================
    $set = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-snapshot' -Request 'Capture'
    Assert-True ($set.tiles.Count -eq 3) "One tile per Canvas (got $($set.tiles.Count))"
    foreach ($t in $set.tiles) {
        Assert-True ($null -ne $t.overlaid_png) "$($t.canvas): overlaid variant present"
        Assert-True ($null -ne $t.clean_png)    "$($t.canvas): clean variant present"
    }

    # === B5. Simultaneity =================================================
    # Each Canvas carries an overlay field bound to {@utc}.
    $stamps = $set.tiles | ForEach-Object { [datetime]$_.captured_utc }
    $spread = (($stamps | Measure-Object -Max).Maximum -
               ($stamps | Measure-Object -Min).Minimum).TotalMilliseconds
    Assert-True ($spread -lt 40) ("All Canvases frozen within one frame (spread {0:N0} ms)" -f $spread)

    # === B6. Save both variants ===========================================
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-snapshot' -Request 'Save' `
                     -Data @{ set = $set.id; variants = 'both'; path = $snapDir } | Out-Null
    Start-Sleep -Seconds 2
    $files = Get-ChildItem $snapDir -Filter '*.png'
    Assert-True ($files.Count -eq 6) "Both variants saved for 3 Canvases (got $($files.Count))"
    Assert-True (($files | Where-Object { $_.Name -match '_CLEAN\.png$' }).Count -eq 3) `
                'Clean variants carry the _CLEAN suffix'

    # === B2/B3. Content routing, and clean really is clean ================
    # Overlay band is the bottom 100 px; sample it and the flat area above it.
    foreach ($canvas in $expected.Keys) {
        $over  = $files | Where-Object { $_.Name -match [regex]::Escape($canvas) -and $_.Name -notmatch '_CLEAN' }
        $clean = $files | Where-Object { $_.Name -match [regex]::Escape($canvas) -and $_.Name -match '_CLEAN' }
        Assert-True ($over -and $clean) "$canvas has both files"

        $body = Get-ImageRegionColor -Path $over.FullName  -Crop @(0, 0, 1920, 900)
        Assert-ColorNear -Actual $body -Expected $expected[$canvas] -Tolerance 30 `
                         -What "$canvas snapshot shows its own content"

        $bandOver  = Get-ImageRegionColor -Path $over.FullName  -Crop @(0, 980, 1920, 100)
        $bandClean = Get-ImageRegionColor -Path $clean.FullName -Crop @(0, 980, 1920, 100)
        Assert-True (-not (Test-ColorNear $bandOver $expected[$canvas] 30)) `
                    "$canvas overlaid: banner region differs from the flat Canvas colour"
        Assert-ColorNear -Actual $bandClean -Expected $expected[$canvas] -Tolerance 30 `
                         -What "$canvas clean: banner region IS the flat Canvas colour (no overlay)"
    }

    # === B7. Metadata =====================================================
    $meta = Get-PngTextChunks -Path ($files | Select-Object -First 1).FullName
    Assert-Near ([double]$meta.CP) 1.031 0.0005 'PNG metadata carries the CP channel value'
    $sidecar = Get-ChildItem $snapDir -Filter '*.json' | Select-Object -First 1
    Assert-True ($null -ne $sidecar) 'Snapshot set wrote a sidecar JSON'
    $sj = Get-Content $sidecar.FullName -Raw | ConvertFrom-Json
    Assert-True ($sj.images.Count -eq 6) 'Sidecar lists every saved image'

    # === B4. No disturbance to a running recording — the critical one =====
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'ArmCanvases' `
                     -Data @{ canvases = @('Camera 1','Camera 2','Camera 3') } | Out-Null
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
    $snapTimes = @()
    for ($i = 0; $i -lt 12; $i++) {
        Start-Sleep -Seconds 5
        $s = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-snapshot' -Request 'Capture'
        $snapTimes += [double]$s.media_seconds
    }
    $r = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StopAll'
    Start-Sleep -Seconds 3

    foreach ($f in $r.files) {
        $info   = Get-MediaInfo -Path $f.path
        $parts  = ($info.streams | Where-Object codec_type -eq 'video').r_frame_rate.Split('/')
        $fps    = [double]$parts[0] / [double]$parts[1]
        $frames = Get-VideoFrameCount -Path $f.path
        Assert-Near $frames ([double]$info.format.duration * $fps) 1 `
                    "$($f.canvas): exact frame count across 12 snapshots"

        # The clean-variant capture must never have stripped the overlay from
        # a recorded frame. Check the band at every snapshot instant.
        foreach ($t in $snapTimes) {
            $band = Get-FrameDominantColor -Path $f.path -AtSeconds $t -Crop @(0, 980, 1920, 100)
            if (Test-ColorNear $band $expected[$f.canvas] 30) {
                Add-Failure "$($f.canvas): overlay MISSING from recorded frame at t=${t}s (snapshot leaked)"
                break
            }
        }
    }
    $stats = Invoke-ObsRequest -Socket $ws4 -Type 'GetStats'
    Assert-True ($stats.outputSkippedFrames -eq 0) 'Snapshots during recording skipped no frames'

    # === B8. Missing source is labelled, not silently black ===============
    Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-layers' -Request 'RemoveElement' `
                     -Data @{ canvas = 'Camera 3'; element = 'Colour' } | Out-Null
    $s = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-snapshot' -Request 'Capture'
    $t3 = $s.tiles | Where-Object canvas -eq 'Camera 3'
    Assert-True ($t3.state -ne 'ok') "Empty Canvas tile is flagged (state=$($t3.state))"

    # === B9. Hotkey spam ==================================================
    $proc.Refresh(); $m0 = $proc.WorkingSet64
    for ($i = 0; $i -lt 50; $i++) {
        Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-snapshot' -Request 'Capture' | Out-Null
    }
    Start-Sleep -Seconds 10
    $proc.Refresh()
    Assert-True (-not $proc.HasExited) 'App survived 50 rapid snapshot requests'
    Assert-True ((($proc.WorkingSet64 - $m0) / 1MB) -lt 300) 'Snapshot concurrency cap bounds memory'
}
finally {
    Disconnect-ObsWebSocket -Socket $ws4
    Stop-App -Process $proc | Out-Null
}
exit (Write-TestSummary -Suite 'T7 Snapshots')
```

**B4 is the test that justifies the whole design choice** in
[architecture.md §3.7](architecture.md#37-snapshots). If the clean-variant capture were implemented
by hiding the overlay on the shared scene item, this test is the only place the resulting one-frame
defect would ever show up.

Two helpers this suite needs, alongside the ones listed in `common.ps1`:
`Get-ImageRegionColor` (average colour of a crop from a PNG on disk — the still-image sibling of
`Get-FrameDominantColor`) and `Get-PngTextChunks`.

---

## T8-unit — Log writer

`test/cmocka/test_mc_log_writer.c` — the cases worth having:

| Test | Asserts |
|---|---|
| `test_csv_header` | Header is `NAME_unit`, ordered by declaration, with the `_quality` column last |
| `test_csv_escaping` | RFC 4180 quoting for values containing `,`, `"`, or a newline |
| `test_quality_string` | One character per channel, in header order, `G`/`S`/`B`/`N` |
| `test_media_seconds` | `media_seconds` = `(row_wall_ns - start_wall_ns) / 1e9`, monotonic, starts at ~0 |
| `test_precision` | Full precision written regardless of the channel's display precision |
| `test_overflow` | Queue overflow drops rows and increments the counter by exactly the drop count |
| `test_truncated_close` | Closing mid-row leaves a file whose complete lines all parse |

---

## T8 — Sidecar log

`tools/subsea-tests/t8-sidecar.ps1` — the sync test is the one that proves the feature.

```powershell
# --- 3. Sync accuracy ------------------------------------------------------
# The overlay template used here contains a field bound to {@rectime}, so each
# frame carries its own media timestamp. If the CSV agrees with the pixels, the
# sidecar log is genuinely usable for resync.
$video = $result.files[0].path
$csv   = [IO.Path]::ChangeExtension($video, '.csv')
$rows  = Import-Csv $csv

foreach ($t in @(5, 15, 25, 45)) {
    $frame = Join-Path $ws "sync_$t.png"
    & ffmpeg -v error -ss $t -i "$video" -frames:v 1 `
             -vf 'crop=300:60:20:20' -y "$frame" | Out-Null
    # Template-match the burned digits against pre-rendered glyphs; OCR is a
    # fallback but is flakier than matching a known font at a known size.
    $burned = Read-BurnedTimestamp -Path $frame
    Assert-Near $burned $t 0.5 "Burned media time at t=${t}s"

    $row = $rows | Where-Object { [Math]::Abs([double]$_.media_seconds - $t) -lt 0.6 } | Select-Object -First 1
    Assert-True ($null -ne $row) "CSV has a row near media_seconds=$t"
}

# --- 1/2. Row count and value fidelity -------------------------------------
Assert-Near $rows.Count 60 1 'One row per second over a 60 s recording'
Assert-True (($rows.media_seconds | ForEach-Object { [double]$_ }) -eq
             ($rows.media_seconds | ForEach-Object { [double]$_ } | Sort-Object)) 'media_seconds is monotonic'

# The fixture simulator emits a known ramp: CP = 1.000 + 0.001 * n
for ($i = 0; $i -lt $rows.Count; $i++) {
    $exp = 1.000 + 0.001 * $i
    if ([Math]::Abs([double]$rows[$i].CP_V - $exp) -gt 0.0015) {
        Add-Failure ("CSV row {0}: CP_V expected ~{1}, got {2}" -f $i, $exp, $rows[$i].CP_V)
        break
    }
}

# --- 4. Quality flags ------------------------------------------------------
# The fixture stalls the simulator from t=20 s to t=30 s.
$stalled = $rows | Where-Object { [double]$_.media_seconds -ge 22 -and [double]$_.media_seconds -le 29 }
Assert-True (($stalled | Where-Object { $_._quality -notmatch '^S+$' }).Count -eq 0) `
            'All rows in the stall window are flagged STALE'
$fresh = $rows | Where-Object { [double]$_.media_seconds -lt 18 }
Assert-True (($fresh | Where-Object { $_._quality -notmatch '^G+$' }).Count -eq 0) `
            'All rows outside the stall window are flagged GOOD'

# --- 7. Crash resilience ---------------------------------------------------
# (separate app run) hard-kill at 30 s, then:
$ok = $true
try { $killedRows = Import-Csv $killedCsv } catch { $ok = $false }
Assert-True $ok 'CSV parses after a hard kill'
if ($ok) { Assert-Near $killedRows.Count 30 3 'Killed CSV has ~30 rows' }
```

---

## T9 — WebRTC streaming

`tools/subsea-tests/t9-streaming.ps1`. Reuses the MediaMTX helper introduced for
[T2](#t2--video-elements) — the same binary serves the RTSP fixture and the WHIP endpoint.

```powershell
$mtx = Start-MediaMTX -RtspPort 8554 -WhipPort 8889
$whip = 'http://127.0.0.1:8889/live/whip'

# --- 1/2. Connect and confirm media is flowing ----------------------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-stream' -Request 'Configure' `
                 -Data @{ url = $whip; token = 'test-token'; canvas = 'Camera 1' } | Out-Null
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-stream' -Request 'Start' | Out-Null
Start-Sleep -Seconds 5

$pub = Get-MediaMTXPaths -Handle $mtx | Where-Object { $_.name -eq 'live' }
Assert-True ($null -ne $pub -and $pub.ready) 'MediaMTX reports an active publisher'

# Pull it back down through MediaMTX's RTSP egress and probe it.
$pull = Join-Path $ws 'pulled.mp4'
& ffmpeg -v error -rtsp_transport tcp -i 'rtsp://127.0.0.1:8554/live' -t 10 -c copy -y "$pull" | Out-Null
$info = Get-MediaInfo -Path $pull
$v = $info.streams | Where-Object codec_type -eq 'video'
Assert-True ($v.codec_name -eq 'h264') "Stream codec is H.264 (got $($v.codec_name))"
Assert-True ($v.width -gt 0)           'Stream has a valid video track'

# --- 3. Content correctness (same colour trick as T6) ---------------------
$c = Get-FrameDominantColor -Path $pull -AtSeconds 5 -Crop @(0, 0, 1920, 900)
Assert-ColorNear -Actual $c -Expected @(200, 30, 30) -Tolerance 30 -What 'Streamed Canvas content'

# --- 4. Latency ------------------------------------------------------------
# Camera 1 carries an overlay field bound to {@utc}; compare it at the receiver.
$frame = Join-Path $ws 'stream-latency.png'
$at = Get-Date
& ffmpeg -v error -rtsp_transport tcp -i 'rtsp://127.0.0.1:8554/live' -frames:v 1 -y "$frame" | Out-Null
$shown = Read-BurnedClock -Path $frame -Crop @(20, 980, 400, 60)
Assert-True ((($at - $shown).TotalMilliseconds) -lt 1000) 'Glass-to-receiver latency under 1 s'

# --- 5/6. Recording isolation — the most important test here --------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'ArmCanvases' `
                 -Data @{ canvases = @('Camera 1','Camera 2','Camera 3') } | Out-Null
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll' | Out-Null
Start-Sleep -Seconds 15
Stop-MediaMTX -Handle $mtx          # yank the stream target mid-run
Start-Sleep -Seconds 20
$r = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StopAll'
Start-Sleep -Seconds 3

Assert-True ($r.files.Count -eq 3) 'All three recordings survived the streaming failure'
foreach ($f in $r.files) {
    $i = Get-MediaInfo -Path $f.path
    Assert-Near ([double]$i.format.duration) 35 1.0 "$($f.canvas) duration unaffected by stream loss"
}
$stats = Invoke-ObsRequest -Socket $ws4 -Type 'GetStats'
Assert-True ($stats.outputSkippedFrames -eq 0) 'Streaming failure skipped no recording frames'

# --- 7. Guard integration --------------------------------------------------
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'SetEncoderLimit' -Data @{ limit = 3 } | Out-Null
Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-stream' -Request 'Start' | Out-Null   # 3 rec + 1 stream
$guard = Invoke-ObsVendor -Socket $ws4 -Vendor 'mc-record' -Request 'StartAll'
Assert-True ($guard.success -eq $false)     'Guard counts the streaming encode'
Assert-True ($guard.error -match '4.*3|stream') 'Guard message names both counts'

# --- 8. Token scrubbing ----------------------------------------------------
$logs = Get-ChildItem (Join-Path $ws 'config') -Recurse -Filter '*.txt'
$leak = $logs | Where-Object { (Get-Content $_.FullName -Raw) -match 'test-token' }
Assert-True ($leak.Count -eq 0) 'Bearer token never appears in a log file'
```

---

## Field QA checklist

Things no script can judge. Run before every release, ideally with someone who actually does the
job.

- [ ] Can a pilot who has never seen the app start recording within 60 seconds, unaided?
- [ ] Is the recording indicator readable from two metres in a bright container?
- [ ] Is it obvious at a glance which cameras are recording and which are not?
- [ ] Is it obvious when data has gone stale, without hunting for it?
- [ ] Can a wrong job number be corrected mid-dive without stopping the recording?
- [ ] Does every error message say what to *do*, not just what went wrong?
- [ ] Does the app survive the monitor being unplugged? A USB hub power-cycling? RDP connect and
      disconnect?
- [ ] After a full dive, is the deliverable set (video + CSV + manifest) complete and correctly
      named, with no manual tidying required?
