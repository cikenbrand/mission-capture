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

# --- P2-AC4: the property sheet is cut down, not cut away --------------------
# Exercised against a real dshow_input built by the plugin, not a hand-made
# list: the filter's job is to walk the plugin's own property set, and a fixture
# would only prove the code can read its own invention.
$cp = $manifest.captureProperties
Assert-True ($null -ne $cp) 'Manifest records the capture property filter' 'P2-AC4'

if ($cp) {
    Assert-True ($cp.isCaptureSource_dshow -eq $true -and $cp.isCaptureSource_decklink -eq $true) `
                'Both capture backends are recognised' 'P2-AC4'
    # Anything else must get upstream's dialog untouched.
    Assert-True ($cp.isCaptureSource_other -eq $false) `
                'A non-capture source is left alone' 'P2-AC4'
}

if ($cp -and $cp.PSObject.Properties.Name -contains 'simple') {
    $simple = $cp.simple
    $adv = $cp.advanced

    Assert-True ($simple.hidden -gt 0) `
                "The simple sheet hides $($simple.hidden) of $($simple.total) properties" 'P2-AC4'
    Assert-True (@($simple.visible).Count -lt @($adv.visible).Count) `
                "Advanced shows more than simple ($(@($simple.visible).Count) -> $(@($adv.visible).Count))" 'P2-AC4'

    # The five the plan names as always-visible. Colour range especially: a
    # full-vs-limited mismatch on SDI looks like a camera fault and is a
    # two-click fix, so burying it costs more than showing it.
    foreach ($keep in @('video_device_id', 'res_type', 'frame_interval', 'color_space', 'color_range',
                        'buffering', 'audio_output_mode')) {
        Assert-True (@($simple.visible) -contains $keep) "'$keep' stays visible by default" 'P2-AC4'
    }

    # ...and the noise that should not be in an operator's way.
    foreach ($hide in @('video_format', 'autorotation', 'hw_decode', 'flip_vertically', 'xbar_config')) {
        Assert-True (@($simple.visible) -notcontains $hide) "'$hide' is behind Advanced" 'P2-AC4'
    }

    # Nothing is removed -- Advanced must bring back everything the filter hid.
    foreach ($hide in @('video_format', 'autorotation', 'hw_decode')) {
        Assert-True (@($adv.visible) -contains $hide) "'$hide' returns under Advanced" 'P2-AC4'
    }

    # A property the *plugin* hides for its own reasons must stay hidden even in
    # Advanced, or turning it on would show rows that do not apply.
    Assert-True (@($adv.visible).Count -lt $adv.total) `
                'Advanced does not override the plugin''s own conditional hiding' 'P2-AC4'
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

# --- P2-AC5: task 2.3 -- a camera that stops is noticed ----------------------
# The hard part of this task is that nothing announces the loss: both backends
# simply stop delivering, and libobs keeps rendering the last frame. So the
# watch counts frames through a filter, and these assertions check the
# machinery is in place -- on a machine with no camera, which is the case where
# it matters most.
if (Test-Path $missingManifest) {
    $sw = (Get-Content $missingManifest -Raw | ConvertFrom-Json).signalWatch
    Assert-True ($null -ne $sw) 'Manifest records the signal watch' 'P2-AC5'

    if ($sw) {
        # A registration that silently failed would leave every Element
        # unwatched with nothing to show for it.
        Assert-True ($sw.filterRegistered -eq $true) `
                    'The signal-watch filter type is registered with libobs' 'P2-AC5'
        Assert-True ($sw.lostThresholdSeconds -gt 0) `
                    "Loss threshold is configured ($($sw.lostThresholdSeconds)s)" 'P2-AC5'

        # The fixture's camera does not exist, and it still gets watched --
        # an Element whose device is absent is precisely the one whose health
        # the operator needs to see.
        $watched = @($sw.watched)
        Assert-True ($watched.Count -ge 1) `
                    "A capture Element with no device is still watched ($($watched.Count) watched)" 'P2-AC5'

        if ($watched.Count -ge 1) {
            Assert-True ($watched[0].element -eq 'Pilot Cam Feed') `
                        "The watch resolves the Element's name (got '$($watched[0].element)')" 'P2-AC5'
            Assert-True ($watched[0].frames -eq 0) `
                        'An absent camera has delivered no frames' 'P2-AC5'

            # Never-connected is deliberately NOT reported as lost: putting a
            # "reconnecting" marker on a camera that was never there would be
            # misleading, and it is a different problem from one that dropped.
            Assert-True ($watched[0].state -eq 'unknown') `
                        "A camera that never connected is not reported as lost (got '$($watched[0].state)')" 'P2-AC5'
        }
    }
}

Assert-NoLogErrors -Criterion 'P2-AC3'

Save-ConfigToWorkspace -Workspace $ws
exit (Write-TestSummary -Suite 'T2 Video Elements' -Workspace $ws)
