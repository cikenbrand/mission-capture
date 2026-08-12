# Mission Capture -- suite T0: foundation smoke.
# See docs/subsea/testing.md and docs/subsea/phase-0-foundation.md.

param([switch]$IncludeSoak)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$ws = New-TestWorkspace -Name 't0'

# --- P0-AC1/AC2: branding ----------------------------------------------------
$exe = Get-AppExe
$vi  = (Get-Item $exe).VersionInfo

Assert-True ($vi.ProductName -eq 'Mission Capture') `
            "ProductName is 'Mission Capture' (got '$($vi.ProductName)')" 'P0-AC1'
Assert-True ($vi.CompanyName -eq 'Cyberian Resources') `
            "CompanyName is 'Cyberian Resources' (got '$($vi.CompanyName)')" 'P0-AC1'
Assert-True ((Split-Path -Leaf $exe) -notmatch '(?i)^obs') `
            "Executable is not named after OBS" 'P0-AC1'
Assert-True ($vi.FileDescription -notmatch '(?i)\bobs\b') `
            "FileDescription carries no OBS branding" 'P0-AC1'

# --- P0-AC5: --dump-ui-manifest produces valid JSON --------------------------
Reset-PortableConfig | Out-Null
$manifestPath = Join-Path (Get-RunDir) 'ui-manifest.json'
if (Test-Path $manifestPath) { Remove-Item -Force $manifestPath }

$code = Invoke-App -AppArgs @('--dump-ui-manifest', '../../ui-manifest.json') -TimeoutSec 120
Assert-True ($code -eq 0) "Manifest dump exited cleanly (code $code)" 'P0-AC5'
Assert-True (Test-Path $manifestPath) 'Manifest file produced' 'P0-AC5'

$manifest = $null
if (Test-Path $manifestPath) {
    $parsed = $true
    try { $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json } catch { $parsed = $false }
    Assert-True $parsed 'Manifest is valid JSON' 'P0-AC5'
}

if ($manifest) {
    Assert-True (@($manifest.actions).Count -gt 0)      "Manifest lists $(@($manifest.actions).Count) visible actions" 'P0-AC5'
    Assert-True (@($manifest.docks).Count -gt 0)        "Manifest lists $(@($manifest.docks).Count) docks" 'P0-AC5'
    Assert-True (@($manifest.features).Count -gt 0)     "Manifest lists $(@($manifest.features).Count) feature flags" 'P0-AC5'
    Assert-True (@($manifest.hotkeys).Count -gt 0)      "Manifest lists $(@($manifest.hotkeys).Count) OBS hotkeys" 'P0-AC5'

    # --- P0-AC2: config directory is fully rebranded -------------------------
    $stray = @(Get-ChildItem (Get-PortableConfigRoot) -Recurse -Directory -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -eq 'obs-studio' })
    Assert-True ($stray.Count -eq 0) 'No "obs-studio" directory created under config' 'P0-AC2'
    Assert-True (Test-Path (Get-AppConfigDir)) `
                'Config lives under "Cyberian Resources\Mission Capture"' 'P0-AC2'

    # --- P0-AC4: feature flags demonstrably hide something -------------------
    Assert-True (@($manifest.hiddenActions).Count -gt 0) `
                "$(@($manifest.hiddenActions).Count) actions hidden by feature flags" 'P0-AC4'
}

Assert-NoLogErrors -Criterion 'P0-AC3'

# --- P0-AC4: an override in features.ini brings a feature back ---------------
$featuresIni = Join-Path (Get-AppConfigDir) 'features.ini'
Assert-True (Test-Path $featuresIni) 'features.ini self-generated on first run' 'P0-AC4'

if ((Test-Path $featuresIni) -and $manifest) {
    $before = @($manifest.hiddenActions).Count

    (Get-Content $featuresIni) -replace '^AdvancedAudio=false', 'AdvancedAudio=true' |
        Set-Content -Encoding utf8 $featuresIni

    $code = Invoke-App -AppArgs @('--dump-ui-manifest', '../../ui-manifest.json') -TimeoutSec 120
    Assert-True ($code -eq 0) 'Second manifest dump exited cleanly' 'P0-AC4'

    $after = Get-Content $manifestPath -Raw | ConvertFrom-Json
    Assert-True (@($after.hiddenActions).Count -lt $before) `
        ("Enabling AdvancedAudio unhid actions ({0} -> {1} hidden)" -f $before, @($after.hiddenActions).Count) 'P0-AC4'

    $flag = $after.features | Where-Object { $_.key -eq 'AdvancedAudio' }
    Assert-True ($flag.enabled -eq $true) 'features.ini override is reflected in the manifest' 'P0-AC4'
}

# --- Evidence for OI-23: hotkeys outlive their hidden UI ---------------------
# Not a failure yet -- Phase 1 task 1.5 owns the fix. Recorded so the report
# shows the leak closing when it does.
if ($manifest) {
    $disabled = @($manifest.features | Where-Object { -not $_.enabled } | ForEach-Object { $_.key })
    $leaks = @($manifest.hotkeys | Where-Object { $_.name -match 'ReplayBuffer|Streaming|Transition' })
    if ($leaks.Count -gt 0) {
        Add-Skip ("OI-23: $($leaks.Count) hotkeys still registered for hidden features: " +
                  (($leaks | ForEach-Object { $_.name }) -join ', ')) 'P1-AC9'
    } else {
        Add-Pass 'No hotkeys registered for hidden features' 'P1-AC9'
    }
}

Save-ConfigToWorkspace -Workspace $ws
exit (Write-TestSummary -Suite 'T0 Foundation' -Workspace $ws)
