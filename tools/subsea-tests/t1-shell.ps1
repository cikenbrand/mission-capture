# Mission Capture -- suite T1: shell and Layers tree.
# See docs/subsea/phase-1-shell-and-layers.md.
#
# Grows as Phase 1 lands. Today it covers task 1.1 (terminology) only; the
# Layers-tree structure, Z-order and golden-manifest checks arrive with 1.2-1.4.

param([switch]$IncludeSoak)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\common.ps1')

$ws = New-TestWorkspace -Name 't1'

Reset-PortableConfig | Out-Null
$manifestPath = Join-Path (Get-RunDir) 'ui-manifest.json'
if (Test-Path $manifestPath) { Remove-Item -Force $manifestPath }

$code = Invoke-App -AppArgs @('--dump-ui-manifest', '../../ui-manifest.json') -TimeoutSec 120
Assert-True ($code -eq 0) "Manifest dump exited cleanly (code $code)" 'P1-AC7'

if (-not (Test-Path $manifestPath)) {
    Add-Failure 'No manifest produced; cannot run terminology sweep' 'P1-AC7'
    exit (Write-TestSummary -Suite 'T1 Shell and Layers' -Workspace $ws)
}

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

# --- P1-AC7: the UI speaks Canvas / Element / Layers / Job / Rig -------------
#
# Upstream's vocabulary must not reach the user. This is a regression guard as
# much as a check: every upstream merge brings new strings, and a new menu item
# saying "Add Source" would otherwise slip in unnoticed.
$banned = @(
    @{ word = 'Scene';            instead = 'Canvas'  },
    @{ word = 'Scenes';           instead = 'Canvases'},
    @{ word = 'Source';           instead = 'Element' },
    @{ word = 'Sources';          instead = 'Elements'},
    @{ word = 'Scene Collection'; instead = 'Job'     },
    @{ word = 'Profile';          instead = 'Rig'     }
)

# Strings that legitimately keep an old word, with the reason.
$allowed = @(
    'Source code for this program',   # About: source code, not an Element
    'Built on OBS Studio'             # About: upstream attribution
)

$surfaces = @()
foreach ($a in @($manifest.actions) + @($manifest.hiddenActions)) {
    if ($a.text) { $surfaces += [pscustomobject]@{ kind = 'action'; name = $a.name; text = $a.text } }
}
foreach ($d in @($manifest.docks))  { if ($d.title) { $surfaces += [pscustomobject]@{ kind='dock'; name=$d.name; text=$d.title } } }
foreach ($m in @($manifest.menus))  { if ($m.title) { $surfaces += [pscustomobject]@{ kind='menu'; name=$m.name; text=$m.title } } }

foreach ($b in $banned) {
    $pattern = '\b' + [regex]::Escape($b.word) + '\b'
    $hits = @($surfaces | Where-Object {
        if ($_.text -notmatch $pattern) { return $false }
        foreach ($ok in $allowed) { if ($_.text -like "*$ok*") { return $false } }
        return $true
    })

    Assert-True ($hits.Count -eq 0) `
        ("No visible '{0}' (should be '{1}'){2}" -f $b.word, $b.instead,
         $(if ($hits.Count) { ' -- ' + (($hits | ForEach-Object { "$($_.kind) $($_.name): '$($_.text)'" }) -join '; ') } else { '' })) `
        'P1-AC7'
}

# --- The two words that collide or mislead -----------------------------------
# "canvas" in OBS's base-output-resolution sense reads as a Mission Capture
# Canvas, so it must not appear in the UI at all.
$canvasHits = @($surfaces | Where-Object { $_.text -cmatch '\bcanvas\b' })
Assert-True ($canvasHits.Count -eq 0) `
    ("No lower-case 'canvas' (OBS's resolution sense) in visible text" +
     $(if ($canvasHits.Count) { ' -- ' + (($canvasHits | ForEach-Object { "$($_.name): '$($_.text)'" }) -join '; ') } else { '' })) `
    'P1-AC7'

$obsHits = @($surfaces | Where-Object {
    if ($_.text -notmatch '\bOBS\b') { return $false }
    foreach ($ok in $allowed) { if ($_.text -like "*$ok*") { return $false } }
    return $true
})
Assert-True ($obsHits.Count -eq 0) `
    ("No stray 'OBS' branding in visible text" +
     $(if ($obsHits.Count) { ' -- ' + (($obsHits | ForEach-Object { "$($_.name): '$($_.text)'" }) -join '; ') } else { '' })) `
    'P1-AC7'

Assert-True ($surfaces.Count -gt 50) "Swept $($surfaces.Count) visible UI strings" 'P1-AC7'

# --- P1-AC1 / P1-AC2: the Layers model ---------------------------------------
#
# Task 1.2 builds the model before 1.3 builds the view, so this walks the model
# as a QTreeView would, via the manifest. The fixture Job has two Canvases; the
# first holds three Elements stacked Bottom / Middle / Top in libobs order.
Reset-PortableConfig | Out-Null
Copy-Fixture -Relative 'jobs\layers-test'
$layersManifest = Join-Path (Get-RunDir) 'ui-layers.json'
if (Test-Path $layersManifest) { Remove-Item -Force $layersManifest }

$code = Invoke-App -AppArgs @('--collection', 'layers-test', '--dump-ui-manifest', '../../ui-layers.json') -TimeoutSec 120
Assert-True ($code -eq 0) 'Manifest dump with the layers fixture exited cleanly' 'P1-AC1'

if (Test-Path $layersManifest) {
    $lm = Get-Content $layersManifest -Raw | ConvertFrom-Json
    $canvases = @($lm.layers)

    Assert-True ($canvases.Count -eq 2) "Layers model has 2 Canvases (got $($canvases.Count))" 'P1-AC1'

    $cam1 = $canvases | Where-Object { $_.name -eq 'Camera 1' }
    $cam2 = $canvases | Where-Object { $_.name -eq 'Camera 2' }
    Assert-True ($null -ne $cam1 -and $null -ne $cam2) 'Both Canvases present by name' 'P1-AC1'

    if ($cam1) {
        $els = @($cam1.elements)
        Assert-True ($els.Count -eq 3) "Camera 1 has 3 Elements (got $($els.Count))" 'P1-AC1'

        # THE assertion this task exists for. libobs enumerates bottom-first;
        # the tree must show top-first. Row 0 is the topmost Element and maps
        # to the HIGHEST libobs index. Getting this backwards renders Z-order
        # upside down and looks like a rendering bug, not a model bug.
        Assert-True ($els[0].name -eq 'Top' -and $els[0].row -eq 0 -and $els[0].libobsIndex -eq 2) `
                    'Row 0 is the topmost Element (Top, libobs index 2)' 'P1-AC2'
        Assert-True ($els[1].name -eq 'Middle' -and $els[1].libobsIndex -eq 1) `
                    'Row 1 is Middle (libobs index 1)' 'P1-AC2'
        Assert-True ($els[2].name -eq 'Bottom' -and $els[2].row -eq 2 -and $els[2].libobsIndex -eq 0) `
                    'Row 2 is the bottommost Element (Bottom, libobs index 0)' 'P1-AC2'

        # parent() must round-trip or the view renders orphaned rows.
        Assert-True (@($els | Where-Object { -not $_.parentResolves }).Count -eq 0) `
                    'Every Element resolves back to its Canvas via parent()' 'P1-AC1'

        # Per-Element state is read from libobs, not cached stale.
        $top = $els | Where-Object { $_.name -eq 'Top' }
        Assert-True ($top.visible -eq $false -and $top.locked -eq $true) `
                    'Element visible/locked flags reflect the Job file' 'P1-AC1'
        Assert-True ($top.sourceId -eq 'color_source_v3') 'Element reports its source id' 'P1-AC1'
    }

    if ($cam2) {
        Assert-True (@($cam2.elements).Count -eq 1) 'Camera 2 has 1 Element' 'P1-AC1'
    }

    # The Layers dock exists and is registered under the expected objectName,
    # which is also what the feature flag targets.
    $layersDock = @($lm.docks | Where-Object { $_.name -eq 'layersDock' })
    Assert-True ($layersDock.Count -eq 1) 'Layers dock is registered' 'P1-AC1'
    if ($layersDock.Count -eq 1) {
        Assert-True ($layersDock[0].title -eq 'Layers') `
                    "Layers dock is titled 'Layers' (got '$($layersDock[0].title)')" 'P1-AC1'
    }

    # --- P1-AC9: the docks Layers replaces are gone --------------------------
    foreach ($retired in @('scenesDock', 'sourcesDock')) {
        $d = @($lm.docks | Where-Object { $_.name -eq $retired })
        # They stay in the widget hierarchy on purpose -- SaveSceneListOrder()
        # still reads the Scenes list to persist Canvas order -- but must not be
        # visible.
        Assert-True (($d.Count -eq 0) -or (-not $d[0].visible)) `
                    "'$retired' is retired from the UI" 'P1-AC9'
    }

    # Exactly one Canvas is the program Canvas.
    $program = @($canvases | Where-Object { $_.program })
    Assert-True ($program.Count -eq 1 -and $program[0].name -eq 'Camera 1') `
                "Camera 1 is the program Canvas (found $($program.Count) marked)" 'P1-AC3'
} else {
    Add-Failure 'No layers manifest produced' 'P1-AC1'
}

# --- P1-AC9: task 1.5 -- a hidden feature has no way back --------------------
# The audit (docs/subsea/ui-audit.md) found four routes back to a hidden
# feature. Each is asserted here, because each was invisible to the checks that
# existed before it: 0.4 could report "30 hidden / 0 missing" with all four open.

# 1. Hotkeys must not outlive the UI they belong to. Was OI-23.
$leaks = @($manifest.hotkeys | Where-Object { $_.name -match 'ReplayBuffer|Streaming|Transition' })
Assert-True ($leaks.Count -eq 0) `
            ("No hotkeys registered for hidden features" +
             $(if ($leaks.Count) { ": " + (($leaks | ForEach-Object { $_.name }) -join ', ') } else { '' })) 'P1-AC9'

# 2. A hidden dock must not keep a live View > Docks toggle. The toggle actions
#    are created by Qt and were nameless until 1.5 named them -- which is why
#    this leak survived 0.4 unnoticed.
foreach ($d in @($manifest.docks)) {
    $toggle = @(@($manifest.actions) + @($manifest.hiddenActions) |
                Where-Object { $_.name -eq ($d.name + 'Toggle') })
    if ($toggle.Count -ne 1) {
        Add-Failure "Dock '$($d.name)' has no named toggle action" 'P1-AC9'
        continue
    }
    Assert-True ($toggle[0].visible -eq $d.visible) `
                ("Dock '$($d.name)' toggle matches its visibility " +
                 "(dock=$($d.visible), toggle=$($toggle[0].visible))") 'P1-AC9'
}

# 3. A disabled settings page must be unreachable from the sidebar. The dialog
#    is not a child of the main window, so nothing saw this before 1.5 taught
#    the manifest to build it.
$pages = @($manifest.settingsPages)
Assert-True ($pages.Count -gt 0) "Manifest records $($pages.Count) settings pages" 'P1-AC9'
if ($pages.Count) {
    $stream = @($pages | Where-Object { $_.page -eq 'streamPage' })
    Assert-True ($stream.Count -eq 1 -and -not $stream[0].visible) `
                'Stream settings page is unreachable while StreamingUI is off' 'P1-AC9'

    # Guard against over-hiding: the pages we keep must still be reachable.
    foreach ($keep in @('generalPage', 'audioPage', 'videoPage', 'hotkeyPage')) {
        $pg = @($pages | Where-Object { $_.page -eq $keep })
        Assert-True ($pg.Count -eq 1 -and $pg[0].visible) "Settings page '$keep' is still reachable" 'P1-AC9'
    }
}

# 4. A menu emptied of its items must not open onto nothing.
$hiddenMenus = @($manifest.menus | Where-Object { -not $_.visible } | ForEach-Object { $_.name })
foreach ($gone in @('orderMenu', 'menuCrashLogs', 'sceneListModeMenu')) {
    Assert-True ($hiddenMenus -contains $gone) "Menu '$gone' is hidden" 'P1-AC9'
}

# The Order actions moved to the Layers context menu rather than being removed,
# so they must stay visible -- their shortcuts are how keyboard users reorder.
foreach ($kept in @('actionMoveUp', 'actionMoveDown', 'actionMoveToTop', 'actionMoveToBottom')) {
    $a = @($manifest.actions | Where-Object { $_.name -eq $kept })
    Assert-True ($a.Count -eq 1) "Order action '$kept' survives the move to Layers" 'P1-AC9'
}

# --- P1-AC8: task 1.6 -- the Add Element list is restricted, not gutted ------
$types = @($manifest.elementTypes)
Assert-True ($types.Count -gt 0) "Manifest lists $($types.Count) registered Element types" 'P1-AC8'

if ($types.Count) {
    $offered = @($types | Where-Object { $_.offered })

    # The distinction the whole task rests on: unoffered types stay REGISTERED,
    # so a Job referencing one still loads. If these were equal we would have
    # unregistered them, which would break exactly that.
    Assert-True ($offered.Count -lt $types.Count) `
                ("Unoffered types stay registered ($($offered.Count) offered of $($types.Count) registered)") 'P1-AC8'

    # Video Capture Device is the one of the three that exists today; RTSP
    # arrives in 2.4 and Overlay in phase 4.
    $dshow = @($types | Where-Object { $_.unversionedId -eq 'dshow_input' })
    Assert-True ($dshow.Count -eq 1 -and $dshow[0].offered) 'Video Capture Device is offered' 'P1-AC8'

    # Everything OBS offers that this product does not.
    foreach ($blocked in @('ffmpeg_source', 'image_source', 'color_source', 'slideshow',
                           'text_gdiplus', 'text_ft2_source', 'monitor_capture',
                           'window_capture', 'game_capture', 'wasapi_input_capture',
                           'wasapi_output_capture')) {
        $t = @($types | Where-Object { $_.unversionedId -eq $blocked })
        if ($t.Count -eq 0) { continue }   # not built in this configuration
        Assert-True (-not $t[0].offered) "'$blocked' is registered but not offered" 'P1-AC8'
    }
}

# The Layers context menu borrows these two from upstream. They are the only
# route to adding anything now that the Sources dock is retired, so a rename
# upstream must fail loudly here rather than silently emptying the menu.
foreach ($borrowed in @('actionAddScene', 'actionAddSource',
                        'actionMoveUp', 'actionMoveDown', 'actionMoveToTop', 'actionMoveToBottom')) {
    $a = @(@($manifest.actions) + @($manifest.hiddenActions) | Where-Object { $_.name -eq $borrowed })
    Assert-True ($a.Count -eq 1) "Layers menu can still borrow '$borrowed' from upstream" 'P1-AC8'
}

# --- P1-AC8: AllSourceTypes is a real escape hatch ---------------------------
# Same shape as T0's AdvancedAudio check: prove the flag does something, so the
# restriction can never become a dead end on a vessel.
$featuresIni = Join-Path (Get-AppConfigDir) 'features.ini'
if ((Test-Path $featuresIni) -and $types.Count) {
    $before = @($types | Where-Object { $_.offered }).Count

    (Get-Content $featuresIni) -replace '^AllSourceTypes=false', 'AllSourceTypes=true' |
        Set-Content -Encoding utf8 $featuresIni

    $code = Invoke-App -AppArgs @('--dump-ui-manifest', '../../ui-all-types.json') -TimeoutSec 120
    Assert-True ($code -eq 0) 'Manifest dump with AllSourceTypes on exited cleanly' 'P1-AC8'

    $allPath = Join-Path (Get-RunDir) 'ui-all-types.json'
    if (Test-Path $allPath) {
        $allTypes = @((Get-Content $allPath -Raw | ConvertFrom-Json).elementTypes)
        $after = @($allTypes | Where-Object { $_.offered }).Count
        Assert-True ($after -gt $before) `
                    "AllSourceTypes restores the full list ($before -> $after offered)" 'P1-AC8'
        Assert-True ($after -eq $allTypes.Count) 'Every registered type is offered with the flag on' 'P1-AC8'
    } else {
        Add-Failure 'No manifest produced with AllSourceTypes on' 'P1-AC8'
    }

    # Put it back, so a later suite in the same workspace sees the default.
    (Get-Content $featuresIni) -replace '^AllSourceTypes=true', 'AllSourceTypes=false' |
        Set-Content -Encoding utf8 $featuresIni
}

# --- P1-AC10: task 1.7 -- the Rig template -----------------------------------
# These are set with config_set_default_*, which writes nothing to basic.ini, so
# the manifest is the only place the effective values can be read.
$rd = $manifest.recordingDefaults
Assert-True ($null -ne $rd) 'Manifest records the recording defaults' 'P1-AC10'

if ($rd) {
    # The one that is not a preference: MP4 writes its index at the end, so a
    # power loss mid-dive leaves an unopenable file. MKV does not.
    Assert-True ($rd.'SimpleOutput/RecFormat2' -eq 'mkv') `
                "Recording container is MKV (got '$($rd.'SimpleOutput/RecFormat2')')" 'P1-AC10'
    Assert-True ($rd.'AdvOut/RecFormat2' -eq 'mkv') `
                "Advanced-mode container is MKV (got '$($rd.'AdvOut/RecFormat2')')" 'P1-AC10'

    # ...and nothing silently converts it afterwards.
    Assert-True ($rd.'Video/AutoRemux' -eq $false) 'Auto-remux is off' 'P1-AC10'

    # Quality-targeted, not bitrate-targeted. HQ is CRF 16 / CQP.
    Assert-True ($rd.'SimpleOutput/RecQuality' -eq 'HQ') `
                "Recording is quality-targeted (got '$($rd.'SimpleOutput/RecQuality')')" 'P1-AC10'

    # Hardware encoder where the machine has one. x264 is a valid outcome on a
    # runner with no GPU, so this asserts the choice is sane rather than fixed.
    Assert-True ($rd.'SimpleOutput/RecEncoder' -in @('nvenc', 'amd', 'x264')) `
                "Recording encoder resolved to '$($rd.'SimpleOutput/RecEncoder')'" 'P1-AC10'

    Assert-True ($rd.'Output/FilenameFormatting' -eq '%JOB%_%CANVAS%_%CCYY%%MM%%DD%_%hh%%mm%%ss%') `
                'Filename template is the inspection template' 'P1-AC10'

    # The tokens must actually resolve -- a template that survives into the
    # filename literally would be worse than not having one.
    Assert-True ($rd.filenameExample -notmatch '%JOB%|%CANVAS%') `
                "%JOB% and %CANVAS% expand (got '$($rd.filenameExample)')" 'P1-AC10'
    Assert-True ($rd.filenameExample -match '^[^<>:"/\\|?*]+$') `
                'Expanded filename contains no characters illegal in a filename' 'P1-AC10'

    Assert-True ($rd.'AdvOut/RecSplitFile' -eq $true) 'Recording auto-split is on' 'P1-AC10'
    Assert-True ($rd.'AdvOut/RecSplitFileType' -eq 'Time') 'Auto-split is by time' 'P1-AC10'

    # One comms channel, no desktop audio. Channel 1 is desktop, 3 is mic/aux.
    $channels = @($rd.audioChannels)
    $desktop = @($channels | Where-Object { $_.channel -eq 1 })
    Assert-True ($desktop.Count -eq 0) `
                'A new Job captures no desktop audio' 'P1-AC10'
    Assert-True ($channels.Count -le 1) `
                "A new Job has at most one global audio channel (got $($channels.Count))" 'P1-AC10'
}

# --- P1-AC11: task 1.8 -- the Job wizard and its metadata --------------------

# File > New Job must exist and be reachable. It is created in code rather than
# in OBSBasic.ui, so a silent failure here would leave no way to start a Job.
$newJob = @($manifest.actions | Where-Object { $_.name -eq 'actionNewJob' })
Assert-True ($newJob.Count -eq 1) 'File > New Job action exists and is visible' 'P1-AC11'
if ($newJob.Count -eq 1) {
    Assert-True ($newJob[0].text -eq 'New Job') "New Job is labelled correctly (got '$($newJob[0].text)')" 'P1-AC11'
}

# The Rig menu retires now that the wizard creates one with the Job. The Job
# menu deliberately stays: it is still the only way to switch between existing
# Jobs (OI-51).
$hiddenMenus2 = @($manifest.menus | Where-Object { -not $_.visible } | ForEach-Object { $_.name })
Assert-True ($hiddenMenus2 -contains 'profileMenu') 'Rig menu is retired' 'P1-AC11'
$jobMenu = @($manifest.menus | Where-Object { $_.name -eq 'sceneCollectionMenu' })
Assert-True ($jobMenu.Count -eq 1 -and $jobMenu[0].visible) `
            'Job menu stays until there is another way to switch Jobs' 'P1-AC11'

# A Job with no metadata reports empty rather than inventing values.
Assert-True ($manifest.jobMetadata.empty -eq $true) `
            'A Job created outside the wizard has no metadata' 'P1-AC11'

# Device enumeration must survive a machine where a backend failed to load --
# DeckLink does exactly that without its drivers, which is the common case on a
# desk and on a CI runner. An empty list is a pass; a crash or a missing key is
# not. The manifest runs it on every dump so this is genuinely exercised.
Assert-True ($null -ne $manifest.captureDevices) `
            "Capture device enumeration ran ($(@($manifest.captureDevices).Count) found)" 'P1-AC11'

# --- Metadata round-trip -----------------------------------------------------
# Read back through the preload callback from a Job file on disk, which is the
# same path a real Job takes and the one Phase 8's manifest will depend on.
Copy-Fixture -Relative 'jobs\job-meta'

$metaManifest = Join-Path (Get-RunDir) 'ui-job-meta.json'
if (Test-Path $metaManifest) { Remove-Item -Force $metaManifest }

$code = Invoke-App -AppArgs @('--collection', 'job-meta', '--dump-ui-manifest', '../../ui-job-meta.json') -TimeoutSec 120
Assert-True ($code -eq 0) 'Manifest dump with the job-meta fixture exited cleanly' 'P1-AC11'

if (Test-Path $metaManifest) {
    $jm = (Get-Content $metaManifest -Raw | ConvertFrom-Json).jobMetadata
    Assert-True ($jm.empty -eq $false) 'Job metadata survives a save/load round-trip' 'P1-AC11'
    Assert-True ($jm.number -eq 'CR-2026-014')          "Job number read back (got '$($jm.number)')" 'P1-AC11'
    Assert-True ($jm.client -eq 'Northern Subsea Ltd')  "Client read back (got '$($jm.client)')" 'P1-AC11'
    Assert-True ($jm.vessel -eq 'MV Aurora')            "Vessel read back (got '$($jm.vessel)')" 'P1-AC11'
    Assert-True ($jm.system -eq 'Work-class ROV 2')     "System read back (got '$($jm.system)')" 'P1-AC11'
    Assert-True ($jm.created -eq '2026-08-13T09:15:00Z') 'Creation timestamp read back' 'P1-AC11'
} else {
    Add-Failure 'No manifest produced for the job-meta fixture' 'P1-AC11'
}

# --- P1-AC12: task 1.9a -- recording-safety UI -------------------------------
$rs = $manifest.recordingSafety
Assert-True ($null -ne $rs) 'Manifest records the recording-safety state' 'P1-AC12'

if ($rs) {
    # The indicator must exist but be *absent* when idle, not merely grey. A
    # badge that is always there and only changes colour is one people stop
    # reading, which defeats the point of having it.
    Assert-True ($rs.indicatorPresent -eq $true) 'Record indicator is installed in the controls dock' 'P1-AC12'
    Assert-True ($rs.indicatorVisible -eq $false) 'Record indicator is hidden while idle' 'P1-AC12'

    # Upstream leaves this unset, which means a mis-click ends a dive silently.
    Assert-True ($rs.warnBeforeStopping -eq $true) 'Confirm-before-stopping is on by default' 'P1-AC12'
    Assert-True ($rs.warnAfterSeconds -eq 60) `
                "Confirmation threshold defaults to 60s (got $($rs.warnAfterSeconds))" 'P1-AC12'

    # Nothing is locked when nothing is recording, and no override is left over
    # from a previous session.
    Assert-True ($rs.recording -eq $false) 'Not recording in a fresh session' 'P1-AC12'
    Assert-True ($rs.editingLocked -eq $false) 'Layers editing is unlocked while idle' 'P1-AC12'
    Assert-True ($rs.overridden -eq $false) 'The editing override does not persist across sessions' 'P1-AC12'
}

# The lock is a flag, so it can be turned off in the field.
$lockFlag = @($manifest.features | Where-Object { $_.key -eq 'LockLayersWhileRecording' })
Assert-True ($lockFlag.Count -eq 1 -and $lockFlag[0].enabled -eq $true) `
            'Layers lock-while-recording is on by default' 'P1-AC12'

# NOT ASSERTED HERE: the behaviour while a recording is actually running --
# indicator visible, edits refused, override releasing them. That needs a live
# recording, which an unattended manifest dump cannot produce. Covered by
# OI-55, and 1.9b forces the issue: testing a disk-full stop needs exactly the
# same harness.

# --- English is the only language offered ------------------------------------
$localeIndex = Join-Path (Get-RunDir) 'data\obs-studio\locale.ini'
if (Test-Path $localeIndex) {
    $langs = @(Select-String -Path $localeIndex -Pattern '^\[' | ForEach-Object { $_.Line })
    Assert-True ($langs.Count -eq 1 -and $langs[0] -eq '[en-US]') `
                "Only en-US is offered as a language (found $($langs.Count))" 'P1-AC7'
} else {
    Add-Skip 'locale.ini not found in run directory' 'P1-AC7'
}

Assert-NoLogErrors -Criterion 'P1-AC7'

Save-ConfigToWorkspace -Workspace $ws
exit (Write-TestSummary -Suite 'T1 Shell and Layers' -Workspace $ws)
