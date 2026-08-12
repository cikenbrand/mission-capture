# Building Mission Capture

Windows x64 only. See [README](README.md) for why.

---

## Prerequisites

| Requirement | Notes |
|---|---|
| **Visual Studio 2022** | Community or Professional. **Must include the "C++ ATL for latest build tools" component** — see below |
| **CMake ≥ 3.28** | |
| **Git** | Submodules are required |

### The ATL component is not optional

`win-dshow` — the DirectShow capture backend, which is how AVerMedia and other UVC devices work —
pulls in Elgato's `capture-device-support`, which includes `<atlbase.h>`, `<atlcomcli.h>` and
`<atlstr.h>`. Without ATL the capture backend does not compile, and Mission Capture has no
DirectShow capture.

Install it via Visual Studio Installer → Modify → Individual components → **C++ ATL for latest
v143 build tools (x86 & x64)**.

**If you have both Build Tools and Community installed**, CMake will pick Build Tools by default,
which typically does *not* have ATL. The `windows-subsea-x64` preset pins
`CMAKE_GENERATOR_INSTANCE` to the Community install for exactly this reason. If your Community
install lives elsewhere, override it:

```bash
cmake --preset windows-subsea-x64 -DCMAKE_GENERATOR_INSTANCE="C:/Path/To/VisualStudio/2022/Community"
```

To see what you have and whether it carries ATL:

```bash
"C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" -products * -property installationPath
```

---

## First build

```bash
git submodule update --init --recursive
```

This is required, not optional — `deps/libdshowcapture`, `plugins/obs-browser` and
`plugins/obs-websocket` are submodules, and `libdshowcapture` has a nested submodule of its own.
Configure fails with a missing `dshowcapture.hpp` if you skip it.

```bash
cmake --preset windows-subsea-x64
```

The first configure downloads the prebuilt dependency bundles into `.deps/` (a few hundred MB) and
takes noticeably longer than later ones.

```bash
cmake --build --preset windows-subsea-x64
```

Output: `build_x64/rundir/RelWithDebInfo/bin/64bit/MissionCapture64.exe`

---

## Measured timings

Recorded 2026-08-12 during task 0.3, on the development machine, with `.deps/` already populated:

| Step | Time |
|---|---|
| Clean configure | **~45 s** |
| Clean build (`RelWithDebInfo`, all targets) | **~200 s** |
| Incremental frontend-only rebuild | ~60 s |
| `rundir` size | **382 MB** |
| Plugins built | **19** |

That ~4-minute clean build is the developer-experience budget. If it drifts much past five minutes,
something has been added that deserves scrutiny.

No before-slimming baseline was captured: the stock `windows-x64` preset cannot complete on this
machine (it requests a "Visual Studio 18 2026" generator, and without the instance pin it selects
the ATL-less Build Tools install). The slimmed numbers above are the ones that matter day to day.

---

## What the slimmed preset excludes

Full rationale in [phase-0-foundation.md](phase-0-foundation.md) task 0.3. Summary: no CEF/browser
source, no Lua/Python scripting, no VST, no VLC, no AJA, no Intel QSV, no virtual camera, no
frontend-tools, no updater, and no components that phone home to obsproject.com.

The 19 plugins that do ship:

```
decklink  decklink-captions  decklink-output-ui  image-source  nv-filters
obs-ffmpeg  obs-filters  obs-nvenc  obs-outputs  obs-text  obs-transitions
obs-webrtc  obs-websocket  obs-x264  rtmp-services  text-freetype2
win-capture  win-dshow  win-wasapi
```

To build the full upstream feature set instead — for comparison, or to check whether a bug is ours
or upstream's — override the flags on the command line rather than editing the preset:

```bash
cmake --preset windows-subsea-x64 -DENABLE_BROWSER=ON -DENABLE_SCRIPTING=ON
```

---

## Known rough edges

- **AMD builds untested.** Only NVIDIA hardware has been used so far. `ENABLE_NVENC` is on and AMF
  comes via `obs-ffmpeg`; neither has been exercised on an AMD machine
  ([Phase 0 task 0.7](phase-0-foundation.md) covers this)
- **`decklink-captions` and `decklink-output-ui` still build** — they ride along with
  `ENABLE_DECKLINK` and have no separate switch. Both are peripheral; harmless for now
- **`rtmp-services` still builds.** The service list *fetch* is disabled, but the plugin provides
  service infrastructure that the streaming path expects
- **Line endings** — the repo has no `*.md` rule in `.gitattributes`, so git warns about LF→CRLF.
  Tracked as OI-16 in [PROGRESS.md](PROGRESS.md)
