# UI surface audit

**Task:** [1.5](phase-1-shell-and-layers.md#15--ui-surface-audit-and-hiding).
**Status:** proposal — **awaiting review**. Nothing here is implemented yet.

Every menu action, dock, toolbar button, status-bar field and settings page in the application,
classified **Keep / Hide / Rework**. The phase doc says these dispositions are "to be confirmed with
a real inspection engineer rather than decided by us", so treat every row as a proposal with a
reason attached, not a decision already taken.

## How to read a disposition

| | Meaning |
|---|---|
| **Keep** | Visible, unchanged. |
| **Hide** | Behind a `MCFeatures` flag, default off. Recoverable by editing `features.ini` — nothing is deleted, so a wrong call here costs a config edit, not a rebuild. |
| **Rework** | Stays, but not in its current form. Each names the phase that owns the replacement. Rework items are **not** hidden by 1.5 unless the row says so. |

## How the inventory was produced

Two sources, both re-runnable, so this document can be regenerated at the next upstream merge rather
than re-read by eye:

- `MissionCapture64.exe --dump-ui-manifest` — every `QAction`, `QDockWidget` and `QMenu` reachable
  from the main window at runtime, plus registered libobs hotkeys and current flag state
- Parsing `frontend/forms/OBSBasic.ui`, `OBSBasicControls.ui`, `StatusBarWidget.ui` and
  `OBSBasicSettings.ui` for menu *structure* and ordering, and for the settings dialog, which the
  manifest cannot see (it is a separate dialog built on demand, not a child of the main window)

**Manifest state at the time of writing** (fresh dump, 2026-08-13): 82 visible actions, 20 hidden by
flags, 7 docks, 30 registered hotkeys of which 6 are leaked. Leak #1 below is confirmed against that
dump; #2, #3 and #4 are established from the code paths cited — #2 provably cannot be seen in a dump
at all, which is the point it makes.

---

## What this audit found that changes the work

The classification below was the expected half of the task. The other half is that **the 0.4 hiding
mechanism is not sufficient to enforce it**. Four ways a "hidden" feature is still reachable today —
the first is the known OI-23, the other three are new:

### 1. Hotkeys outlive their UI — OI-23

Six libobs hotkeys stay registered for features whose buttons are already hidden:
`OBSBasic.StartStreaming`, `StopStreaming`, `ForceStopStreaming`, `StartReplayBuffer`,
`StopReplayBuffer`, `Transition`. T0 reports this as a SKIP on every run.

These are libobs hotkeys, not `QAction` shortcuts, so hiding the widget does nothing. A stray
keypress can start a stream mid-dive.

**Fix:** `obs_enum_hotkeys` to collect the ids of hotkeys belonging to disabled features, then
`obs_hotkey_unregister` each. Two details matter:

- Collect first, unregister after. `obs_enum_hotkeys` holds the hotkey mutex for the whole
  iteration (`libobs/obs-hotkey.c:obs_enum_hotkeys`), so unregistering inside the callback would
  deadlock.
- Half of a registered *pair* can be unregistered on its own. `obs_hotkey_pair_load` and
  `obs_hotkey_pair_save` both null-check each half before touching it, so the leftover pair record
  is inert until shutdown. This matters because it means we can work **by name**, and never need
  access to `OBSBasic`'s private `streamingHotkeys` / `replayBufHotkeys` members — no upstream file
  is touched, the same result as task 1.4.

### 2. Hidden docks can be switched back on from the Docks menu

`SETUP_DOCK` adds each dock's `toggleViewAction()` to **View ▸ Docks**
(`frontend/widgets/OBSBasic.cpp:549`). Hiding `scenesDock` leaves a live menu entry that puts it
straight back — and the retired Scenes and Sources docks are exactly the ones an operator must not
be able to resurrect, because Layers is now the only thing that keeps its model in sync.

Worse, `setupDockAction` installs an `enabledChanged` handler that forces the action back to enabled
(`frontend/widgets/OBSBasic_Docks.cpp:28`), so disabling it does not stick. The toggle must be made
**invisible**, not disabled.

These toggle actions are created by Qt and carry no `objectName`, so they are invisible to the
manifest and to the current `objectNames`-based flag table — **which is why 0.4's verification pass
reported "30 hidden / 0 missing" and still missed this.** They must be reached through the dock they
belong to, and given an `objectName` on the way so T1 can assert the fix holds.

### 3. Settings pages are out of reach entirely

`MCFeatures::apply()` only walks children of the main window. The settings dialog is separate and
built on demand, so **no settings page can currently be hidden** — Stream settings, with its OAuth
and stream-key UI, is fully reachable today regardless of `StreamingUI` being off.

**Fix:** an `apply()` overload taking the settings dialog, called from its constructor. This is the
one place 1.5 must touch an upstream file, and it is a single call.

### 4. A menu whose every item is hidden still shows as an empty menu

Hiding all of Help's children leaves a "Help" menu that opens onto nothing. Cosmetic, but it looks
broken. `apply()` should hide a menu whose visible action count reaches zero, after all other hiding
is done.

---

## Menu bar

Top-level order is File, Edit, View, Docks, Profile, Scene Collection, Tools, Help.

### File

| Action | Disposition | Reason |
|---|---|---|
| `actionShow_Recordings` | **Keep** | Getting to the footage is the core job. |
| `actionRemux` | **Keep** | `.mkv` is our primary format precisely because it survives a crash; remux to `.mp4` is the standard delivery step. Phase 8 may absorb it into the deliverable flow. |
| `action_Settings` | **Keep** | |
| `actionShowSettingsFolder`, `actionShowProfileFolder` | **Keep** | Support asks for these constantly. |
| `actionE_xit` | **Keep** | Phase 1.9 adds a recording-in-progress guard. |

### Edit

| Action | Disposition | Reason |
|---|---|---|
| `actionMainUndo` / `actionMainRedo` | **Keep** | |
| Transform submenu (17 actions) | **Keep** | Fitting a camera to a region is routine overlay layout work. |
| Order submenu (`actionMoveUp`/`Down`/`ToTop`/`ToBottom`) | **Rework → 1.5** | Keep the actions, **move them under Layers**. Ordering is now a Layers-tree concept; leaving it under Edit splits one idea across two places. Drag-drop in the tree already does this — these stay for keyboard users. |
| `actionCopySource`, `actionPasteRef`, `actionPasteDup` | **Keep** | Duplicating a configured camera across Canvases is real work. |
| `actionCopyFilters`, `actionPasteFilters` | **Keep** | Follows Filters being kept. |
| `actionInteract` | **Hide** | Browser-source interaction. `obs-browser` is not built in this fork. |
| `actionSourceProperties`, `actionSceneFilters` | **Keep** | |
| `actionLockPreview` | **Keep** | Genuinely useful once a layout is final. |

### View

| Action | Disposition | Reason |
|---|---|---|
| `actionSceneListMode`, `actionSceneGridMode` | **Hide** | Both configure the Scenes dock, which no longer exists. Dead controls. |
| `actionFullscreenInterface` | **Keep** | |
| `toggleContextBar` | **Keep** | Already flagged `SourceToolbar`, default on. |
| Multiview entries | **Rework → later** | See Projectors below. |

### Docks

| Action | Disposition | Reason |
|---|---|---|
| `lockDocks` | **Keep** | Stops an operator dragging the layout apart mid-dive. Consider defaulting **on** in 1.7. |
| `sideDocks` | **Keep** | |
| `resetDocks` | **Keep** | The recovery path when a layout is wrecked. |
| Per-dock toggles | **Rework → 1.5** | Auto-generated; must follow their dock's visibility. See leak #2. |

### Profile and Scene Collection

Both menus are **Rework → 1.8**, and this is the biggest structural call in the audit.

"Profile" (encoder and output settings) and "Scene Collection" (the Canvases) are two separate
concepts an operator must keep in step by hand. For us they are one thing — a **Job** — and 1.8
builds the New Job wizard on that premise.

Proposal: **hide both menus in 1.5**, and let 1.8 introduce a single Job menu. The risk of hiding
first is a gap where neither exists; the alternative is shipping the confusion we are trying to
remove. Flagged for your call.

Import/export of both is already hidden (`SceneCollectionImportExport`), as is
`actionRemigrateSceneCollection` — a migration path for OBS 27-era collections that cannot exist
here. `actionShowMissingFiles` should be **kept** and re-parented to the Job menu: a Job that
references a moved file is a real failure and needs a visible answer.

### Tools

| Action | Disposition | Reason |
|---|---|---|
| `autoConfigure` | **Hide** — already flagged | Replaced by New Job (1.8). Tunes for Twitch bitrate; wrong on every axis for us. |
| `idianPlayground` | **Hide** — already flagged | Upstream widget gallery. |
| `actionOpenPluginManager` | **Hide** | We ship a fixed plugin set. A user disabling `win-dshow` produces an unexplainable support call. |
| Scripting | **Hide** | Not built in this fork; listed for completeness. |

### Help

| Action | Disposition | Reason |
|---|---|---|
| `actionShowLogs`, `actionViewCurrentLog`, `actionShowCrashLogs` | **Keep** | First thing support asks for. |
| `actionUploadCurrentLog`, `actionUploadLastLog`, `actionUploadLastCrashLog` | **Hide** — already flagged | Uploads to the OBS Project's servers. Sends vessel data to a third party — a compliance problem, not just a branding one. **Phase 8 should replace this with a local "export support bundle".** |
| `actionHelpPortal`, `actionWebsite`, `actionDiscord` | **Hide** — already flagged | OBS Project destinations. |
| `actionCheckForUpdates`, `actionReleaseNotes`, `actionRepair`, `actionRestartSafe` | **Hide** — already flagged | Points at OBS's updater. `actionRestartSafe` (safe mode, third-party plugins off) is worth **reworking back in** later — it is a genuine recovery tool, it just needs to stop being an OBS update path. |
| `actionShowWhatsNew` | **Hide** | Fetches OBS release notes over the network. |
| `actionShowAbout` | **Rework → 1.5** | Must state Mission Capture, Cyberian Resources, the GPLv2 offer and OBS attribution. `THIRD_PARTY_NOTICES.md` already has the text; the dialog does not show it. **Licence compliance, not polish.** |
| `actionShowMacPermissions` | **Hide** | macOS-only; dead on Windows. |

---

## Docks

| Dock | Disposition | Reason |
|---|---|---|
| `layersDock` | **Keep** | Ours. The centre of the UI. |
| `mixerDock` | **Keep** | Audio levels matter; divers narrate. |
| `controlsDock` | **Keep** | See button table below. |
| `scenesDock`, `sourcesDock` | **Hide** — already flagged, retired in 1.4 | Add toggle suppression (leak #2). |
| `transitionsDock` | **Hide** — already flagged | Inspection work cuts. |
| `statsDock` | **Rework → 2.6** | Hidden now, becomes the Health panel. See below. |
| Extra browser docks | **Hide** | `obs-browser` is not built. |

### Stats → Health

Upstream Stats reports CPU, disk space, memory, FPS, and per-output dropped frames and bitrate. All
of that is worth keeping. What it lacks is everything that answers *"is the dive being recorded
properly right now?"* — per-camera signal state, per-Element received frame rate, data-link status,
per-recording state once Phase 6 lands.

Phase 2.6 already owns per-Element health. Building a Health panel now means building it twice, so:
**keep Stats hidden through Phase 1, and let 2.6 grow the real panel.** Recorded here so the
decision is deliberate rather than an omission.

### Projectors and Multiview

**Keep, unchanged, in Phase 1.** A fullscreen projector to a second monitor for a client rep is one
of the genuinely good things OBS does and it works today. Multiview is a plausible fit for watching
several cameras at once, but that overlaps whatever Phase 6 does for multi-recording, so it should
be judged then rather than reworked speculatively now.

---

## Controls dock

| Button | Disposition | Reason |
|---|---|---|
| `recordButton` | **Keep** | 1.9 adds the safety affordances. |
| `pauseRecordButton` | **Keep** | Though see the note below. |
| `settingsButton` | **Keep** | |
| `streamButton`, `broadcastButton` | **Hide** — already flagged | Returns in Phase 9 as a WHIP panel. |
| `replayBufferButton`, `saveReplayButton` | **Hide** — already flagged | Superseded by Phase 7 secondary recording, which is the same idea done properly for our workflow. |
| `virtualCamButton`, `virtualCamConfigButton` | **Hide** — already flagged | |
| `modeSwitch` (Studio Mode) | **Hide** — already flagged | |

**Question for 1.9, flagged here:** pause writes a discontinuity into the recording. For an
inspection record that is later used as evidence, a paused gap may be unacceptable — or may be
exactly how snippets are handled today. Worth your input when we reach 1.9.

---

## Status bar

| Field | Disposition | Reason |
|---|---|---|
| `recordTime`, `recordIcon` | **Keep** | |
| `droppedFrames`, `fpsCurrent`, `cpuUsage` | **Keep** | Cheap, always-visible health. |
| `kbps` | **Keep** | |
| `streamTime`, `streamIcon`, `streamFrame` | **Hide** | Follows `StreamingUI`. Returns in Phase 9. |
| `delayFrame`, `delayInfo` | **Hide** | Stream delay buffer only. |

**Gap:** no free-disk-space indicator. Filling a disk mid-dive destroys the recording, and it is the
single most predictable failure on a long job. Proposed as a new status-bar field — **raised here,
implemented in 1.9** with the other recording-safety work.

---

## Settings pages

Currently unreachable by the flag system; see leak #3.

| Page | Disposition | Reason |
|---|---|---|
| General | **Rework → 1.7** | Trim to what applies. Several entries concern Studio Mode and projectors. |
| Appearance | **Keep** | Theme choice is real: a dark theme in a dark ROV shack, a light one on deck. |
| **Stream** | **Hide** | Service list, OAuth, stream keys — all OBS-ecosystem. Phase 9 replaces it with a WHIP panel. |
| Output | **Rework → 1.7** | Keep it, but default to sane inspection values and hide the streaming half. The encoder work in 6.x owns the rest. |
| Audio | **Keep** | |
| Video | **Rework → 1.7** | Base/output resolution and FPS are load-bearing. Downscale filters and FPS-as-fraction are not. |
| Hotkeys | **Rework → 1.5** | Do **not** hide — hotkeys are how a busy operator works. But the page currently lists hotkeys for hidden features, so it must be filtered alongside the unregistration in leak #1. Fixing #1 largely fixes this for free, since the page enumerates registered hotkeys. |
| Accessibility | **Keep** | Colour-blind-safe selection colours are a genuine need, and it costs nothing. |
| Advanced | **Rework → 1.7** | Contains the recording filename formatting we need, alongside browser-source and stream-delay settings we do not. |

---

## Proposed new feature flags

Extending `MCFeatures::Feature`, all default off unless noted:

| Flag | Covers |
|---|---|
| `SceneViewModes` | `actionSceneListMode`, `actionSceneGridMode` |
| `BrowserInteraction` | `actionInteract` |
| `PluginManager` | `actionOpenPluginManager` |
| `WhatsNew` | `actionShowWhatsNew` |
| `MacPermissions` | `actionShowMacPermissions` |
| `ProfileMenu` | Profile menu — pending your call above |
| `SceneCollectionMenu` | Scene Collection menu — pending your call above |
| `StreamSettingsPage` | Stream settings page |
| `StreamStatusBar` | `streamFrame`, `delayFrame` |
| `DockToggles` | *Mechanism, not a surface:* suppress `toggleViewAction` for hidden docks |

---

## Implementation plan for the rest of 1.5

1. Extend `MCFeatures` with the flags above — table row plus enum value each, no new mechanism
2. Dock toggle suppression: after hiding a dock, hide its `toggleViewAction()`
3. Hotkey unregistration for disabled features — closes **OI-23**, flips T0's SKIP to a PASS
4. `apply(OBSBasicSettings *)` overload for settings pages — the one upstream call site
5. Empty-menu collapse after all other hiding
6. Re-parent the Order actions under Layers
7. About dialog rebranding and the GPLv2 offer — licence compliance
8. Regenerate the manifest; extend T1 to assert the new hidden set and the empty hotkey leak list

Items 3 and 7 are the ones with consequences beyond tidiness. The rest is surface.

---

## Needs your decision

1. **Profile and Scene Collection menus** — hide in 1.5 and let 1.8 introduce the Job menu, or leave
   them visible until 1.8 is actually built? Hiding first is cleaner but leaves a gap.
2. **Anything above marked Keep that you know your operators never touch** — every Keep is a guess
   made from the outside.
3. **Anything Hidden that you actually need.** Cheapest possible correction: say so, and it is one
   row in a table.
