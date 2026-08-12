# Mission Capture — Subsea Inspection DVR

> A stripped-down, inspection-focused fork of OBS Studio for the subsea industry.
>
> **Product:** Mission Capture · **Author:** Cyberian Resources
> **Status:** planning. Nothing in this directory has been implemented yet.
>
> 📋 **[PROGRESS.md](PROGRESS.md)** — what's done, what isn't, and what's in the way. Updated after
> every task. Start there if you want status rather than plan.

---

## 1. Product

### 1.1 Who it is for

| User | What they do all day | What they need from us |
|---|---|---|
| **3.4U inspection engineer** | Runs the video/data spread, produces the client deliverable | Reliable multi-camera recording, correct data overlay, clean file naming, an audit trail |
| **ROV pilot** | Flies the vehicle, watches the primary camera | A calm, uncluttered screen. One button to start recording. No accidental clicks into a settings maze |
| **Diver supervisor / dive tech** | Records helmet-cam and dive-panel video | Same, plus depth/gas data burned into the picture |

None of them are streamers. Almost none of them will ever open a settings dialog that has
"Twitch", "Bitrate ladder", or "Chroma key" in it. The core insight behind the fork is that
**OBS's engine is exactly right and OBS's product surface is exactly wrong** for this market.

### 1.2 Terminology

Mission Capture renames OBS's core concepts. **This is a product decision with a real
engineering hazard attached — read §1.3.**

| Mission Capture term | OBS term | libobs type | Where it appears |
|---|---|---|---|
| **Layers** | *(none — new)* | — | The single tree panel that replaces the Scenes and Sources docks |
| **Canvas** | Scene | `obs_scene_t` | A top-level node in Layers |
| **Element** | Source / scene item | `obs_sceneitem_t` | A child node under a Canvas |
| **Overlay Template** | *(none — new)* | a hidden `obs_scene_t` | Managed in the Overlay Editor, not shown in Layers |
| **Job** | Scene collection | — | One inspection job or dive |
| **Rig** | Profile | — | Vessel/spread hardware configuration |

The Layers panel is a tree, two levels deep:

```
Layers
├── Canvas  "Pilot Cam"
│   ├── Element  Video Capture Device  (DeckLink SDI 1)
│   └── Element  Overlay               (Pipeline Banner)
├── Canvas  "Manip Cam"
│   ├── Element  Video Capture Device  (AVerMedia HDMI)
│   └── Element  Overlay               (Pipeline Banner)
└── Canvas  "Sonar"
    ├── Element  RTSP Camera           (rtsp://10.0.0.40/stream1)
    └── Element  Overlay               (Sonar Banner)
```

**Only three element types ship**: Video Capture Device, RTSP Camera, and Overlay. Every other
OBS source type is registered but hidden from the Add-Element menu, so it can be re-enabled
through a feature flag if a job ever demands it.

### 1.3 ⚠ The "Canvas" naming collision

**libobs already has a type called `obs_canvas_t`, and it is *not* what we are calling a Canvas.**

- **Mission Capture "Canvas"** = an OBS scene = `obs_scene_t`
- **libobs `obs_canvas_t`** = a render target with its own video mix — the thing we use to give
  each recording its own composited output ([architecture.md §3](architecture.md#3-multi-canvas-recording))

These will be in the same source files. A developer reading `MCCanvasRecorder` six months from
now must not have to guess which one is meant. The convention, enforced in review:

| Concept | Call it this in code, comments, and docs | Never call it |
|---|---|---|
| `obs_scene_t` (the user's Canvas) | `canvas`, `MCCanvas`, "Canvas" | "scene" in user-facing text |
| `obs_canvas_t` (the render target) | `renderTarget`, `MCRenderTarget`, "render target" / "video mix" | "canvas" |

Every `obs_canvas_t` variable is named `renderTarget*`. No exceptions. If this convention slips,
the multi-recording code becomes genuinely unreadable.

*If you'd rather avoid the collision entirely, "View" or "Channel" as the user-facing term would
remove it at the cost of a less evocative name. Your call — the plan proceeds with Canvas.*

### 1.4 What we keep from OBS

- `libobs` — the compositor, render graph, A/V sync clock, encoder abstraction
- Capture backends — DeckLink/SDI, DirectShow (AVerMedia and friends), Media Foundation
- Encoders — NVENC, QSV, AMF, x264, and the FFmpeg muxer
- Transforms, filters, and the preview transform gizmos
- Profiles and scene collections (renamed Rig and Job)
- The WHIP/WebRTC output (`plugins/obs-webrtc`) — see [Phase 9](phase-9-webrtc-streaming.md)
- The FFmpeg media source, which is what makes RTSP work

### 1.5 What we add (this plan)

| # | Feature | Phase |
|---|---|---|
| 1 | **Layers tree** — one panel, Canvas → Element, replacing the Scenes + Sources docks | [1](phase-1-shell-and-layers.md) |
| 2 | **RTSP camera element** — network cameras with low-latency defaults and automatic reconnect | [2](phase-2-video-elements.md) |
| 3 | **Overlay editor mode** — place text, images and live data fields; save as reusable templates; assign to many Canvases | [4](phase-4-overlay-editor.md) |
| 4 | **Real-time data configuration** — ingest strings from RS-232 / UDP / TCP, split them, name each field | [3](phase-3-data-core.md) + [5](phase-5-transports.md) |
| 5 | **Multi-canvas recording** — record any subset of Canvases simultaneously, each to its own file | [6](phase-6-multi-record.md) |
| 6 | **Clips** — capture short recordings of a Canvas *while* its main recording runs | [7](phase-7-secondary-capture.md) |
| 7 | **Snapshots** — one button freezes a still from every Canvas, previews them, saves with or without the overlay | [7](phase-7-secondary-capture.md) |
| 8 | **Sidecar data log** — timestamped CSV/JSONL written next to every recording | [8](phase-8-sidecar-log.md) |
| 9 | **WebRTC streaming to a private server** — WHIP output to your own endpoint | [9](phase-9-webrtc-streaming.md) |

### 1.6 What we take away

Hidden or build-disabled, not deleted (see [fork strategy](#3-fork-strategy)): streaming-platform
integration and OAuth, Studio Mode, replay buffer, virtual camera, browser source, VST, scripting,
the stinger/transition zoo, multitrack video, all source types outside the three element types,
and roughly two-thirds of the Settings dialog.

---

## 2. Confirmed decisions

| Decision | Choice | Consequence |
|---|---|---|
| **Product / author** | Mission Capture, by Cyberian Resources | Drives exe name, config path `%APPDATA%\Cyberian Resources\Mission Capture\`, installer, version resource |
| **Platform** | Windows only | Serial layer uses Win32 `CreateFile`/`SetCommState` directly. One CI runner. macOS/Linux plugin trees stay in-tree, unbuilt |
| **Terminology** | Scene → **Canvas**, source → **Element**, panel → **Layers** | See the collision warning in §1.3 |
| **UI structure** | One **Layers** tree panel, Canvas → Element | Replaces the two flat list widgets with a `QTreeView` + custom model. The largest single UI change in the plan |
| **Element types** | Video Capture Device, RTSP Camera, Overlay — only | Everything else hidden behind a flag |
| **Overlay model** | Reusable templates assigned to Canvases | Needs a template store + assignment layer. Build the banner once, push it to all eight cameras |
| **Recording outputs** | Overlay burned in. **No clean copy** | One file per recorded Canvas. Settled — see §5.2 for what this means for the log |
| **Container** | **MKV** | Survives a hard kill or power loss with a playable file. Remux to MP4 stays a manual post-step, never automatic |
| **Secondary recording** | Clips of a Canvas *while* its main recording runs | Costs **zero extra encoder sessions** — the clip shares the primary's encoder. See §5.2 and [Phase 7](phase-7-secondary-capture.md) |
| **Snapshots** | One button, a still from every Canvas, saved with the overlay, without it, or both | The clean *still* is cheap for exactly the reason the clean *video* wasn't — one render pass, no encoder. See §5.2 |
| **Data log contents** | RS-232 / TCP / UDP device data only | No app-event or system telemetry in the CSV. Keeps the deliverable clean |
| **Fork strategy** | Stay mergeable — additive + feature flags | New code in new directories; upstream touched only at a documented list of seams |
| **Deployment** | Single machine | The obs-websocket vendor API stays a test hook rather than becoming a product feature |
| **Capture hardware** | Blackmagic (DeckLink) + AVerMedia (DirectShow), others possible | Both backends stay enabled; Phase 2 tests against both |
| **GPU / encoders** | **AMD (AMF) or NVIDIA (NVENC)** | These two are the design target. QSV and x264 remain as fallbacks but are not benchmarked or tuned |
| **Serial** | Mostly RS-232, **receive-only** | Phase 5 targets RS-232 semantics first (DTR/RTS, framing/parity errors). The app never transmits to the survey system |
| **Streaming** | WebRTC to a private server, via WHIP | Last phase, explicitly non-critical. See §6 for a correction on ICE |

---

## 3. Fork strategy

> **New behaviour goes in new files. Upstream files are touched only at named seams, and every
> seam is a one-line call into our code.**

This is what makes `git merge upstream/master` survivable at OBS's release cadence. Concretely:

- **New source types** → new plugin directories: `plugins/mc-data`, `plugins/mc-overlay`,
  `plugins/mc-rtsp`
- **New shared logic** → `shared/mc-data/` (a Qt-free static lib both the plugin and the frontend link)
- **New frontend UI** → `frontend/subsea/` (the Layers tree, docks, dialogs, controllers)
- **Removals** → a feature-flag check (`MCFeatures::enabled(...)`) or a CMake `option(... OFF)`.
  **Never** delete an upstream `.cpp`, never rip a block out of `OBSBasic.cpp`
- **Renaming** → locale strings only. The C++ keeps saying `scene` and `sceneitem`; only the
  displayed text says Canvas and Element. Renaming upstream identifiers would conflict on every
  merge, forever
- **Seams** → all registered in [architecture.md §7](architecture.md#8-upstream-seams). If that
  list grows past ~30 entries, the mergeability promise is breaking

### 3.1 Legal notes (act on these in Phase 0)

- OBS Studio is **GPLv2**. A distributed fork must ship its complete corresponding source. Plan a
  public source mirror or a written offer in the installer, and keep `COPYING` intact.
- **"OBS" and the OBS logo are trademarks of the OBS Project.** Mission Capture must not be
  branded as OBS or use its logo. Rebranding is a Phase 0 task, not polish.
- Bundled third-party components carry their own terms — the DeckLink SDK and NVENC headers in
  particular have redistribution conditions worth re-reading before first release.

---

## 4. Roadmap

Phases are ordered by **dependency**, not by the order the features were requested. See §4.1.

| Phase | Title | Delivers | Rough effort |
|---|---|---|---|
| [0](phase-0-foundation.md) | **Foundation** | Fork hygiene, branding, Windows-only build, feature flags, test harness, CI, hardware benchmark | 1–2 weeks |
| [1](phase-1-shell-and-layers.md) | **Shell & Layers tree** | Terminology, the Layers tree panel, declutter, defaults, New Job wizard | 4–6 weeks |
| [2](phase-2-video-elements.md) | **Video elements** | Video Capture Device (DeckLink + DirectShow) and RTSP Camera elements | 2–3 weeks |
| [3](phase-3-data-core.md) | **Data core** | Channel registry, parser engine, simulator transport — headless, unit-tested | 2–3 weeks |
| [4](phase-4-overlay-editor.md) | **Overlay editor** | Overlay Edit mode, templates, text/image/data-field items, assignment | 4–6 weeks |
| [5](phase-5-transports.md) | **Transports & config UI** | RS-232 / UDP / TCP, device manager, parse-a-sample-line wizard | 3–4 weeks |
| [6](phase-6-multi-record.md) | **Multi-canvas recording** | Per-Canvas recorders, Record panel, resource guard, auto-split | 3–5 weeks |
| [7](phase-7-secondary-capture.md) | **Secondary capture** | Clips during a running recording, plus all-Canvas snapshots with/without overlay | 4–5 weeks |
| [8](phase-8-sidecar-log.md) | **Sidecar log & hardening** | CSV log, run manifest, sync metadata, soak testing | 2–3 weeks |
| [9](phase-9-webrtc-streaming.md) | **WebRTC streaming** | WHIP output to a private server | 2–3 weeks |

Total: roughly **7–10 months** of one focused developer. Treat these as planning sighting-shots.
Phases 1, 4 and 6 carry the real unknowns, each called out in its own doc.

**A shippable product exists after Phase 7.** Phases 8 and 9 are additive. If schedule pressure
arrives, cut Phase 9 first (you said it isn't crucial) and Phase 8's soak second. Phase 7 is not a
good candidate for cutting — clipping and snapshotting during a dive are workflows your users
already have, and both are cheap because they ride on machinery Phases 4 and 6 already built.

### 4.1 Sequencing notes

**Why the Layers tree is Phase 1 and large.** It replaces `SceneTree` (a `QListWidget`) and
`SourceTree` (a `QListView` with its own model) with one `QTreeView` over a new two-level model,
and touches everything downstream: drag-and-drop, multi-select, context menus, the preview's
selection sync, and the visibility/lock toggles. Doing it first means Phases 2–8 build on the
final structure. Doing it late means rebuilding UI three times.

**Why the data core comes before the overlay editor.** The editor's headline item is the live
data field, which is meaningless without named channels. Phase 3 deliberately ships **no hardware
I/O** — just the registry, the parsers, and a *simulator* transport — so Phase 4 can bind
`{DEPTH}` on day one and be tested without a serial cable. Real RS-232/UDP/TCP is Phase 5. If you
want a demo-able editor sooner, Phase 4 can start in parallel once Phase 3 task 3.1 lands.

**Why video elements come before the overlay editor.** The overlay editor renders the selected
Canvas underneath as a positioning reference. Doing real overlay design over a colour bar rather
than actual subsea video is how you end up with a banner that's unreadable on dark water.

**Why clips are Phase 7 and not part of Phase 6.** They share almost all of Phase 6's machinery, so
merging them would look efficient. But Phase 6 already carries the plan's biggest risk (encoder
limits) and folding a second feature into it makes that risk harder to isolate. Phase 7 also has
its own UI surface, its own naming rules, and one genuinely new component (the packet ring). Keep
them separate; Phase 7 will move fast because Phase 6 did the hard part.

**Snapshots could ship earlier than Phase 7.** They need Canvases, video elements and overlays —
nothing from the recording machinery — so they could run straight after
[Phase 4](phase-4-overlay-editor.md). They sit in Phase 7 because they share a UI area, hotkey
philosophy, and likely event-marking tie-in with clips, and designing that surface once is better
than twice. If you want snapshots in an operator's hands sooner, say so and tasks 7.7–7.11 move
forward; nothing else has to change.

---

## 5. Principal risks

### 5.1 Concurrent encoder sessions (Phase 6) — **high**

Recording 4 Canvases means 4 concurrent video encodes. NVIDIA has historically capped concurrent
NVENC sessions on GeForce cards (the limit has been raised over time by driver updates;
Quadro/RTX-pro cards are unrestricted). AMD AMF has no comparable published session cap but is
throughput-limited in practice, and behaviour varies by card generation and driver. Since the
target hardware is AMD or NVIDIA, these two families are what the guard must model.

**Mitigation:** Phase 6 task 6.6 builds a pre-flight resource guard. **Phase 0 task 0.7 benchmarks
the actual target hardware** so the guard's limits are measured, not guessed. This is the single
cheapest risk to retire early — and it now needs to cover both an AMD box and an NVIDIA box, since
they will not behave the same.

**Note:** secondary recordings (clips) do *not* add to this count — they share the primary
recording's encoder. See [architecture.md §3.6](architecture.md#36-secondary-recordings-clips).

### 5.2 What the sidecar log can and cannot do — **informational, settled**

Confirmed: overlays are burned in, there is no clean copy, and the log carries RS-232/TCP/UDP
device data only. That resolves what was previously an open risk. Recording what it means, so
nobody is surprised later:

- ✅ **Resync** — map any data row to a frame in the recording, for reporting or a separate
  deliverable. Fully supported; Phase 8 task 8.3's clock-offset block exists for exactly this.
- ✅ **Reporting and audit** — the CSV plus manifest is a complete record of what the instruments
  said during the dive.
- ❌ **Re-burn a different overlay onto a recorded file** — impossible once the pixels are baked.
  A later pass could only paint over the existing banner.

A free hedge worth adopting as a template-design convention: **confine burn-in to a lower-third or
corner band.** If re-burning ever becomes a requirement, a future pass can cleanly overwrite that
region rather than fighting text scattered across the frame. Costs nothing to do now.

**Stills are the exception, and they're free.** [Snapshots](phase-7-secondary-capture.md) *can* be
saved overlay-free, because a clean still needs one extra render pass and no encoder — whereas a
clean video needs a second render target and encoder running for the whole dive. Same conceptual
problem, two orders of magnitude apart in cost. So: clean video is out, clean stills are in.

### 5.3 The Layers tree is a bigger change than it looks (Phase 1) — **medium-high**

`OBSBasic_Scenes.cpp` (1026 lines) and `OBSBasic_SceneItems.cpp` (1433 lines) both assume two
separate flat widgets. A unified tree means reworking selection sync with the preview,
drag-and-drop between Canvases, group handling, and the reordering logic — while keeping those
upstream files nearly untouched for mergeability. Phase 1 handles this by putting the new tree
entirely in `frontend/subsea/` and driving upstream logic through its existing public methods.
**Expect this to be the phase that overruns.**

### 5.4 RTSP reliability (Phase 2) — **medium**

RTSP works today through the FFmpeg media source, but the defaults are tuned for file playback,
not live cameras: 2 MB of buffering, UDP transport, a 10-second reconnect delay. Getting
sub-second, reliably-reconnecting RTSP is a matter of the right `ffmpeg_options` plus reconnect
UX — not new decoding code. The risk is camera-specific quirks; test against the actual cameras.

### 5.5 Text re-rasterisation cost (Phase 4) — **medium**

The GDI+ text source rebuilds its bitmap and re-uploads a texture on every text change
(`plugins/obs-text/gdiplus/obs-text.cpp`, `RenderText()`). Twelve data fields at 50 Hz means 600
texture uploads a second. Phase 4 task 4.7 handles this with dirty-checking and a configurable
rate (default 4 Hz).

### 5.6 Upstream merge drift — **medium, slow-burning**

Every seam is a future merge conflict. Mitigated by discipline (§3), the locale-only renaming
rule, and a monthly merge cadence set up in Phase 0.

### 5.7 Clip start latency and the packet ring (Phase 7) — **medium**

An output joining a live encoder can only begin at a keyframe, so a naive clip would start up to a
full GOP *after* the operator pressed the button — the wrong direction for capturing something you
just saw. There is no libobs API to force a keyframe on demand, and adding one would mean changing
the encoder interface upstream.

Phase 7 solves this with a ring buffer of encoded packets so the clip starts *before* the press
instead. The cost is bounded memory (~190 MB per Canvas at 50 Mbit/s and 30 s preroll) and one
genuinely new component. Full reasoning in
[architecture.md §3.6](architecture.md#36-secondary-recordings-clips).

### 5.8 Data/video timestamp alignment (Phase 8) — **medium**

RS-232 data arrives with unknown latency; the video pipeline has its own buffering. Phase 8 task
8.3 records an explicit clock-offset block so alignment is measurable and correctable rather than
silently wrong.

---

## 6. On WebRTC — a partial correction

You said: *"stream to my private server (real time), which is using WebRTC, which later I setup
myself with the ICE framework."* Mostly right, with one correction.

**Right:** WebRTC is the correct choice for sub-second latency to a private server, and **OBS
already ships it.** `plugins/obs-webrtc/` implements a **WHIP** output (WebRTC-HTTP Ingestion
Protocol) built on libdatachannel, advertising `h264;hevc;av1` video and Opus audio. Phase 8 is
mostly configuration and UI, not protocol work.

**The correction:** ICE is not a framework you set up alongside WebRTC — it is *part of* WebRTC,
the mechanism that discovers a working network path between two peers. libdatachannel already
implements it, and OBS's WHIP client even parses ICE servers out of the WHIP response's `Link`
header (`whip-output.cpp:342`). What you actually stand up server-side is:

1. **A WHIP endpoint** to receive the stream. [MediaMTX](https://github.com/bluenviron/mediamtx)
   is the usual first choice — single Go binary, speaks WHIP in and WebRTC/HLS/RTSP out. Janus,
   LiveKit, and Cloudflare Stream are alternatives.
2. **STUN, and possibly TURN**, only if the stream crosses a NAT. On a vessel LAN where the
   viewer and the DVR are on the same subnet, you may need neither.

**Worth considering before committing:** if the link is a satellite or 4G connection rather than a
LAN, **SRT or RIST may serve you better than WebRTC.** Both are already supported by OBS, both are
built for lossy long-haul links with retransmission, and both tolerate multi-hundred-millisecond
jitter that WebRTC handles by degrading quality. WebRTC wins when the viewer is a plain browser
with no plugin. MediaMTX will happily take SRT in and serve WebRTC out, which gets you both.

Phase 9 plans WHIP as the default and keeps SRT/RIST available, since OBS supports them already.

---

## 7. Documents in this set

| Document | Contents |
|---|---|
| [PROGRESS.md](PROGRESS.md) | **Live status** — task tracker, open-items register, upstream merge log. The only document that changes as work happens |
| [architecture.md](architecture.md) | How each feature maps onto real OBS internals, with file and API references. **Read before any phase doc.** |
| [phase-0-foundation.md](phase-0-foundation.md) | Fork hygiene, branding, build slimming, feature flags, test harness, hardware benchmark |
| [phase-1-shell-and-layers.md](phase-1-shell-and-layers.md) | Terminology, the Layers tree, declutter, defaults, New Job wizard |
| [phase-2-video-elements.md](phase-2-video-elements.md) | Capture device and RTSP camera elements |
| [phase-3-data-core.md](phase-3-data-core.md) | Channel registry, parsers, simulator |
| [phase-4-overlay-editor.md](phase-4-overlay-editor.md) | Overlay Edit mode and templates |
| [phase-5-transports.md](phase-5-transports.md) | RS-232 / UDP / TCP and the config UI |
| [phase-6-multi-record.md](phase-6-multi-record.md) | Per-Canvas recording |
| [phase-7-secondary-capture.md](phase-7-secondary-capture.md) | Clips during a running recording, and all-Canvas snapshots |
| [phase-8-sidecar-log.md](phase-8-sidecar-log.md) | Data logging and hardening |
| [phase-9-webrtc-streaming.md](phase-9-webrtc-streaming.md) | WHIP output to a private server |
| [testing.md](testing.md) | Test strategy, tooling prerequisites, and the full text of every test script |

---

## 8. Deferred decisions — remind the user at these points

You asked to be reminded rather than decide now. These are checkpoints, not open questions.

| # | Decision | Ask again at | Why it matters then |
|---|---|---|---|
| 1 | **Client deliverable format** — is there an IMCA or client-specific spec the CSV, filenames, and manifest must match? | **Start of [Phase 8](phase-8-sidecar-log.md)** (task 8.2 fixes the CSV column order) | Retrofitting a column convention after the first job is painful |
| 2 | **Data rate and channel count** — how many channels, at what Hz? | **Start of [Phase 3](phase-3-data-core.md)** (sizes the registry) and again at **[Phase 5](phase-5-transports.md)** (sizes the transports) | 20 channels at 1 Hz and 200 at 50 Hz are different engineering problems |
| 3 | **Event marking** — a hotkey that stamps an event into the data log ("anode", "freespan", "damage"), what the event vocabulary is, and whether it should also trigger a clip | **Start of [Phase 8](phase-8-sidecar-log.md)**, discussed together with #1 | Agreed in principle, deferred for detail. It touches the CSV schema, so it must be settled with the column order, not after |
| 4 | **Clip preroll default** — how many seconds before the button press should a clip include? | **Start of [Phase 7](phase-7-secondary-capture.md)** | Sizes the packet ring's memory budget. My guess is 10 s; it may want to be 30 |

All four are written as callout boxes at the top of the phases concerned, so they surface when the
answer is actually needed rather than being forgotten.

**Event marking and clips are obviously related** — "I saw an anode" is plausibly one keypress that
both marks the log and starts a clip. Worth designing them together when we get to Phase 8, which
is another reason to keep #3 on that checkpoint rather than resolving it early in isolation.

### Closed since the last revision

- ~~Clean/overlay-free copy~~ — not wanted. Removed from the plan; Phase 6 is simpler for it.
- ~~Sending data back to the survey system~~ — receive-only, confirmed.
- ~~The Canvas naming collision~~ — acknowledged and accepted. The `renderTarget` convention in
  [architecture.md §0](architecture.md#0-naming-before-anything-else) is what keeps it manageable;
  no further action needed unless it bites in review.
