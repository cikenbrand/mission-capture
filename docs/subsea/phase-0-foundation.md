# Phase 0 — Foundation

**Goal:** turn a clone of OBS Studio into a fork we can develop in confidently — correctly
branded, building fast on Windows only, with a feature-flag system and a working test loop.

**Prerequisites:** none. This is the starting line.

**Effort:** 1–2 weeks.

**Definition of done:** `tools/subsea-tests/run-tests.ps1 -Suite 0` is green, the app launches
under its own name from its own config directory, and a `git merge upstream/master` has been
performed at least once to prove the workflow.

---

## Tasks

### 0.1 — Fork branch topology and merge cadence
`~0.5 day`

- Add the upstream remote, and **disable pushing to it** so `git push upstream` can never reach the
  OBS Project's repository:
  ```
  git remote add upstream https://github.com/obsproject/obs-studio.git
  git remote set-url --push upstream DISABLED
  git fetch upstream --no-tags
  ```
  No local `upstream-tracking` branch — `remotes/upstream/master` already does that job and a local
  copy is one more thing to forget to update.
- Repoint `origin` at your own fork (it currently points at `obsproject/obs-studio`).
- Branch model: `master` = shippable, `develop` = integration, `feature/mc-*` per task group.
- Record the fork point (`14e3dae77`) in `docs/subsea/UPSTREAM.md` along with the merge procedure,
  health signals, and a merge log table.
- Set a **monthly** merge reminder. Merging monthly is a chore; merging annually is a project.

**Files:** `docs/subsea/UPSTREAM.md` (new)

---

### 0.2 — Rebranding
`2 days`

Required for trademark reasons (see [README §3.1](README.md#31-legal-notes-act-on-these-in-phase-0)), not cosmetics.

Settled values — use these exactly:

| Field | Value |
|---|---|
| Product name | **Mission Capture** |
| Organisation / author | **Cyberian Resources** |
| Config directory | `%APPDATA%\Cyberian Resources\Mission Capture\` |
| Executable | `Mission Capture.exe` |

- `QApplication::setApplicationName("Mission Capture")` and
  `setOrganizationName("Cyberian Resources")` in `frontend/OBSApp.cpp` — this determines the config
  directory, so get it right before any other config path work
- Executable name, product/company strings, version resource (`cmake/common/bootstrap.cmake`,
  `frontend/cmake/windows/`)
- Icons, splash, About dialog (`frontend/dialogs/OBSAbout.cpp`), window title
- Installer: name, upgrade GUID, install path, Start-menu entry, file associations
- Remove OBS logos and wordmarks from `frontend/data/images/`
- **Keep** `COPYING`, `AUTHORS`, and per-file GPL headers untouched, and add a
  `THIRD_PARTY_NOTICES.md` plus a source-availability statement in About
- Point crash reporting / log upload at our own endpoint, or disable it

**Files:** seams #4, #5 in [architecture.md §8](architecture.md#8-upstream-seams)

**Watch out:** changing the config directory orphans any existing profiles. Decide now whether to
migrate from `%APPDATA%\obs-studio` on first run (probably not — a clean start is cleaner).

---

### 0.3 — Windows-only build slimming
`1 day`

Add a `windows-subsea-x64` preset to `CMakePresets.json`. `CMakePresets.json` is strict JSON and
cannot carry comments, so the rationale lives here.

**Kept — load-bearing:**

| Option | Why |
|---|---|
| `ENABLE_DECKLINK` | Blackmagic SDI capture, the primary hardware |
| `ENABLE_WEBRTC` | WHIP output for [Phase 9](phase-9-webrtc-streaming.md) |
| `ENABLE_WEBSOCKET` | The test hook every later phase asserts through |
| `ENABLE_NVENC` | One of the two target encoder families |
| `ENABLE_HEVC` | WHIP advertises HEVC; also useful for recording |
| `ENABLE_NEW_MPEGTS_OUTPUT` | SRT / RIST, the Phase 9 alternatives to WebRTC |

**Dropped:**

| Option | Why |
|---|---|
| `ENABLE_SCRIPTING` | No Lua/Python for this audience; large dependency |
| `ENABLE_BROWSER` | CEF is ~200 MB and a large attack surface |
| `ENABLE_WHATSNEW` | A browser panel that fetches from obsproject.com |
| `ENABLE_VST` | Audio plugin hosting is irrelevant here |
| `ENABLE_VLC` | FFmpeg media playback is enough |
| `ENABLE_AJA` | Not on the confirmed hardware list |
| `ENABLE_FRONTEND_TOOLS` | Scripting-adjacent tools; **option added by us**, upstream has none |
| `ENABLE_VIRTUALCAM` | Hidden feature |
| `ENABLE_QSV11` | Intel encoding; the targets are AMD and NVIDIA |
| `ENABLE_UPDATER` | **Option added by us.** Would otherwise ship an updater.exe that patches a Mission Capture install with OBS Studio binaries |
| `ENABLE_SERVICE_UPDATES`, `ENABLE_COMPAT_UPDATES` | Both fetch lists from obsproject.com |
| `ENABLE_NVAFX`, `ENABLE_NVVFX` | NVIDIA effect SDKs; need redistributables |
| `ENABLE_COREAUDIO_ENCODER` | Needs Apple software installed on Windows |

`win-dshow` (AVerMedia and other UVC devices) and `obs-ffmpeg` (RTSP, and the muxer) have no
opt-out and stay on — both are load-bearing for [Phase 2](phase-2-video-elements.md).

macOS/Linux plugin directories stay in the tree, unbuilt — deleting them creates merge conflicts
for zero benefit.

**Toolchain pinning.** The preset sets `CMAKE_GENERATOR_INSTANCE` to the Visual Studio *Community*
install and the generator to `Visual Studio 17 2022`. Upstream's preset asks for "Visual Studio 18
2026", and on a machine with both Build Tools and Community installed CMake will otherwise pick
Build Tools — which lacks the ATL component that `win-dshow` needs via Elgato's
`capture-device-support`. Without the pin, the capture backend does not compile.

Measure and record configure+build time; this number is the developer-experience budget for the
next six months.

**Files:** `CMakePresets.json`, `plugins/CMakeLists.txt`, `frontend/cmake/os-windows.cmake`,
`CMakeLists.txt`

---

### 0.4 — Feature-flag system
`2 days`

The mechanism that lets us "remove" features without deleting code.

```cpp
// frontend/subsea/MCFeatures.hpp
namespace MCFeatures {

enum class Feature {
    StudioMode, ReplayBuffer, VirtualCam, StreamingUI, Transitions,
    Filters, AdvancedAudio, Stats, Multiview, Projectors, SourceToolbar,
    /* ... one per hideable surface ... */
};

bool enabled(Feature f);                 // reads compiled defaults, overridden by features.ini
void apply(class OBSBasic *main);        // hides menus/docks/toolbars/settings pages
const char *name(Feature f);
}
```

- Defaults compiled in (a `constexpr` table), overridable by `features.ini` **in the config root**
  alongside `global.ini` — not the profile directory, because these are product-level decisions
  rather than per-Rig ones. The file is written with all defaults and per-flag comments on first
  run so it is self-documenting in the field
- `apply()` walks named `QAction` and `QWidget` objects and calls `setVisible(false)`. Use
  `findChild<>()` by `objectName` — it degrades to a warning if upstream renames something, rather
  than failing to compile or crashing, and returns the miss count so drift is measurable.
  **`QLayout` is neither a widget nor an action** — list the buttons, not their layout
- Log every flag state *and its origin* (file or default) at startup so support tickets are
  diagnosable
- **Two seams:** `MCFeatures::load()` in `OBSApp::OBSInit()` before the window is built, and
  `MCFeatures::apply(this)` at the end of `OBSBasic::OBSInit()` once every dock exists

**Files:** `frontend/subsea/MCFeatures.{hpp,cpp}` (new), seams #3, #6, #7

**Watch out:** hiding a `QAction` does not disable its hotkey. Phase 1 task 1.2 must unregister
hotkeys for hidden features too, or a stray `Ctrl+Shift+...` will start a replay buffer that has
no UI.

---

### 0.5 — Test harness bring-up
`2 days`

- Add `include(CTest)` / `enable_testing()` to the root `CMakeLists.txt` (currently absent — see
  [architecture.md §9](architecture.md#9-testing-infrastructure-that-already-exists))
- Confirm `find_package(CMocka CONFIG REQUIRED)` resolves on Windows (vcpkg is the likely route);
  document the setup in `testing.md`
- Create `tools/subsea-tests/` with `run-tests.ps1`, `lib/common.ps1`, and `fixtures/`
- Add the `--dump-ui-manifest <path>` CLI flag: writes a JSON list of every visible menu action,
  dock, and settings page, then exits. Makes UI decluttering assertable instead of eyeballed
- Verify `ffprobe` is locatable and version-logged by the harness

**Run reports — the project's only systematic written record.** Full spec in
[testing.md § Run reports](testing.md#run-reports). Build it here, in Phase 0, because a reporting
format retrofitted at Phase 6 leaves the first five phases with no evidence trail.

- Every suite writes `report_<suite>_<timestamp>.{json,md}` next to its artifacts: result counts,
  full environment (commit, branch, **dirty-tree flag**, app version, OS, GPU, ffmpeg version), the
  acceptance-criteria table, and every assertion in order
- `run-tests.ps1` writes `out/run_<timestamp>/index.md` rolling up every suite in that invocation
- Assertions take an optional `-Criterion 'P<n>-AC<m>'` tag; the report groups by criterion so a run
  shows which acceptance criteria have passing evidence
- CI uploads the report directory as a build artifact

**Watch out:** the dirty-tree flag matters more than it looks. A green report from a working tree
with uncommitted changes proves nothing reproducible, and that is exactly the run someone will cite
six months later.

**Files:** `CMakeLists.txt`, `tools/subsea-tests/*` (new), seams #1, #8, #18

---

### 0.6 — CI reduction
`1 day`

- Cut `.github/workflows/` to Windows x64: build, `ctest`, artifact upload
- Add the Phase 0 smoke test as a CI step
- **Upload `tools/subsea-tests/out/` as a build artifact** so run reports survive the runner
- Delete or disable macOS/Linux/FreeBSD jobs, Sparkle/notarisation steps, and the upstream
  release automation
- Keep `clang-format` / `gersemi` checks — matching upstream formatting materially reduces merge
  noise, which is the whole point of the fork strategy

**Files:** `.github/workflows/*` (seam #16)

---

### 0.7 — Baseline hardware benchmark
`1 day` — **do this now, not in Phase 6**

The single largest unknown in the plan is how many simultaneous encodes the target hardware
sustains ([README §5.1](README.md#51-concurrent-encoder-sessions-phase-6--high)). Resolve it
before designing around a guess.

The target hardware is **AMD or NVIDIA**, so benchmark **AMF and NVENC** — ideally on one machine
of each, since they fail differently: NVENC refuses to create a session past its cap, while AMF
tends to degrade throughput instead. x264 is worth one run as a fallback data point; QSV can be
skipped.

On each target machine, script `ffmpeg` to run 1/2/3/4/6/8 concurrent 1080p30 and 1080p60 encodes
to **MKV** and record:

- The N at which session creation starts failing (NVENC) or throughput collapses (AMF)
- Encode FPS per session at each N
- Total CPU and GPU utilisation
- Aggregate write throughput vs. the recording disk's sustained speed

Write the results into `docs/subsea/hardware-baseline.md`. Phase 6's resource guard reads its
default limits from this table, and [Phase 9](phase-9-webrtc-streaming.md) adds the streaming
encode to the same budget.

**While you have the machine:** inventory the actual capture hardware — which DeckLink models,
which AVerMedia models, and anything else on the spread — into the same file. Phase 2 tests
against that list, and "there may be other capture cards as well" is a scope question best
answered with a list rather than a guess.

---

## Acceptance criteria

Criterion IDs are the tags `-Criterion` uses in the run reports
([testing.md § Run reports](testing.md#run-reports)). `P0-AC3` is "no unexpected errors in the
application log", asserted by T0 rather than listed here.

- [ ] `P0-AC1` App launches as "Mission Capture"; no "OBS" string or logo in UI, installer, or window title
- [ ] `P0-AC2` Config lives in `%APPDATA%\Cyberian Resources\Mission Capture\`
- [ ] `P0-AC6` Clean configure+build from scratch on Windows in a documented, measured time
- [ ] `P0-AC7` `ctest` runs the four existing cmocka tests and they pass
- [ ] `P0-AC4` `MCFeatures::enabled()` demonstrably hides at least one real menu item
- [ ] `P0-AC5` `--dump-ui-manifest` produces valid JSON
- [ ] `P0-AC8` Every suite run writes a Markdown + JSON report, and `run-tests.ps1` writes a roll-up index
- [ ] `P0-AC9` A report from a dirty working tree is flagged as such
- [ ] `P0-AC10` An assertion tagged with a `-Criterion` ID appears in the report's criteria table
- [ ] `P0-AC11` CI green on Windows x64 only, with the report directory uploaded as an artifact
- [ ] `P0-AC12` `hardware-baseline.md` exists with real encode numbers and a capture-hardware inventory
- [ ] `P0-AC13` One `git merge upstream/master` completed and documented

---

## Tests

Script: **`tools/subsea-tests/t0-foundation.ps1`** — full text in
[testing.md §T0](testing.md#t0--foundation-smoke).

What it asserts:

1. **Build** — configure with the `windows-subsea-x64` preset and build; non-zero exit fails
2. **Unit tests** — `ctest --output-on-failure`
3. **Branding** — scan the built binary's version resource and the UI manifest for the string
   `OBS`; fail if found outside legally-required attribution
4. **Launch smoke** — start with `--portable --multi` against a throwaway config dir, wait for the
   window, exit cleanly, then assert the log contains no `error`/`crash` lines and that the
   process exit code is 0
5. **Feature flags** — launch twice with different `features.ini` contents, dump the UI manifest
   each time, assert the flagged item is present in one and absent in the other
6. **Config isolation** — assert the throwaway config dir was created and the developer's real
   `%APPDATA%` profile was untouched
