# Mission Capture test harness -- orchestrator.
#
#   .\run-tests.ps1                 # build, unit tests, quick suites
#   .\run-tests.ps1 -Suite all
#   .\run-tests.ps1 -Suite 0 -SkipBuild
#
# See docs/subsea/testing.md.

[CmdletBinding()]
param(
    [ValidateSet('0','1','2','3','4','5','6','7','8','9','all','unit','quick')]
    [string]$Suite = 'quick',
    [switch]$SkipBuild,
    [switch]$SkipUnit,
    [switch]$IncludeSoak
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$repo   = $script:RepoRoot
$build  = Join-Path $repo 'build_x64'
$total  = 0
$runLog = New-Object System.Collections.ArrayList

$runStamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$runDir   = Join-Path $script:OutRoot "run_$runStamp"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

Write-Host '=== Environment ===' -ForegroundColor Cyan
$env0 = Get-ToolVersions
foreach ($k in $env0.Keys) { Write-Host ("  {0,-11}: {1}" -f $k, $env0[$k]) }
if ($env0.dirty) { Write-Host '  WARNING: working tree is dirty; this run is not reproducible' -ForegroundColor Yellow }

if (-not $SkipBuild) {
    Write-Host "`n=== Build ===" -ForegroundColor Cyan
    & cmake --preset windows-subsea-x64
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    & cmake --build --preset windows-subsea-x64
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
}

if (-not $SkipUnit) {
    Write-Host "`n=== Unit tests (ctest) ===" -ForegroundColor Cyan
    Push-Location $build
    try {
        & ctest -C RelWithDebInfo --output-on-failure
        $rc = $LASTEXITCODE
    } finally { Pop-Location }
    if ($rc -ne 0) { $total += 1; Write-Host 'ctest FAILED' -ForegroundColor Red }
    [void]$runLog.Add([pscustomobject]@{ suite='unit'; script='ctest'; result=$(if($rc -eq 0){'pass'}else{'fail'}); failures=$rc; duration_s=0 })
}
if ($Suite -eq 'unit') { exit $total }

$map = @{
    '0' = @('t0-foundation.ps1');     '1' = @('t1-shell.ps1')
    '2' = @('t2-video-elements.ps1'); '3' = @('t3-data-core.ps1')
    '4' = @('t4-overlay.ps1');        '5' = @('t5-transports.ps1')
    '6' = @('t6-multirecord.ps1');    '7' = @('t7-clips.ps1','t7-snapshots.ps1')
    '8' = @('t8-sidecar.ps1');        '9' = @('t9-streaming.ps1')
}

if     ($Suite -eq 'all')   { $run = '0','1','2','3','4','5','6','7','8','9' }
elseif ($Suite -eq 'quick') { $run = '0','1','2' }   # grows as suites land
else                        { $run = @($Suite) }

foreach ($s in $run) {
    foreach ($name in $map[$s]) {
        $path = Join-Path $PSScriptRoot $name
        if (-not (Test-Path $path)) {
            Write-Host "skip: $name not present yet" -ForegroundColor Yellow
            [void]$runLog.Add([pscustomobject]@{ suite=$s; script=$name; result='absent'; failures=0; duration_s=0 })
            continue
        }
        Write-Host "`n=== Suite $s : $name ===" -ForegroundColor Cyan
        $t0 = Get-Date
        & $path -IncludeSoak:$IncludeSoak
        $rc = $LASTEXITCODE
        $total += $rc
        [void]$runLog.Add([pscustomobject]@{
            suite=$s; script=$name
            result=$(if($rc -eq 0){'pass'}else{'fail'}); failures=$rc
            duration_s=[math]::Round(((Get-Date)-$t0).TotalSeconds,1)
        })
    }
}

# --- roll-up -----------------------------------------------------------------
$idx = @("# Test run $runStamp", '',
         ("Commit ``{0}`` on ``{1}``" -f $env0.commit, $env0.branch), '')
if ($env0.dirty) { $idx += '> **Warning:** working tree was dirty; not reproducible from the commit alone.'; $idx += '' }
$idx += @('| Suite | Script | Result | Failures | Duration |', '|---|---|---|---|---|')
foreach ($r in $runLog) { $idx += "| $($r.suite) | $($r.script) | $($r.result) | $($r.failures) | $($r.duration_s)s |" }
$idx += @('', "**Total failures: $total**", '', 'Per-suite reports are alongside each suite''s workspace under ``out/``.')
($idx -join "`n") | Out-File -Encoding utf8 (Join-Path $runDir 'index.md')
$runLog | ConvertTo-Json -Depth 4 | Out-File -Encoding utf8 (Join-Path $runDir 'index.json')

Write-Host ''
Write-Host "roll-up: $(Join-Path $runDir 'index.md')" -ForegroundColor Cyan
if ($total -eq 0) { Write-Host 'ALL SUITES PASSED' -ForegroundColor Green }
else              { Write-Host "$total FAILURE(S)" -ForegroundColor Red }
exit $total
