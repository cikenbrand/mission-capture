# Phase 1 — Shell and the Layers tree

**Goal:** Mission Capture stops looking like OBS. One **Layers** tree replaces the Scenes and
Sources docks, the vocabulary becomes Canvas / Element / Job / Rig, and everything an inspection
crew doesn't need is out of sight.

**Prerequisites:** Phase 0 (needs `MCFeatures` and `--dump-ui-manifest`).

**Effort:** 4–6 weeks. **Expect this phase to overrun** — see
[README §5.3](README.md#53-the-layers-tree-is-a-bigger-change-than-it-looks-phase-1--medium-high).

**Read first:** [architecture.md §0](architecture.md#0-naming-before-anything-else) and
[§2](architecture.md#2-the-layers-tree).

---

## Design principles

1. **A pilot should be able to record without reading anything.** Record is the biggest control on
   screen.
2. **Destructive actions need friction; recording actions need none.** Stopping a recording
   mid-dive by mis-click is the worst outcome this app can produce.
3. **Nothing is deleted, only hidden.** Every hidden feature is one `features.ini` line away.
4. **Field diagnosability beats elegance.** Recording state, per-Canvas status, data health, and
   disk remaining must be readable from two metres away.

---

## Tasks

### 1.1 — Terminology
`2 days`

- Introduce the Canvas / Element / Layers / Job / Rig vocabulary in **locale strings only**
  (`frontend/data/locale/en-US.ini`). C++ identifiers keep saying `scene` and `sceneitem` — see
  [architecture.md §0](architecture.md#0-naming-before-anything-else) for why
- Adopt the `renderTarget` naming rule for every `obs_canvas_t` and write it into a short
  `docs/subsea/CODING.md`, so it survives contact with a second developer
- Delete non-English locale files we won't maintain — leaving them means those users see OBS's old
  vocabulary, which is worse than English

**Files:** seam #14

---

### 1.2 — `MCLayersModel`
`8 days` — the core of the phase

A two-level `QAbstractItemModel` over scenes and their items. Full design in
[architecture.md §2.2](architecture.md#22-model-design).

- Canvas rows from the main render target's scenes; Element rows from `obs_scene_enum_items()`
- **libobs stores scene items bottom-first; the tree shows top-first.** Reverse the index in the
  model and assert it in a unit test — getting this backwards is the classic bug in this widget
- Track `source_create` / `source_remove` / `source_rename` globally and
  `item_add` / `item_remove` / `item_reorder` / `item_visible` / `item_locked` per scene. All
  libobs signals fire on arbitrary threads — marshal every one through `QMetaObject::invokeMethod`
- Groups are disabled this phase (only three element types ship), but keep the node structs
  polymorphic so a recursive model stays possible later
- Emit precise `beginInsertRows`/`endInsertRows` rather than `modelReset`, or selection and scroll
  position jump on every change

**Files:** `frontend/subsea/MCLayersModel.{hpp,cpp}` (new)

---

### 1.3 — `MCLayersTree` and delegate
`6 days`

- `QTreeView` with a custom delegate: row icon by element type, inline rename, eye and lock
  toggles, and a per-Canvas recording indicator (populated in Phase 6)
- Interaction table in [architecture.md §2.3](architecture.md#23-interaction) — selection,
  drag-reorder, drag-between-Canvases, double-click behaviour, context menus scoped by node type
- **Dragging an Element to another Canvas** needs an explicit decision surfaced in the UI: move,
  copy sharing the same underlying source, or copy with a duplicated source. OBS's existing
  copy/paste semantics (`frontend/widgets/OBSBasic_Clipboard.cpp`) are the reference
- Keyboard: arrows, Enter to rename, Del to remove, Space to toggle visibility

**Files:** `frontend/subsea/MCLayersTree.{hpp,cpp}`, `MCLayersDelegate.{hpp,cpp}` (new)

---

### 1.4 — Wire the tree in, retire the old docks
`4 days`

- Register Layers as a dock; hide Scenes and Sources behind feature flags rather than deleting them
- **Redirect `OBSBasicPreview`'s selection callbacks** to `MCLayersTree`. Selection can now change
  from the tree, the preview, or a hotkey — build a re-entrancy guard in from the start rather
  than debugging an infinite signal loop later (seam #11, the most merge-fragile one in the plan)
- Drive upstream behaviour through `OBSBasic`'s existing public methods. Do not reimplement
  `AddScene`, `RemoveScene`, or the scene-item helpers — that is what keeps 2459 lines of upstream
  code untouched and mergeable
- Migrate saved dock layouts so an existing config doesn't restore a dead Sources dock

**Files:** seams #7, #9, #10, #11

---

### 1.5 — UI surface audit and hiding
`4 days`

Enumerate every menu action, dock, toolbar button, and settings page; classify
**Keep / Hide / Rework** in `docs/subsea/ui-audit.md`. Starting position, to be confirmed with a
real inspection engineer rather than decided by us:

| Surface | Disposition |
|---|---|
| Layers, Audio Mixer, Controls | **Keep** |
| Scenes dock, Sources dock | **Hide** — replaced by Layers |
| Transitions | **Hide** — cut only |
| Studio Mode, Replay buffer, Virtual camera | **Hide** |
| Stream button and service/OAuth UI | **Hide** until [Phase 9](phase-9-webrtc-streaming.md), then a simplified WHIP panel |
| Multiview, Projectors | **Rework** — fullscreen projector to a second monitor is genuinely useful for a client rep |
| Stats | **Rework** into a "Health" panel: dropped frames, disk space, encoder load, data-link and camera status |
| Filters | **Keep** — colour correction and sharpening are real inspection needs |
| Advanced Audio Properties | **Hide** by default |
| Scripting, Auto-config wizard, What's New, Updater | **Hide** |
| Settings → Stream / Hotkeys / Accessibility / Advanced | **Hide** or heavily trim |

Then implement: extend `MCFeatures::Feature`, implement `apply()` for menus, docks, toolbars, and
settings pages, and **unregister hotkeys for hidden features** — a hidden replay buffer that still
answers its hotkey is a field bug waiting to happen.

---

### 1.6 — Element type restriction
`2 days`

Only **Video Capture Device**, **RTSP Camera**, and **Overlay** appear in the Add Element menu.
Everything else stays registered — so existing Jobs still load — but hidden behind
`MCFeatures::AllSourceTypes`.

The picker itself is [Phase 2](phase-2-video-elements.md) task 2.5; this task is the filter and the
flag.

---

### 1.7 — Inspection-appropriate defaults
`2 days`

A Rig template applied on first run:

- **Recording format: MKV.** Confirmed as the primary container. It survives a power loss or a hard
  kill mid-dive with a playable file, which plain MP4 does not. **No automatic remux** — offer it as
  an explicit action, never as a silent post-step that can fail unattended
- **Encoder:** NVENC or AMF depending on the machine, quality-targeted (CQP/CRF) rather than
  bitrate-targeted. Detect at first run and pick; don't make the operator choose
- **Resolution/FPS:** match the capture source rather than forcing 1920×1080 — inspection cameras
  are frequently not 16:9
- **Filename template:** `%JOB%_%CANVAS%_%CCYY%MM%DD_%hh%mm%ss`
  — **corrected 2026-08-13.** This originally read `…%CCYY%%MM%%DD%_%hh%%mm%%ss%`, which is wrong:
  libobs' date tokens have no trailing delimiter and `%%` is an escaped percent, so that form
  produced `2026%MM%DD`. `%JOB%` and `%CANVAS%` *are* closed with a second `%` — they are ours and
  substituted before libobs sees the string. Shipped broken in 1.7 and found in 1.9b by recording
  a real file
- **Auto-split** on by default ([Phase 6](phase-6-multi-record.md) task 6.8)
- **Audio:** one mic/comms channel, no desktop audio
- Never auto-remux to a lossy container

---

### 1.8 — New Job wizard
`4 days`

Replaces OBS's auto-configuration wizard. On first run and from `File → New Job`:

1. Job / project number, client, vessel, ROV or dive system
2. Recording destination with a live free-space readout
3. Camera setup: detect capture devices, name them ("Pilot Cam", "Manip Cam", "Sonar"), create one
   **Canvas** per camera *(element creation lands in Phase 2)*
4. Optionally assign an Overlay Template *(wired in Phase 4)*
5. Optionally configure a data device *(wired in Phase 5)*

Creates a Job (scene collection) named after the job and stamps the metadata into it — which is
what [Phase 8](phase-8-sidecar-log.md)'s manifest reads.

**Files:** `frontend/subsea/MCJobWizard.{hpp,cpp}` (new), seam #13

---

### 1.9 — Recording safety

**Split into three on 2026-08-13**, after 1.5–1.8 added two concerns the original 2-day estimate
never covered. The three are separated by *risk*, not by size: 1.9a cannot corrupt a recording,
1.9b can, and 1.9c is blocked on a decision rather than on code. Bundled together, the one that
can lose footage would have been the one that got rushed.

---

#### 1.9a — Recording-safety UI
`2 days`

- Large, unmistakable record indicator with elapsed time
- Confirm-on-stop when a recording has run longer than a configurable duration (default 60 s)
- A "locked" mode that disables Layers editing while recording, toggled deliberately

All frontend, all reversible, and all observable through `--dump-ui-manifest` the way 1.5–1.8 were.

---

#### 1.9b — Disk-space protection
`3 days` — the task in this phase that can destroy a recording if it is wrong

- A free-space field in the status bar. **Raised in the [UI audit](ui-audit.md) as a gap**: filling
  a disk mid-dive is the most predictable way to lose footage, and nothing currently shows it
- Warnings at 10 GB and 2 GB
- A hard stop with a **clean file close** before the disk fills

The last point is why this is its own task. It reaches into the output pipeline rather than the UI,
and "stop cleanly at the edge of a full disk" is precisely the path that, done wrong, produces the
unplayable file the MKV default in [1.7](#17--inspection-appropriate-defaults) exists to prevent.

**Needs a test fixture that does not involve filling a real disk** — most likely a small
virtual disk or a configurable threshold the test can move, decided when the task starts.

---

#### 1.9c — Unattended startup
`1 day`, and **blocked on a product decision**, not on code

Two dialogs currently wait forever for an operator who may not be there:

- **OI-46** — a dirty shutdown leaves a `.sentinel/run_*` file, and the next launch opens a modal
  crash prompt
- **OI-54** — first run opens the New Job wizard, added in [1.8](#18--new-job-wizard)

Both are correct for someone sitting at the machine, and both are wrong for a vessel PC that is
auto-started, freshly imaged, or recovering from a power cut. They are one decision, not two:
**what should this app do when it starts with nobody watching?**

Options, to be chosen rather than guessed:

| Approach | Trade-off |
|---|---|
| A command-line switch (`--unattended`) | Explicit and testable, but only helps where someone configured the shortcut |
| Timeout that picks the safe default | Works with no configuration; risks dismissing a crash prompt someone wanted to read |
| Suppress when not launched by hand | Correct automatically, but "was this auto-started?" is a guess on Windows |

CI already exercises this path — hosted runners start from a clean config every time — so whatever
is chosen can be asserted in T0.

---

## Acceptance criteria

- [ ] `P1-AC1` One Layers panel shows Canvases with their Elements nested, both levels visible at once
- [ ] `P1-AC2` Element order in the tree matches Z-order in the preview (top row = drawn last)
- [ ] `P1-AC3` Selecting a Canvas makes it program; selecting an Element selects it in the preview
- [ ] `P1-AC4` Drag-reorder within a Canvas and drag-move between Canvases both work and persist
- [ ] `P1-AC5` Selection never loops between tree and preview
- [ ] `P1-AC6` Renaming, adding, and removing — from the tree or an external trigger — updates the tree
      without a full reset or a scroll jump
- [ ] `P1-AC7` The UI says Canvas / Element / Layers / Job / Rig everywhere; no "Scene" or "Source" visible
- [ ] `P1-AC8` The UI manifest matches the signed-off audit table exactly
- [ ] `P1-AC9` No hidden feature is reachable by hotkey, menu, dock menu, or restored dock layout
- [ ] `P1-AC10` Only three element types offered; others restorable by flag
- [ ] `P1-AC11` A fresh install goes from launch to recording in under 60 seconds with no settings visits
- [ ] `P1-AC12` Killing the app mid-recording leaves a playable file
- [ ] `P1-AC13` No upstream `.cpp` had logic rewritten — only the documented seams touched

---

## Tests

### Unit — `test/cmocka/test_mc_layers_model.c`

The model is pure logic and deserves real unit tests, run against a headless libobs:

| Test | Asserts |
|---|---|
| `test_row_ordering` | Element row 0 is the **topmost** item; libobs index 0 is the **bottom** |
| `test_insert_signals` | Adding an item emits `beginInsertRows` at the right row, not a reset |
| `test_reorder` | Reordering N items produces the reverse mapping in the tree |
| `test_remove_selected` | Removing the selected Element leaves a valid selection |
| `test_canvas_removal` | Removing a Canvas removes its Element rows with no dangling pointers |

### Integration — `tools/subsea-tests/t1-shell.ps1`

Full text in [testing.md §T1](testing.md#t1--shell-and-layers). Asserts:

1. **Layers structure** — load a fixture Job with 3 Canvases × 2 Elements; dump the tree via the
   websocket vendor API and assert exactly that shape and order
2. **Z-order agreement** — reorder Elements via the API, screenshot, assert the drawn order matches
   the tree order
3. **UI manifest golden diff** — compare against `fixtures/golden/ui-manifest.golden.json`; drift
   fails and must be consciously re-blessed
4. **Terminology sweep** — scan the manifest and all visible strings for "Scene", "Source",
   "Profile", "Scene Collection"; fail on any hit
5. **Hotkey leakage** — assert no hidden feature holds a binding
6. **Defaults** — fresh portable config, then assert `basic.ini` has the expected container,
   encoder mode, split settings, and filename template
7. **Crash recovery** — start recording, hard-kill, relaunch, `ffprobe` the orphaned file, assert
   it is playable with a plausible duration. *Highest-value test in the phase*
8. **Feature restoration** — for each hidden feature, enable it via `features.ini`, launch, assert
   it appears and doesn't crash on activation
9. **Disk guard** — point recording at a small virtual disk, fill it, assert a clean stop with a
   playable file
