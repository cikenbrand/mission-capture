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
