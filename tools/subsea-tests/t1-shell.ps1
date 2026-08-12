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

    # Exactly one Canvas is the program Canvas.
    $program = @($canvases | Where-Object { $_.program })
    Assert-True ($program.Count -eq 1 -and $program[0].name -eq 'Camera 1') `
                "Camera 1 is the program Canvas (found $($program.Count) marked)" 'P1-AC3'
} else {
    Add-Failure 'No layers manifest produced' 'P1-AC1'
}

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
