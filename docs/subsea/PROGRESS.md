# Progress tracker

**The single source of truth for what is done, what is not, and what is in the way.**

Phase docs are the *specification* and never carry status. Status lives here and only here, so
there is nothing to keep in sync.

---

## How this is maintained

**At the end of every task**, three things happen without being asked:

1. **Mark the task** — status → `done`, fill in the date, link the evidence.
2. **Note anything non-obvious** — what changed from the plan, what was cut, what surprised us.
   A blank Notes cell means "went as written", which is genuinely informative.
3. **Update the open-items register** — log anything discovered that isn't being fixed now (a bug,
   a deferred piece, an unmet dependency, a question for you), and close any item this task
   resolved.

### Definition of done

A task is `done` only when all of these hold. Anything short of it stays `in progress`.

- Code is written, builds clean, and is committed on a `feature/mc-*` branch
- Its tests exist and pass, with a run report as evidence
- Acceptance criteria it touches are tagged with `-Criterion` IDs
      (see [testing.md § Run reports](testing.md#run-reports))
- Anything left behind is in the open-items register below — not in someone's head

### Status vocabulary

| Status | Meaning |
|---|---|
| `todo` | Not started |
| `wip` | In progress |
| `blocked` | Cannot proceed — must have a linked open item saying why |
| `done` | Meets the definition above |
| `cut` | Deliberately dropped — Notes must say why |

---

## Summary

| Phase | Title | Tasks | Done | Status |
|---|---|---|---|---|
| [0](phase-0-foundation.md) | Foundation | 7 | **7** | **`done`** |
| [1](phase-1-shell-and-layers.md) | Shell & Layers tree | 9 | **4** | `wip` |
| [2](phase-2-video-elements.md) | Video elements | 6 | 0 | `todo` |
| [3](phase-3-data-core.md) | Data core | 7 | 0 | `todo` |
| [4](phase-4-overlay-editor.md) | Overlay editor | 8 | 0 | `todo` |
| [5](phase-5-transports.md) | Transports & config UI | 6 | 0 | `todo` |
| [6](phase-6-multi-record.md) | Multi-canvas recording | 8 | 0 | `todo` |
| [7](phase-7-secondary-capture.md) | Secondary capture | 11 | 0 | `todo` |
| [8](phase-8-sidecar-log.md) | Sidecar log & hardening | 8 | 0 | `todo` |
| [9](phase-9-webrtc-streaming.md) | WebRTC streaming | 6 | 0 | `todo` |
| | **Total** | **76** | **11** | |

**Acceptance criteria met:** 9 / 113 — P0-AC1..AC5, evidence in the T0 run report.

---

## Open items

Everything discovered but not resolved. **This table is the point of the tracker** — a task list
tells you what's left to build, this tells you what's waiting to bite.

Types: `bug` · `deferred` · `dependency` · `question` · `risk` · `debt`

| ID | Raised in | Type | Sev | Item | Blocks | Status |
|---|---|---|---|---|---|---|
| OI-1 | planning | question | med | Clip preroll default — how many seconds before the button press should a clip include? Sizes the packet ring's memory budget | 7.1 | open — ask at Phase 7 |
| OI-2 | planning | question | high | Client deliverable format — required CSV columns, header naming, timestamp format, filename convention, any IMCA/in-house spec | 8.2 | open — ask at Phase 8 |
| OI-3 | planning | question | high | Event marking scope — vocabulary fixed or free-text, own row/column/file, should the hotkey also fire a clip, append-only or editable | 8.6, 8.2 | open — ask at Phase 8 |
| OI-4 | planning | question | med | Real channel count and data rate, and the RS-232 line settings actually in use | 3.1, 5.1 | open — ask at Phase 3, again at Phase 5 |
| OI-5 | planning | dependency | high | Encoder limits on the **real target machine** are still unmeasured. 0.7 measured the dev laptop instead: no session cap up to 12, throughput-bound at 8 (NVENC 1080p30) / 3 (NVENC 1080p60) / 12+ (AMF 1080p30). Useful, but a laptop | 6.6 | **open — re-run `bench-encoders.ps1` on the topside PC** |
| OI-6 | planning | dependency | med | Capture-hardware inventory — which DeckLink and AVerMedia models, plus "there may be other capture cards as well" | 2.1, 2.2 | **open — no capture hardware connected during 0.7.** Note DeckLink needs Blackmagic Desktop Video installed; UVC/DirectShow devices do not |
| OI-7 | planning | risk | med | Audio-encoder fan-out across simultaneously-started outputs is inferred from `obs_encoder_add_output`'s DARRAY, not yet proven. Fallback is one audio encoder per recorder | 6.2 | open — verify early in 6.2 |
| OI-8 | planning | risk | med | An `EPHEMERAL \| ACTIVATE` private render target keeping its sources active while not the program Canvas is the premise of the whole recording feature, and is unverified | 6.2 | open — verify early in 6.2 |
| OI-9 | planning | debt | low | Acceptance criteria need `-Criterion` IDs stamped per phase as its tests are written | — | open — rolling; **Phase 0 and Phase 1 done** |
| OI-10 | planning | deferred | low | Phase completion reports declined in favour of run reports only. Run reports cover verification but record nothing about mid-phase cuts, deferrals, or plan deviations — that gap is now covered by this tracker's Notes column | — | accepted |
| OI-11 | planning | deferred | low | Clean (overlay-free) *video* copy is out of scope. Clean *stills* are in, via Phase 7 snapshots | — | closed by decision |
| OI-12 | 0.1 | dependency | high | `origin` pointed at `obsproject/obs-studio` | 0.1 | ✅ **closed 2026-08-12** — repointed to `cikenbrand/mission-capture`, stale refs pruned |
| OI-13 | 0.1 | question | low | Monthly upstream-merge reminder | 0.1 | ✅ **closed 2026-08-12** — scheduled task `mission-capture-upstream-merge`, 09:00 on the 1st monthly |
| OI-14 | 0.1 | debt | low | `docs/subsea/` untracked and uncommitted | — | ✅ **closed 2026-08-12** — commit `2d5d1942c` |
| OI-15 | 0.1 | question | med | First push not yet authorised | OI-14 | ✅ **closed 2026-08-12** — `master` and `develop` pushed, tracking set |
| OI-16 | 0.1 | debt | low | Suspected line-ending problem from git`s LF→CRLF warnings | — | ✅ **closed 2026-08-12 — not a problem.** `git ls-files --eol` shows `i/lf` for every file, ours and upstream`s: `* text=auto` is normalising correctly and the warning is purely informational. My original concern was wrong, and fixing it by renormalising would have rewritten thousands of upstream files for nothing |
| OI-17 | 0.2 | dependency | **high** | **Logo not designed yet.** `frontend/cmake/windows/mission-capture.ico` is a deliberately plain placeholder ("MC" on subsea blue with an amber bar to mark it provisional). Must be replaced before any external release — a placeholder icon on a client's vessel PC looks unfinished | first release | **open — waiting on you** |
| OI-18 | 0.2 | dependency | med | Visual Studio ATL component missing, breaking `win-dshow` (and others) | 0.3 | ✅ **closed 2026-08-12** — root cause was CMake selecting the ATL-less **Build Tools** install when **Community** (which has ATL) was also present. Fixed by pinning `CMAKE_GENERATOR_INSTANCE` in the preset; no install needed. My earlier note that disabling plugins would resolve it was wrong — `win-dshow` is load-bearing and cannot be disabled |
| OI-19 | 0.2 | bug | med | `CMakePresets.json` requires generator "Visual Studio 18 2026"; this machine has VS 2022 | 0.3 | ✅ **closed 2026-08-12** — `windows-subsea-x64` pins "Visual Studio 17 2022". Upstream presets left untouched |
| OI-20 | 0.2 | deferred | med | **There is no installer in this repo** — OBS's Windows installer lives in a separate project, and CPack here only produces a ZIP. Task 0.2's "installer: name, upgrade GUID, Start-menu entry" could not be done because there is nothing to rebrand. An installer is genuinely new work and needs its own task before release | release | open — needs a plan decision |
| OI-21 | 0.2 | debt | low | Updater / WhatsNew / service-list endpoints pointing at obsproject.com | 0.3, 1.5 | **partly closed 2026-08-12** — updater.exe, WhatsNew, service and compat updates are now build-disabled. The in-app update *check* in `AutoUpdateThread.cpp` still compiles; runtime-disabled only. Full removal in 1.5 |
| OI-22 | 0.2 | debt | low | Submodules were uninitialised on this clone; `git submodule update --init --recursive` is required after any fresh clone. Belongs in the build documentation | 0.5 | ✅ **closed 2026-08-12** — documented in BUILDING.md |
| OI-23 | 0.4 | debt | **med** | **Hidden features keep their hotkeys — now measured.** Corrected in 0.5: the leak is *not* in `QAction` shortcuts (the manifest shows **0** hidden actions hold one) but in **libobs hotkeys**, which `setVisible(false)` cannot touch. Six are live for hidden features: `StartStreaming`, `StopStreaming`, `ForceStopStreaming`, `StartReplayBuffer`, `StopReplayBuffer`, `Transition`. T0 records them as a SKIP against `P1-AC9`, so the report will show the leak closing | 1.5 | open — evidence captured |
| OI-24 | 0.4 | bug | low | Upstream logs `Failed to rename basic scene collection file` on **every fresh config** — the first-run migration renames `basic/scenes.json` without checking it exists. Cosmetic, but it pollutes error-scanning tests, so it is allow-listed in `testing.md`. Left unpatched to avoid touching an upstream file for no functional gain; candidate for an upstream PR | — | open |
| OI-25 | 0.4 | debt | low | `decklink-captions` and `decklink-output-ui` fail to load their `en-US` locale at startup. Both are peripheral plugins that ride along with `ENABLE_DECKLINK` and neither is used; worth disabling outright if a switch can be added | 2.1 | open |
| OI-26 | 0.5 | bug | low | `--dump-ui-manifest` returns early from `OBSBasic::OBSInit`, so the crash handler never records a sentinel location and logs an error at shutdown. Confirmed **absent from normal runs**, so it is an artifact of the test-only flag, not a product defect. Allow-listed in `common.ps1`; not root-caused | — | open |
| OI-27 | 0.5 | dependency | low | `ENABLE_UNIT_TESTS` is on in the preset, so every build compiles the cmocka suite. Negligible now (4 small tests) but worth a separate CI-only preset if it grows | 0.6 | open |
| OI-28 | 0.6 | risk | **med** | **CI has never actually run.** `gh` is not installed here, so `mission-capture.yaml` is validated only by YAML parse and local preset configure. First push to GitHub is the real test; expect to iterate | — | **open — needs a run** |
| OI-29 | 0.6 | risk | med | The T0 smoke job is `continue-on-error: true`. Hosted runners have no GPU and fall back to WARP software D3D11; whether OBS initialises there is unknown. Left non-blocking so it reports rather than reddening CI. Make it required once a few runs prove it stable | — | open |
| OI-30 | 0.7 | bug | low | GPU utilisation reads 0 throughout the AMF runs — the `GPU Engine(*engtype_VideoEncode)` counter evidently exposes the AMD encode engine under a different instance name. The figure is missing, not zero. Fix before the real-machine run | 0.7 rerun | open |
| OI-31 | 0.7 | risk | med | The benchmark measures **encode only** — no capture, compositing, overlay rendering or muxing running alongside, and 10-second runs so no thermal throttling. Real recording will have materially less headroom than these figures suggest | 6.6, 8.8 | open |
| OI-32 | 1.1 | bug | **high** | **The test harness was never committed.** Root `.gitignore` is an allowlist (`/*` then `!/dir`) and `tools/` was not on it, so everything added under `tools/subsea-tests` in 0.5–0.7 was invisible to git — `git status` reported a clean tree throughout. `THIRD_PARTY_NOTICES.md` (the GPLv2 source offer) was ignored the same way. CI would have failed on its first run. Both allowlisted in commit `4fe7e82d1` | — | ✅ **closed 2026-08-13** |
| OI-33 | 1.2 | debt | med | Canvas **display** order now mirrors upstream`s Scenes list, so it matches what `scene_order` persists. Canvas **drag-reorder** is still unimplemented — it would mean reordering that hidden widget | later | **partly closed 2026-08-13** — display correct, reorder outstanding |
| OI-34 | 1.2 | debt | low | Element add/remove refreshes a Canvas`s whole child block rather than the single affected row, because the signal does not say which position changed. Correct and scoped to one Canvas, but a diff would preserve selection better. Revisit if it feels wrong in the real widget | 1.3 | open |
| OI-35 | 1.3 | debt | med | Remove was inert | 1.4 | ✅ **closed 2026-08-13** — Del and the context menu trigger upstream`s `actionRemoveScene`/`actionRemoveSource`, keeping confirmation, multi-select and undo |
| OI-36 | 1.3 | debt | low | Delegate glyphs for the eye and lock are hand-drawn rather than themed icons — the theme`s are sized for upstream`s checkbox rows. Legible, but should become real artwork alongside the logo (OI-17) | first release | open |
| OI-37 | 1.3 | bug | **high** | **Canvas reference leak, fixed.** `obs_source_get_canvas()` and `obs_get_main_canvas()` both return *strong* references; comparing the raw pointers leaked two canvas refs per scene creation, and a Job switch creates every scene at once. Found by inspection while checking whether the guard survives a Job switch, not by any test | — | ✅ **closed 2026-08-13** |
| OI-38 | 1.3 | bug | med | Program-Canvas marker never invalidated | 1.4 | ✅ **closed 2026-08-13** — `OBS_FRONTEND_EVENT_SCENE_CHANGED` drives `refreshProgramMarkers()` |
| OI-39 | 1.3 | risk | med | Job-switch path unverified | 1.4 | ✅ **closed 2026-08-13** — the frontend collection-changed event triggers a single `reload()` rather than tracking the storm of create/remove signals. Verified live: switching Jobs replaced both Canvases with the new one, correct order and program marker |

---

## Phase 0 — Foundation

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 0.1 | Fork branch topology and merge cadence | **`done`** | 2026-08-12 | [UPSTREAM.md](UPSTREAM.md) · `2d5d1942c` | `origin` → `git@github.com:cikenbrand/mission-capture.git`; 22 stale obsproject refs pruned. `upstream` added with **push URL `DISABLED`** — an addition to the plan, verified to fail closed. `master` + `develop` at the fork point, both pushed and tracking. `master` kept (not renamed to `main`). Monthly scheduled task `mission-capture-upstream-merge` reports drift and seam changes — **reports only, never merges**. Deviation: **no `upstream-tracking` branch** — `remotes/upstream/master` does the same job and can't go stale. Carried forward: OI-16 |
| 0.2 | Rebranding | **`done`** | 2026-08-12 | `MissionCapture64.exe` version resource verified | Product/company/copyright centralised in `bootstrap.cmake`; exe renamed; CPack package renamed; window title, About dialog and locale strings rebranded. **Config dir** moved to `%APPDATA%\Cyberian Resources\Mission Capture` via a rewrite at the `GetAppConfigPath`/`GetAppConfigPathPtr` chokepoints rather than editing 47 literals in 16 files — see `frontend/subsea/MCBranding.hpp` for the trade-off. **Crash-log and log upload disabled** (both pointed at obsproject.com); About no longer makes a network call. `THIRD_PARTY_NOTICES.md` added with the GPLv2 source offer. OBS icons deleted, placeholder icon in place. Carried forward: OI-17…OI-22 |
| 0.3 | Windows-only build slimming | **`done`** | 2026-08-12 | Clean build exit 0; [BUILDING.md](BUILDING.md) | `windows-subsea-x64` preset added: 22 cache vars, 19 plugins ship (was 27). **Configure 45 s, clean build 200 s, rundir 382 MB.** Added two options upstream lacks: `ENABLE_FRONTEND_TOOLS` and `ENABLE_UPDATER` (the latter otherwise ships an updater.exe that would patch our install with OBS binaries). Pinned `CMAKE_GENERATOR_INSTANCE` to VS Community — see OI-18, my earlier assumption that disabling plugins would dodge the ATL problem was **wrong**. Also made the root CMakeLists record disabled scripting in the feature summary, which upstream silently omits. Beyond the planned list I also disabled WhatsNew, service/compat updates, NVAFX/NVVFX and CoreAudio encoder — all either phone home or need absent redistributables |
| 0.4 | Feature-flag system | **`done`** | 2026-08-12 | Runtime verified: 30 hidden / 0 missing; override round-trip 30→28 | 16 flags in one table in `MCFeatures.cpp`; `features.ini` self-writes with per-flag comments on first run. Two seams, not one: `load()` in `OBSApp::OBSInit` + `apply()` at the end of `OBSBasic::OBSInit`. `features.ini` lives in the config root, not the profile dir — product-level, not per-Rig. **Found 4 more config-path literals my 0.2 verification missed** (grep pattern `"obs-studio` skipped every leading-slash variant); one of them broke startup entirely. `ScenesDock`/`SourcesDock` deliberately default ON until Phase 1 builds Layers |
| 0.5 | Test harness bring-up | **`done`** | 2026-08-12 | `T0 Foundation: PASS — 19 passed, 0 failed, 1 skipped` | ctest wired up and green (4/4 cmocka). **CMocka already ships in obs-deps** — no vcpkg needed, contrary to the plan; but upstream`s `test/cmocka/CMakeLists.txt` used `${CMOCKA_LIBRARIES}`, which that package does not set, so the suite could never have linked. Fixed to `cmocka::cmocka` and its hardcoded multi-config test paths corrected. `--dump-ui-manifest` added, emitting actions/docks/menus/element types/**OBS hotkeys**/feature flags. Harness written with corrected paths (portable config root is `rundir/config`, and cwd must be the exe dir). Run reports working incl. dirty-tree flag and criteria table. Phase 0 acceptance criteria now carry `P0-ACn` IDs |
| 0.6 | CI reduction | **`done`** | 2026-08-12 | All 10 workflow YAMLs parse; CI preset configures clean | Upstream entry points (`push`, `pr-pull`, `publish`, `scheduled`) neutered to `workflow_dispatch` rather than deleted — deleting guarantees a conflict every time upstream edits them, neutering does not. The `workflow_call` workflows go inert automatically. New `mission-capture.yaml`: format → build+ctest+branding check → T0 smoke, with dep caching and artifact upload. **Preset split into base/local/CI** — the VS-instance pin is a property of this machine and would break a runner. Skipped upstream`s `swift-format` job (no Swift here, and macOS minutes bill 10x). **Unverified: never executed on GitHub** — see OI-28 |
| 0.7 | Baseline hardware benchmark | **`done`** | 2026-08-12 | [hardware-baseline.md](hardware-baseline.md) | **Headline: no session-creation failures at all**, either family, up to 12 concurrent 1080p60 — the NVENC cap the plan was built around did not materialise. The binding constraint is **throughput**, so 6.6 should be a dropped-frame watchdog first and a session counter second. Both target families measured on one machine; AMF (iGPU) beat NVENC by 40–70%, almost certainly a laptop power-budget artifact. D: sustains ~1.6 GB/s. **Results are provisional — not the target machine — so OI-5 stays open.** OI-6 untouched (no capture hardware connected) |

## Phase 1 — Shell and the Layers tree

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 1.1 | Terminology | **`done`** | 2026-08-13 | `T1: PASS` — 122 UI strings swept, 0 banned terms | 131 locale strings renamed to Canvas/Element/Job/Rig, values only, keys untouched. Found and fixed **two collisions with OBS`s own "canvas"** (base output resolution) and two article-agreement bugs the rename introduced ("a element"). Also cleared **33 strings still saying OBS** — 0.2 debt that never reached the locale file. Rather than deleting 76 translation files, trimmed `locale.ini` to en-US: the files stay mergeable, but no stale translation can be selected, and a non-English Windows will not auto-pick one. `CODING.md` written. T1 suite created as a permanent regression guard |
| 1.2 | `MCLayersModel` | **`done`** | 2026-08-13 | `T1: PASS` — 12 model assertions incl. the Z-order reversal | Two-level `QAbstractItemModel` over the main render target`s scenes. Row↔libobs index reversal implemented and **asserted against a fixture Job with a known stack** (Top/Middle/Bottom). All libobs signal handlers marshal to the Qt thread. Reorder emits `dataChanged` rather than remove/insert, so selection and scroll survive. Groups flattened (three element types, no grouping) with polymorphic nodes so a recursive model stays possible. **Verified without a view** by dumping the model through the UI manifest — 1.3 builds the widget. Found a real bug doing so: `ProgramRole` used `obs_get_output_source(0)`, which returns the *transition*, not the scene, so no Canvas ever showed as program |
| 1.3 | `MCLayersTree` and delegate | **`done`** | 2026-08-13 | `T1: PASS`; screenshot verified in the running app | `QTreeView` + painted delegate, added as a **Layers dock alongside** the old Scenes/Sources docks so the two can be compared — 1.4 retires them. Toggles are **painted, not child widgets**: upstream gives every row two live QCheckBoxes, which does not scale to a tree of eight Canvases. Model gained rename (`setData`, with a duplicate-name guard), visible/lock toggles, and Element drag-drop within and between Canvases (move shares the source rather than duplicating, so one capture device is not opened twice). Re-entrancy guard on selection from the start. **Not done here:** Canvas reordering (OI-33) and Remove — Remove must route through OBSBasic`s undo stack in 1.4 rather than give the operator an unundoable delete mid-dive |
| 1.4 | Wire the tree in, retire the old docks | **`done`** | 2026-08-13 | `T1: PASS`; Job switch and old-dock retirement verified live | **Seam #11 turned out to be unnecessary and is struck from the seam table.** The preview does not push selection to a widget — it calls `obs_sceneitem_select()` and libobs emits `item_select`, so the tree listens to the same signal upstream`s SourceTree does. **1.4 touched zero upstream files.** Canvas switching uses `obs_frontend_set_current_scene()` rather than the private `OBSBasic::SetCurrentScene()`. Remove triggers upstream`s own QAction so it keeps confirmation and undo. Scenes/Sources docks hidden by flag but kept alive — `SaveSceneListOrder()` still reads the Scenes list for Canvas order, so the tree mirrors that order rather than inventing one that would not survive a save |
| 1.5 | UI surface audit and hiding | `todo` | | | |
| 1.6 | Element type restriction | `todo` | | | |
| 1.7 | Inspection-appropriate defaults | `todo` | | | |
| 1.8 | New Job wizard | `todo` | | | |
| 1.9 | Recording-safety affordances | `todo` | | | |

## Phase 2 — Video elements

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 2.1 | Unified Video Capture Device element | `todo` | | | Needs OI-6 |
| 2.2 | Simplified capture properties | `todo` | | | Needs OI-6 |
| 2.3 | Device loss and recovery | `todo` | | | |
| 2.4 | RTSP Camera element | `todo` | | | |
| 2.5 | Add Element picker | `todo` | | | |
| 2.6 | Latency and health measurement | `todo` | | | |

## Phase 3 — Data core

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 3.1 | Channel registry | `todo` | | | Needs OI-4 · unblocks Phase 4 |
| 3.2 | Frame assembler | `todo` | | | |
| 3.3 | Parsers | `todo` | | | |
| 3.4 | Channel transforms | `todo` | | | |
| 3.5 | Simulator transport | `todo` | | | Unblocks Phase 4 testing |
| 3.6 | Configuration persistence | `todo` | | | |
| 3.7 | Plugin shell and websocket vendor API | `todo` | | | Test hook for every later phase |

## Phase 4 — Overlay editor

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 4.1 | Template store | `todo` | | | |
| 4.2 | `mc_text` data-bound source | `todo` | | | |
| 4.3 | Overlay Edit mode controller | `todo` | | | |
| 4.4 | Overlay Items list | `todo` | | | |
| 4.5 | Item types | `todo` | | | |
| 4.6 | Assignment to Canvases | `todo` | | | |
| 4.7 | Update-rate control | `todo` | | | |
| 4.8 | Import / export | `todo` | | | |

## Phase 5 — Transports and configuration UI

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 5.1 | Serial transport (Win32) | `todo` | | | Needs OI-4 |
| 5.2 | Network transports | `todo` | | | |
| 5.3 | Device manager dialog | `todo` | | | |
| 5.4 | Parser configuration wizard | `todo` | | | Highest-value UI in the phase |
| 5.5 | Channels view | `todo` | | | |
| 5.6 | Settings integration and status | `todo` | | | |

## Phase 6 — Multi-Canvas recording

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 6.1 | Per-Canvas recording configuration | `todo` | | | |
| 6.2 | `MCCanvasRecorder` | `todo` | | | Verify OI-7 and OI-8 before building on them |
| 6.3 | `MCRecordingManager` | `todo` | | | |
| 6.4 | Record panel | `todo` | | | |
| 6.5 | Hotkeys | `todo` | | | |
| 6.6 | Resource guard | `todo` | | | Needs OI-5 |
| 6.7 | Filename templating | `todo` | | | |
| 6.8 | Auto-split and continuity | `todo` | | | |

## Phase 7 — Secondary capture

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 7.1 | Encoder sharing and the packet ring | `todo` | | | Needs OI-1 |
| 7.2 | Independence guarantees | `todo` | | | |
| 7.3 | Clip control UI | `todo` | | | |
| 7.4 | Naming and organisation | `todo` | | | |
| 7.5 | Hotkeys | `todo` | | | |
| 7.6 | Data log integration | `todo` | | | Depends on 8.1 |
| 7.7 | Snapshot capture engine | `todo` | | | Only needs Phases 1, 2, 4 — can move earlier |
| 7.8 | Preview gallery | `todo` | | | |
| 7.9 | Saving, formats, and naming | `todo` | | | |
| 7.10 | Data metadata | `todo` | | | |
| 7.11 | Snapshot control and hotkeys | `todo` | | | |

## Phase 8 — Sidecar data log and hardening

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 8.1 | Log writer | `todo` | | | |
| 8.2 | CSV format | `todo` | | | Needs OI-2, OI-3 |
| 8.3 | Manifest and clock sync | `todo` | | | |
| 8.4 | Raw log | `todo` | | | |
| 8.5 | Integration with multi-record and clips | `todo` | | | |
| 8.6 | Event marking | `todo` | | | Needs OI-3 |
| 8.7 | Post-dive review tooling (spec only) | `todo` | | | Specification, no code |
| 8.8 | Hardening and soak | `todo` | | | |

## Phase 9 — WebRTC streaming

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 9.1 | Simplified stream configuration | `todo` | | | |
| 9.2 | Choosing what to stream | `todo` | | | |
| 9.3 | Resource-guard integration | `todo` | | | |
| 9.4 | Connection resilience | `todo` | | | |
| 9.5 | Server-side documentation | `todo` | | | |
| 9.6 | Keep SRT and RIST reachable | `todo` | | | |

---

## Upstream merges

Kept here rather than in a separate file so there is one place to look. Monthly cadence
([Phase 0 task 0.1](phase-0-foundation.md)).

| Date | Upstream commit | Conflicts | Seams touched | Notes |
|---|---|---|---|---|
| — | fork point `14e3dae77` | — | — | Baseline |
