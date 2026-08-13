# Mission Capture -- suite T2: video elements.
# See docs/subsea/phase-2-video-elements.md.
#
# Deliberately runs with no capture hardware. The cards vary job to job and are
# not being validated (OI-6, closed by decision), so what this suite proves is
# that the *product* behaves: the right backend is chosen, our settings are
# applied, and a Job whose camera is absent still opens and still records.
#
# The RTSP and device-recovery assertions arrive with tasks 2.3 and 2.4.

param([switch]$IncludeSoak)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$ws = New-TestWorkspace -Name 't2'

Reset-PortableConfig | Out-Null
$manifestPath = Join-Path (Get-RunDir) 'ui-manifest.json'
if (Test-Path $manifestPath) { Remove-Item -Force $manifestPath }

$code = Invoke-App -AppArgs @('--dump-ui-manifest', '../../ui-manifest.json') -TimeoutSec 120
Assert-True ($code -eq 0) "Manifest dump exited cleanly (code $code)" 'P2-AC1'

if (-not (Test-Path $manifestPath)) {
    Add-Failure 'No manifest produced; cannot check the capture factory' 'P2-AC1'
    exit (Write-TestSummary -Suite 'T2 Video Elements' -Workspace $ws)
}

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

# --- P2-AC1: one Element type, the right backend behind it -------------------
# Asserted against synthetic devices, because the mapping is a pure function and
# waiting for hardware to check it would mean never checking it.
$factory = @($manifest.captureFactory)
Assert-True ($factory.Count -ge 3) "Capture factory reports $($factory.Count) backend cases" 'P2-AC1'

function Get-Case([string]$given) { return @($factory | Where-Object { $_.given -eq $given }) }

$dl = @(Get-Case 'decklink-input')
Assert-True ($dl.Count -eq 1 -and $dl[0].resolvesTo -eq 'decklink-input') `
            'A DeckLink device resolves to the DeckLink source' 'P2-AC1'

$ds = @(Get-Case 'dshow_input')
Assert-True ($ds.Count -eq 1 -and $ds[0].resolvesTo -eq 'dshow_input') `
            'A DirectShow device resolves to the DirectShow source' 'P2-AC1'

# "There may be other capture cards as well" -- an unrecognised backend must
# fall through rather than refuse. A wrong guess shows no signal, which is
# recoverable; refusing to create the Element is not.
$unknown = @(Get-Case 'some-future-backend')
Assert-True ($unknown.Count -eq 1 -and $unknown[0].resolvesTo -eq 'dshow_input') `
            'An unrecognised backend falls through to DirectShow' 'P2-AC1'

# --- P2-AC2: our settings, not OBS's -----------------------------------------
if ($dl.Count -eq 1) {
    $s = $dl[0].settingsJson | ConvertFrom-Json
    Assert-True ($s.device_hash -eq 'test-device-id') 'DeckLink Element carries the device hash' 'P2-AC2'

    # Auto input-format detection. A card set to the wrong mode shows nothing at
    # all, and the operator has no way to tell which of the two is wrong.
    Assert-True ($s.mode_id -eq -1) "DeckLink input format is Auto (got '$($s.mode_id)')" 'P2-AC2'
    Assert-True ($s.buffering -eq $false) 'DeckLink buffering is off for latency' 'P2-AC2'
}

if ($ds.Count -eq 1) {
    $s = $ds[0].settingsJson | ConvertFrom-Json
    Assert-True ($s.video_device_id -eq 'test-device-id') 'DirectShow Element carries the device id' 'P2-AC2'
    Assert-True ($s.buffering -eq $false) 'DirectShow buffering is off for latency' 'P2-AC2'

    # Upstream turns capture off when a source is not on screen. Phase 6 records
    # several Canvases at once, only one of which is ever on screen, so that
    # default would silently produce black recordings.
    Assert-True ($s.deactivate_when_not_showing -eq $false) `
                'Capture continues while the Element is off-screen' 'P2-AC2'
}

# --- P2-AC3: a Job whose camera is absent still works ------------------------
# The case that actually happens: a Job built on the vessel opened on a laptop,
# or a card swapped between dives. No hardware is needed to test it, and it is
# the behaviour most likely to be got wrong for arbitrary cards.
Reset-PortableConfig | Out-Null
Copy-Fixture -Relative 'jobs\missing-device'

$missingManifest = Join-Path (Get-RunDir) 'ui-missing.json'
if (Test-Path $missingManifest) { Remove-Item -Force $missingManifest }

$code = Invoke-App -AppArgs @('--collection', 'missing-device', '--dump-ui-manifest', '../../ui-missing.json') `
                   -TimeoutSec 120
Assert-True ($code -eq 0) 'A Job with an absent capture device opens cleanly' 'P2-AC3'

if (Test-Path $missingManifest) {
    $mm = Get-Content $missingManifest -Raw | ConvertFrom-Json
    $canvases = @($mm.layers)

    Assert-True ($canvases.Count -eq 1 -and $canvases[0].name -eq 'Pilot Cam') `
                'The Canvas loaded' 'P2-AC3'

    if ($canvases.Count -ge 1) {
        $els = @($canvases[0].elements)
        # The Element must still be there. Dropping it would silently discard
        # the operator's layout the first time a Job moved between machines.
        Assert-True ($els.Count -eq 1 -and $els[0].name -eq 'Pilot Cam Feed') `
                    'The Element for the absent camera is still present' 'P2-AC3'
        if ($els.Count -eq 1) {
            Assert-True ($els[0].sourceId -eq 'dshow_input') `
                        'The Element kept its backend' 'P2-AC3'
            Assert-True ($els[0].visible -eq $true) `
                        'The Element is not silently hidden' 'P2-AC3'
        }
    }
}

Assert-NoLogErrors -Criterion 'P2-AC3'

Save-ConfigToWorkspace -Workspace $ws
exit (Write-TestSummary -Suite 'T2 Video Elements' -Workspace $ws)
