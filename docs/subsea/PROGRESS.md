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
| [1](phase-1-shell-and-layers.md) | Shell & Layers tree | 11 | **10** | **`done`** |
| [2](phase-2-video-elements.md) | Video elements | 6 | 0 | `todo` |
| [3](phase-3-data-core.md) | Data core | 7 | 0 | `todo` |
| [4](phase-4-overlay-editor.md) | Overlay editor | 8 | 0 | `todo` |
| [5](phase-5-transports.md) | Transports & config UI | 6 | 0 | `todo` |
| [6](phase-6-multi-record.md) | Multi-canvas recording | 8 | 0 | `todo` |
| [7](phase-7-secondary-capture.md) | Secondary capture | 11 | 0 | `todo` |
| [8](phase-8-sidecar-log.md) | Sidecar log & hardening | 8 | 0 | `todo` |
| [9](phase-9-webrtc-streaming.md) | WebRTC streaming | 6 | 0 | `todo` |
| | **Total** | **78** | **17** | |

**Acceptance criteria met:** 15 / 113 — `P0-AC1`–`AC5` and `P1-AC1`, `AC2`, `AC3`, `AC7`–`AC13`, each asserted by T0 or T1 with evidence in their run reports.

Phase 0 and Phase 1 define 13 criteria each; the later phases have not had theirs written yet, which is why the denominator is a plan figure rather than a measured one. Phase 0's remaining eight and Phase 1's remaining three (`AC4`, `AC5`, `AC6`) are untested, not failing.

**Task count changed 2026-08-13**: 1.9 split into 1.9a/b/c, so Phase 1 and the total each rose by two.

**Phase 1 reads 10 of 11 and is still `done`**: the eleventh, 1.9c, is `cut` — deferred by decision
because the app is only ever launched by hand. Its design is retained in the phase doc and the two
open items it owned are marked deferred rather than closed, so neither disappears if the deployment
model changes.

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
| OI-6 | planning | dependency | med | Capture-hardware inventory — which DeckLink and AVerMedia models | 2.1, 2.2 | ✅ **closed by decision 2026-08-13 — not needed.** The cards vary job to job, they already work in OBS, and this fork does not modify the capture backends, so a model matrix would test upstream code and still miss the next vessel's card. Phase 2 is validated on **recording integrity** instead, with a killable RTSP source standing in for a failing device. Four hardware-dependent criteria moved to a named field-check list in the phase doc rather than being dropped silently. Consequence worth keeping: task 2.3 (device loss and recovery) matters *more* under this decision, since correct behaviour with an arbitrary misbehaving card is the only guarantee left |
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
| OI-23 | 0.4 | debt | **med** | **Hidden features keep their hotkeys — now measured.** Corrected in 0.5: the leak is *not* in `QAction` shortcuts (the manifest shows **0** hidden actions hold one) but in **libobs hotkeys**, which `setVisible(false)` cannot touch. Six are live for hidden features: `StartStreaming`, `StopStreaming`, `ForceStopStreaming`, `StartReplayBuffer`, `StopReplayBuffer`, `Transition`. T0 records them as a SKIP against `P1-AC9`, so the report will show the leak closing. **Closed in 1.5, and it was nine not six** — `obs_enum_hotkeys` also turned up `QuickTransition.1/2/3`, registered per configured transition with a numeric suffix, so prefix matching was needed. Unregistered by name, which keeps it out of `OBSBasic`'s private hotkey members: no upstream file changed. T0's SKIP is now a PASS | 1.5 | ✅ **closed 2026-08-13** |
| OI-24 | 0.4 | bug | low | Upstream logs `Failed to rename basic scene collection file` on **every fresh config** — the first-run migration renames `basic/scenes.json` without checking it exists. Cosmetic, but it pollutes error-scanning tests, so it is allow-listed in `testing.md`. Left unpatched to avoid touching an upstream file for no functional gain; candidate for an upstream PR | — | open |
| OI-25 | 0.4 | debt | low | `decklink-captions` and `decklink-output-ui` fail to load their `en-US` locale at startup. Both are peripheral plugins that ride along with `ENABLE_DECKLINK` and neither is used; worth disabling outright if a switch can be added | 2.1 | open |
| OI-26 | 0.5 | bug | low | `--dump-ui-manifest` returns early from `OBSBasic::OBSInit`, so the crash handler never records a sentinel location and logs an error at shutdown. Confirmed **absent from normal runs**, so it is an artifact of the test-only flag, not a product defect. Allow-listed in `common.ps1`; not root-caused | — | open |
| OI-27 | 0.5 | dependency | low | `ENABLE_UNIT_TESTS` is on in the preset, so every build compiles the cmocka suite. Negligible now (4 small tests) but worth a separate CI-only preset if it grows | 0.6 | open |
| OI-28 | 0.6 | bug | **high** | **CI ran red from 0.6 until 2026-08-13** — the original wording (`never run`) was wrong; there were 20+ runs, all failing, unwatched. Four separate faults, found in order: (1) six files failed `clang-format`; (2) **Configure died in ~3s because the fork had no git tags**, so `git describe --tags` returned a bare SHA and `project(VERSION "bf8b17bcc")` was rejected — three earlier hypotheses (preset instance pin, pinned SDK 10.0.26100, poisoned cache) were tested and disproved first; (3) unit tests died at `0xc0000135`, see OI-40; (4) a CDN drop mid-download, see OI-41. **Run `31635011692` is green across all three jobs.** Fixed by tag `0.1.0` + commits `3771413d3`, `39b5db52c` | release confidence | ✅ **closed 2026-08-13** |
| OI-29 | 0.6 | risk | med | The T0 smoke job is `continue-on-error: true`. Hosted runners have no GPU and fall back to WARP software D3D11; whether OBS initialises there was unknown. **Answered: it does.** T0 passed on `windows-2022` in 32s (run `31635011692`), with the OI-23 hotkey leak as its only SKIP. Left non-blocking for now on the stated rule — *a few* runs, not one — but the risk this tracked is retired; flipping to required is now a judgement call about flakiness, not about WARP | — | open — downgrade to low |
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
| OI-40 | 0.6 | bug | **high** | **The unit tests were testing someone else`s binary.** The cmocka executables link libobs but have no `obs.dll` beside them, so which one loaded was decided by PATH — and this machine has OBS Studio installed at `C:\Program Files\obs-studio\bin\64bit`, on PATH. Every local *4/4 passed* since 0.5 exercised the **installed** OBS, not our build; CI, with nothing to find, died at `0xc0000135` before `main()`. `test/cmocka/CMakeLists.txt` now sets `ENVIRONMENT_MODIFICATION` per test: the dep bundle`s `bin` dirs (FFmpeg + zlib, which only a *packaged* build copies next to the binaries) then our rundir last, so it lands first. Bundle path read from `CMAKE_PREFIX_PATH`, not rebuilt from the version date. Proved both ways with PATH cut to `system32`: ctest 4/4 green, and the same exe without the property still `0xC0000135` | — | ✅ **closed 2026-08-13** (`3771413d3`) |
| OI-41 | 0.6 | bug | med | **The dependency cache had never once been written.** Its save step was gated on `success()` — the whole job — and no run had ever finished green, so all 20+ runs re-downloaded ~1 GB and each got another chance to flake. One did: Configure died 0.17s into the x86 bundle with curl error 56. Save moved to immediately after Configure (completing Configure means every archive arrived whole and matched its SHA256, which is all the cache needs; the original worry was a half-written copy from an *aborted* download, which cannot survive that). Configure also retries 3×. Cache written for the first time on run `31635011692` | — | ✅ **closed 2026-08-13** (`39b5db52c`) |
| OI-42 | 1.5 | bug | **high** | **Hidden docks kept a live toggle in View ▸ Docks**, so the retired Scenes and Sources docks could be switched straight back — the two that must not come back, since Layers is now the only thing keeping its model in sync. `setupDockAction` also forces the action back to enabled, so it had to be made invisible rather than disabled. Invisible to the 0.4 verification because Qt creates these actions with no `objectName`; they are now named, so T1 asserts each toggle matches its dock | — | ✅ **closed 2026-08-13** |
| OI-43 | 1.5 | bug | **high** | **No settings page could be hidden at all.** `MCFeatures::apply()` only walked children of the main window, and the settings dialog is separate and built on demand, so the Stream page — services, OAuth, stream keys — stayed fully usable with `StreamingUI` off. Fixed with an `apply(QDialog *)` overload called from its constructor. Hiding the page widget would have achieved nothing (`QStackedWidget` hides every page but the current one regardless); the navigation row is the real gate, and its index is resolved from the stack rather than hardcoded. `--dump-ui-manifest` now builds the dialog and records the rows, so this is testable | — | ✅ **closed 2026-08-13** |
| OI-44 | 1.5 | bug | med | **`menuCrashLogs` was never actually hidden**, despite being listed under `LogUpload` since 0.4. `QMenu` is a `QWidget`, so `setVisible(false)` found it and hid the *popup* — which is hidden anyway until opened — while the entry under Help stayed. What shows a submenu entry is its `menuAction`. Found while adding `orderMenu` and hitting the same wall. `apply()` now special-cases `QMenu` | — | ✅ **closed 2026-08-13** |
| OI-45 | 1.5 | bug | med | **`QMenu::actions()` returns by value**, so `std::any_of(menu->actions().begin(), menu->actions().end(), …)` built a range across two different temporaries and never terminated — startup hung with the main thread blocked and the render loop still ticking, which is what made it look like a deadlock rather than a runaway loop. Bound to a local first. Recorded because the failure mode is worth recognising again: an app that starts, logs, then stops between two known points | — | ✅ **closed 2026-08-13** |
| OI-46 | 1.5 | risk | low | **A dirty shutdown blocks unattended startup.** Killing the app leaves a `.sentinel/run_*` file, and the next launch opens a modal crash dialog that waits forever — including under `--dump-ui-manifest`. CI has not hit it because runners start from a clean config, but a vessel machine that loses power mid-dive will show this on the next boot, and an auto-start setup would sit on it. Phase 1.9 should decide whether the prompt is right for an unattended DVR. Owned by 1.9c, which was **deferred by decision on 2026-08-13**: the app is always launched by hand, so someone is there to answer the prompt | 1.9c | ⏸ **deferred — accepted risk** |
| OI-47 | 1.4 | bug | **high** | **Nothing could be added to a Job between 1.4 and 1.6.** `actionAddSource` lives only on `sourcesToolbar`, inside the Sources dock 1.4 retired, and the Layers context menu's Add Canvas entry emitted `canvasActivated` — a signal with no receiver anywhere in the tree. So Add Element was unreachable and Add Canvas silently did nothing. Neither suite caught it: **T0 and T1 assert what is hidden, and nothing asserted that the surface left behind still works.** Fixed in 1.6 by borrowing upstream's `actionAddScene` and `actionAddSource` into the Layers menu, and T1 now asserts both are still borrowable. The wider lesson — every retirement needs a matching "and this still works" assertion — applies to 1.7 through 1.9 | — | ✅ **closed 2026-08-13** |
| OI-48 | 1.7 | deferred | med | **Resolution and FPS do not yet match the capture source.** The plan asks for this "rather than forcing 1920×1080", and 1.7 honours the second half — nothing is forced, base stays at OBS's display-derived value — but not the first: at first run there is no capture source to match. The hook belongs where a device's format is already being read, which is task **2.2** (simplified capture properties). Until then a 1440p monitor produces a 1440p canvas for a 1080p camera, which upscales and wastes encoder throughput. Not silently wrong — just not yet right | 2.2 | open |
| OI-49 | 1.7 | debt | low | **Auto-split is set but inert.** `RecSplitFile` exists only in Advanced output mode and Phase 1 leaves the mode at Simple, so the defaults 1.7 writes are correct and unused. Deliberate: switching output modes drags in `recordEncoder.json` and the whole advanced encoder surface, which is task **6.8**'s to own. Flagged so nobody reads the passing test as proof that splitting works today | 6.8 | open |
| OI-50 | 1.7 | debt | low | **`%CANVAS%` resolves to the program Canvas.** Correct while exactly one recording exists, but Phase 6 records several Canvases at once and each file must carry its own name, so the token has to resolve per-recording rather than from the frontend's current scene. `MCDefaults::expandTokens` is the single place to change | 6.2 | open |
| OI-51 | 1.8 | debt | med | **The Job menu could not retire with the Rig menu.** 1.8 replaces *creating* a Job but not *switching* between existing ones, and Scene Collection's list is the only route to that today. So `SceneCollectionMenu` stays ON while `ProfileMenu` goes off. Needs a Job switcher — a recent-Jobs list in File, or a Jobs panel — before the menu can go. Until then the vocabulary is inconsistent: one concept, one retired menu and one kept. **Deliberately not folded into the 1.9 split**: it is a feature rather than a safety fix, and 1.9 is already carrying two inherited items | later | open |
| OI-52 | 1.5 | bug | **high** | **OBS's auto-config wizard still ran on a genuine first run**, despite `AutoConfigWizard` being flagged off since 0.4. `OBSBasic::OBSInit` invokes `on_autoConfigure_triggered` directly on the first-run path, so hiding the action changed nothing. Never caught because `--dump-ui-manifest` exits before the queued call fires — the same blind spot as OI-47, and the third leak of this shape. An operator's first experience would have been an OBS wizard tuning for Twitch bitrate. Now calls the Job wizard instead | — | ✅ **closed 2026-08-13** |
| OI-53 | 1.8 | risk | med | **The wizard's interactive flow has no automated coverage.** Its parts are tested — the action exists, metadata round-trips through a real Job file, device enumeration runs on every manifest dump — but no test walks the pages, presses Finish, and checks that a Job with N Canvases and the right recording path comes out. Driving a modal `QWizard` needs either a Qt test harness or a `--create-job` style flag, and neither is worth inventing mid-phase. Worth revisiting when 2.5 builds the Add Element picker, since that faces the same problem | 2.5 | open |
| OI-54 | 1.8 | risk | med | **First run now opens a modal wizard, which blocks unattended startup.** Intended for an operator, but it compounds OI-46: a vessel machine that is imaged fresh, or one recovering from a dirty shutdown, will sit on a dialog nobody is there to answer. Owned by 1.9c, which was **deferred by decision on 2026-08-13**: manual launch only, so the wizard on first run is seen by the person who needs it | 1.9c | ⏸ **deferred — accepted risk** |
| OI-55 | 1.9a | risk | med | **The recording-active half of 1.9a has no automated coverage.** Idle state is asserted — indicator present but hidden, defaults, nothing locked — but not the behaviour that matters: indicator visible with a running clock, edits refused, the override releasing them and clearing on stop. A manifest dump cannot produce a live recording. **1.9b forces the fix**: testing a disk-full stop needs a real recording under test control, so the harness that unblocks this has to be built there anyway | — | ✅ **closed 2026-08-13** — 1.9b's recording test uses `--startrecording`, which gives the harness a live recording; the disk-floor stop, the file, and its integrity are all asserted through it |
| OI-56 | 1.7 | bug | **high** | **Every date token but the first stayed literal in recording filenames.** The template shipped in 1.7 was `%CCYY%%MM%%DD%_%hh%%mm%%ss%`, but libobs' tokens have no trailing delimiter and `%%` is an escaped percent, so files were named `..._2026%MM%DD_14%mm%ss.mkv`. Every recording since 1.7 would have collided on the same name within an hour. **Nothing that reads config could have caught it** — it took recording an actual file in 1.9b. Template corrected, `%` added to the filename sanitiser, and T1 now asserts a produced filename contains no `%` | — | ✅ **closed 2026-08-13** |
| OI-57 | 1.9b | bug | **high** | **An unreadable output path read as a full disk.** `os_get_free_disk_space()` returns 0 when it cannot stat the volume — a bad path, an unplugged drive, a permissions failure — and the first version treated 0 as at-or-below the floor, so it would have stopped a perfectly healthy recording. Exactly the harm the class exists to prevent. Zero is now treated as unknown: the field shows `--` and nothing is stopped. Found because a test's output path was invalid | — | ✅ **closed 2026-08-13** |

---

## Phase 0 — Foundation

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 0.1 | Fork branch topology and merge cadence | **`done`** | 2026-08-12 | [UPSTREAM.md](UPSTREAM.md) · `2d5d1942c` | `origin` → `git@github.com:cikenbrand/mission-capture.git`; 22 stale obsproject refs pruned. `upstream` added with **push URL `DISABLED`** — an addition to the plan, verified to fail closed. `master` + `develop` at the fork point, both pushed and tracking. `master` kept (not renamed to `main`). Monthly scheduled task `mission-capture-upstream-merge` reports drift and seam changes — **reports only, never merges**. Deviation: **no `upstream-tracking` branch** — `remotes/upstream/master` does the same job and can't go stale. Carried forward: OI-16 |
| 0.2 | Rebranding | **`done`** | 2026-08-12 | `MissionCapture64.exe` version resource verified | Product/company/copyright centralised in `bootstrap.cmake`; exe renamed; CPack package renamed; window title, About dialog and locale strings rebranded. **Config dir** moved to `%APPDATA%\Cyberian Resources\Mission Capture` via a rewrite at the `GetAppConfigPath`/`GetAppConfigPathPtr` chokepoints rather than editing 47 literals in 16 files — see `frontend/subsea/MCBranding.hpp` for the trade-off. **Crash-log and log upload disabled** (both pointed at obsproject.com); About no longer makes a network call. `THIRD_PARTY_NOTICES.md` added with the GPLv2 source offer. OBS icons deleted, placeholder icon in place. Carried forward: OI-17…OI-22 |
| 0.3 | Windows-only build slimming | **`done`** | 2026-08-12 | Clean build exit 0; [BUILDING.md](BUILDING.md) | `windows-subsea-x64` preset added: 22 cache vars, 19 plugins ship (was 27). **Configure 45 s, clean build 200 s, rundir 382 MB.** Added two options upstream lacks: `ENABLE_FRONTEND_TOOLS` and `ENABLE_UPDATER` (the latter otherwise ships an updater.exe that would patch our install with OBS binaries). Pinned `CMAKE_GENERATOR_INSTANCE` to VS Community — see OI-18, my earlier assumption that disabling plugins would dodge the ATL problem was **wrong**. Also made the root CMakeLists record disabled scripting in the feature summary, which upstream silently omits. Beyond the planned list I also disabled WhatsNew, service/compat updates, NVAFX/NVVFX and CoreAudio encoder — all either phone home or need absent redistributables |
| 0.4 | Feature-flag system | **`done`** | 2026-08-12 | Runtime verified: 30 hidden / 0 missing; override round-trip 30→28 | 16 flags in one table in `MCFeatures.cpp`; `features.ini` self-writes with per-flag comments on first run. Two seams, not one: `load()` in `OBSApp::OBSInit` + `apply()` at the end of `OBSBasic::OBSInit`. `features.ini` lives in the config root, not the profile dir — product-level, not per-Rig. **Found 4 more config-path literals my 0.2 verification missed** (grep pattern `"obs-studio` skipped every leading-slash variant); one of them broke startup entirely. `ScenesDock`/`SourcesDock` deliberately default ON until Phase 1 builds Layers |
| 0.5 | Test harness bring-up | **`done`** | 2026-08-12 | `T0 Foundation: PASS — 19 passed, 0 failed, 1 skipped` | ctest wired up and green (4/4 cmocka). **CMocka already ships in obs-deps** — no vcpkg needed, contrary to the plan; but upstream`s `test/cmocka/CMakeLists.txt` used `${CMOCKA_LIBRARIES}`, which that package does not set, so the suite could never have linked. Fixed to `cmocka::cmocka` and its hardcoded multi-config test paths corrected. `--dump-ui-manifest` added, emitting actions/docks/menus/element types/**OBS hotkeys**/feature flags. Harness written with corrected paths (portable config root is `rundir/config`, and cwd must be the exe dir). Run reports working incl. dirty-tree flag and criteria table. Phase 0 acceptance criteria now carry `P0-ACn` IDs |
| 0.6 | CI reduction | **`done`** | 2026-08-12 | All 10 workflow YAMLs parse; CI preset configures clean | Upstream entry points (`push`, `pr-pull`, `publish`, `scheduled`) neutered to `workflow_dispatch` rather than deleted — deleting guarantees a conflict every time upstream edits them, neutering does not. The `workflow_call` workflows go inert automatically. New `mission-capture.yaml`: format → build+ctest+branding check → T0 smoke, with dep caching and artifact upload. **Preset split into base/local/CI** — the VS-instance pin is a property of this machine and would break a runner. Skipped upstream`s `swift-format` job (no Swift here, and macOS minutes bill 10x). **Verified green on GitHub 2026-08-13** — run `31635011692`, all three jobs `success` (Format 13s, Build+test 12m27s, T0 smoke 32s). Took four fixes to get there; see OI-28 |
| 0.7 | Baseline hardware benchmark | **`done`** | 2026-08-12 | [hardware-baseline.md](hardware-baseline.md) | **Headline: no session-creation failures at all**, either family, up to 12 concurrent 1080p60 — the NVENC cap the plan was built around did not materialise. The binding constraint is **throughput**, so 6.6 should be a dropped-frame watchdog first and a session counter second. Both target families measured on one machine; AMF (iGPU) beat NVENC by 40–70%, almost certainly a laptop power-budget artifact. D: sustains ~1.6 GB/s. **Results are provisional — not the target machine — so OI-5 stays open.** OI-6 untouched (no capture hardware connected) |

## Phase 1 — Shell and the Layers tree

| ID | Task | Status | Done | Evidence | Notes |
|---|---|---|---|---|---|
| 1.1 | Terminology | **`done`** | 2026-08-13 | `T1: PASS` — 122 UI strings swept, 0 banned terms | 131 locale strings renamed to Canvas/Element/Job/Rig, values only, keys untouched. Found and fixed **two collisions with OBS`s own "canvas"** (base output resolution) and two article-agreement bugs the rename introduced ("a element"). Also cleared **33 strings still saying OBS** — 0.2 debt that never reached the locale file. Rather than deleting 76 translation files, trimmed `locale.ini` to en-US: the files stay mergeable, but no stale translation can be selected, and a non-English Windows will not auto-pick one. `CODING.md` written. T1 suite created as a permanent regression guard |
| 1.2 | `MCLayersModel` | **`done`** | 2026-08-13 | `T1: PASS` — 12 model assertions incl. the Z-order reversal | Two-level `QAbstractItemModel` over the main render target`s scenes. Row↔libobs index reversal implemented and **asserted against a fixture Job with a known stack** (Top/Middle/Bottom). All libobs signal handlers marshal to the Qt thread. Reorder emits `dataChanged` rather than remove/insert, so selection and scroll survive. Groups flattened (three element types, no grouping) with polymorphic nodes so a recursive model stays possible. **Verified without a view** by dumping the model through the UI manifest — 1.3 builds the widget. Found a real bug doing so: `ProgramRole` used `obs_get_output_source(0)`, which returns the *transition*, not the scene, so no Canvas ever showed as program |
| 1.3 | `MCLayersTree` and delegate | **`done`** | 2026-08-13 | `T1: PASS`; screenshot verified in the running app | `QTreeView` + painted delegate, added as a **Layers dock alongside** the old Scenes/Sources docks so the two can be compared — 1.4 retires them. Toggles are **painted, not child widgets**: upstream gives every row two live QCheckBoxes, which does not scale to a tree of eight Canvases. Model gained rename (`setData`, with a duplicate-name guard), visible/lock toggles, and Element drag-drop within and between Canvases (move shares the source rather than duplicating, so one capture device is not opened twice). Re-entrancy guard on selection from the start. **Not done here:** Canvas reordering (OI-33) and Remove — Remove must route through OBSBasic`s undo stack in 1.4 rather than give the operator an unundoable delete mid-dive |
| 1.4 | Wire the tree in, retire the old docks | **`done`** | 2026-08-13 | `T1: PASS`; Job switch and old-dock retirement verified live | **Seam #11 turned out to be unnecessary and is struck from the seam table.** The preview does not push selection to a widget — it calls `obs_sceneitem_select()` and libobs emits `item_select`, so the tree listens to the same signal upstream`s SourceTree does. **1.4 touched zero upstream files.** Canvas switching uses `obs_frontend_set_current_scene()` rather than the private `OBSBasic::SetCurrentScene()`. Remove triggers upstream`s own QAction so it keeps confirmation and undo. Scenes/Sources docks hidden by flag but kept alive — `SaveSceneListOrder()` still reads the Scenes list for Canvas order, so the tree mirrors that order rather than inventing one that would not survive a save |
| 1.5 | UI surface audit and hiding | **`done`** | 2026-08-13 | T0 + T1 green; 24 hotkeys with none leaked, 4 dock toggles suppressed, Stream settings unreachable, 0 objectNames unfound | Audit in `ui-audit.md`, reviewed and accepted with no corrections; Profile/Scene Collection stay visible until 1.8 owns the replacement. The classification was the easy half — the audit found **three more ways a hidden feature was still reachable** beyond OI-23 (dock toggles, settings pages, empty menus), all closed here. Two bugs of my own on the way: a `QMenu::actions()` temporary-iterator pair that hung startup, and hiding a `QMenu` as a `QWidget` instead of via its `menuAction` — the latter had silently broken `menuCrashLogs` since 0.4. Order actions moved to the Layers context menu by borrowing upstream's own `QAction`s, so they keep upstream's undo. About dialog now names Mission Capture |
| 1.6 | Element type restriction | **`done`** | 2026-08-13 | T1 green: 1 offered of 13 registered, and `AllSourceTypes` restores all 13 | Filtered in `MCElementTypes`, never unregistered — an existing Job referencing any other type still loads, which is the whole point and is asserted directly. Allowlist names `mc_rtsp_source` and `mc_overlay_source` before they exist so 2.4 and phase 4 need no revisit; `decklink-input` verified against `decklink-source.cpp:273` since it cannot register on a machine without the drivers. Nested Canvases (`scene`) excluded — Layers is two levels by design. **Also fixed OI-47**: adding anything at all had been impossible since 1.4. **Confirmed by the user 2026-08-13:** one addable type in the interim is acceptable — Overlay arrives in a later phase as planned — and **audio Elements stay excluded permanently**, since the product uses global audio. That second one is a product decision, not a temporary state, so the allowlist should not acquire `wasapi_*` later without revisiting it | 
| 1.7 | Inspection-appropriate defaults | **`done`** | 2026-08-13 | T1 green on all 15 default assertions, including that an operator override in `basic.ini` still beats ours | MKV in both output modes, auto-remux off, quality-targeted (`RecQuality=HQ` → CRF 16 / CQP on both NVENC and AMF), NVENC-then-AMF-then-x264 chosen from what the machine has, `%JOB%_%CANVAS%_…` filename template with our own token expansion, auto-split on, desktop audio off on a new Job. **The ordering is counterintuitive and load-bearing**: `MCDefaults::apply()` runs *first* in `InitBasicConfigDefaults`, because `config_set_item_default` copies into the user-section map when no user value exists and reads consult that map first — so the *first* default set for a key wins, and running last would have been silently ignored. **Two items deferred, see OI-48 and OI-49** |
| 1.8 | New Job wizard | **`done`** | 2026-08-13 | T1 green: New Job action present, Rig menu retired, metadata round-trips from a Job file on disk, device enumeration survives a backend that failed to load | Four pages — details, destination with a free-space and hours-remaining readout, cameras, and disabled overlay/data stubs so Phases 4 and 5 have somewhere visible to land. Metadata lives under `modules` → `mission-capture` → `job` in the Job file, which is what Phase 8's manifest reads; libobs preserves that object even for plugins that are not loaded. The wizard also **replaces OBS's auto-config wizard on first run** — hiding the action was never enough, that path calls the slot directly (see OI-52). Rig menu retired per the 1.5 deferral; Job menu deliberately stays, see OI-51. **The interactive flow itself is not covered by automated tests** — see OI-53 |
| 1.9a | Recording-safety UI | **`done`** | 2026-08-13 | T1 green on 8 assertions: indicator installed and hidden while idle, confirm defaults on at 60 s, nothing locked or overridden in a fresh session | Indicator is a red bar above the record button, **hidden when idle rather than greyed** — a badge that is always present and only changes colour is one people stop reading. Elapsed time uses a monotonic clock, not upstream's per-tick counter, and excludes paused stretches. **Upstream's confirm-on-stop was off** (no default set anywhere), and its all-or-nothing design is wrong both ways, so it is now on with a 60 s threshold: a ten-second test still stops in one click, a dive asks. Layers lock refuses toggles, rename, delete and drag-drop; drag is refused in the *model* so no drop indicator appears. Override is offered only while recording and clears on stop. **The recording-active behaviour is not automatically tested** — see OI-55 |
| 1.9b | Disk-space protection | **`done`** | 2026-08-13 | T1 green on 13 assertions including a **real recording stopped by the floor and probed with ffprobe** — valid Matroska, opens cleanly | Permanent status-bar field, caution at 10 GB, critical at 2 GB, stop at 1 GB. Upstream already stopped at 50 MB, which at inspection bitrates is a few seconds and leaves no room for the muxer's trailer; that check stays as a backstop and should never fire. Tested by **moving the thresholds above the real free space** rather than filling a disk or faking the reading — production code path, nothing stubbed. `--startrecording` (upstream's own flag) drives a live recording, which also **closed OI-55**. Two bugs found and fixed on the way: OI-56 and OI-57 |
| 1.9c | Unattended startup | **`cut`** | 2026-08-13 | — | **Deferred by decision, not dropped.** Mission Capture is launched by hand by the operator and never auto-started with Windows, so both dialogs always have someone there to answer them. No `--unattended` mode at this stage. The design stays in the phase doc and should be built if the deployment model ever changes — a Startup shortcut, a scheduled task, a kiosk build. Accepted residual risk: after a crash mid-dive the operator answers the crash prompt before resuming, costing seconds at a bad moment |

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
