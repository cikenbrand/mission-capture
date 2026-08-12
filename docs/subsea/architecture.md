# Architecture

How the features map onto OBS internals. Every claim here was checked against the current tree;
file and line references are from the fork point (`14e3dae77`) and may drift.

---

## 0. Naming, before anything else

Read [README §1.2–1.3](README.md#12-terminology) first. The short version, because it governs
every line below:

| Mission Capture UI | libobs type | Identifier convention in our code |
|---|---|---|
| **Canvas** | `obs_scene_t` | `canvas`, `MCCanvas` |
| **Element** | `obs_sceneitem_t` | `element`, `MCElement` |
| **Layers** | *(the tree widget)* | `MCLayersTree` |
| *(not user-visible)* | `obs_canvas_t` — a render target with its own video mix | **`renderTarget`, `MCRenderTarget` — never `canvas`** |

**Upstream identifiers are never renamed.** `obs_scene_t` stays `obs_scene_t`; `OBSBasic`'s
methods keep saying `Scene`. Only *displayed strings* say Canvas and Element, and those live in
locale files. Renaming upstream symbols would conflict on every merge forever, which defeats the
entire fork strategy.

---

## 1. Directory layout for new code

```
shared/mc-data/                 Static lib: channel registry, parsers, transports (no Qt, no UI)
  include/mc-data/
    mc-channel.h                Channel value/quality/timestamp types + registry C API
    mc-parser.h                 Frame assembly + parse strategies
    mc-transport.h              Transport interface + factory
  src/
    mc-channel.c
    mc-parser.c
    mc-transport-sim.c          Phase 3
    mc-transport-serial.c       Phase 5 (Win32, RS-232)
    mc-transport-udp.c          Phase 5
    mc-transport-tcp.c          Phase 5
    mc-log-writer.c             Phase 8

plugins/mc-rtsp/                obs-module: the RTSP Camera element              Phase 2
  mc-rtsp-source.c
plugins/mc-data/                obs-module: owns transport lifetimes             Phase 3
  mc-data-plugin.c
  mc-ws-vendor.cpp              obs-websocket vendor API (test hook)
plugins/mc-overlay/             obs-module: the mc_text data-bound source        Phase 4
  mc-text-source.cpp
  mc-format.c                   Format-string engine ("{CP:0.00} mV")

frontend/subsea/                Qt UI, links shared/mc-data
  MCFeatures.{hpp,cpp}          Feature-flag system                        Phase 0
  MCLayersTree.{hpp,cpp}        The Layers QTreeView                       Phase 1
  MCLayersModel.{hpp,cpp}       Two-level model over scenes + scene items  Phase 1
  MCLayersDelegate.{hpp,cpp}    Row painting, inline rename, toggles       Phase 1
  MCJobWizard.{hpp,cpp}         New Job / New Rig                          Phase 1
  MCAddElementDialog.{hpp,cpp}  The three-choice element picker            Phase 2
  MCOverlayMode.{hpp,cpp}       Overlay Edit mode controller               Phase 4
  MCOverlayTemplate.{hpp,cpp}   Template model + persistence               Phase 4
  MCOverlayDock.{hpp,cpp}       Overlay item list + add-item toolbar       Phase 4
  MCDataDeviceDialog.{hpp,cpp}  Device manager + raw monitor               Phase 5
  MCParserWizard.{hpp,cpp}      Sample line → named columns                Phase 5
  MCRenderTarget.{hpp,cpp}      RAII wrapper over obs_canvas_t             Phase 6
  MCCanvasRecorder.{hpp,cpp}    One render target + encoders + output      Phase 6
  MCRecordingManager.{hpp,cpp}  Owns N recorders                           Phase 6
  MCRecordDock.{hpp,cpp}        Per-Canvas record toggles + status         Phase 6
  MCPacketRing.{hpp,cpp}        Encoded-packet ring for clip preroll       Phase 7
  MCClipRecorder.{hpp,cpp}      Secondary recording on a live encoder      Phase 7
  MCSnapshotSet.{hpp,cpp}       All-Canvas still capture, both variants    Phase 7
  MCSnapshotDialog.{hpp,cpp}    Preview gallery and save controls          Phase 7
  MCStreamPanel.{hpp,cpp}       WHIP endpoint config + status              Phase 9

test/cmocka/                    Unit tests (existing harness, §8)
tools/subsea-tests/             PowerShell integration harness (new)
```

`shared/mc-data` being Qt-free and frontend-free keeps the parser and registry unit-testable
under cmocka without spinning up an app.

---

## 2. The Layers tree

### 2.1 What it replaces

| Today | Type | Lines |
|---|---|---|
| `frontend/components/SceneTree.{hpp,cpp}` | `QListWidget` — flat list of scenes | 247 |
| `frontend/components/SourceTree.{hpp,cpp}` | `QListView` + `SourceTreeModel` + `SourceTreeItem` | 662 + 450 + 582 |

Two flat widgets, each bound to its own dock, with the Sources list repopulated whenever the
current scene changes. Mission Capture shows both levels at once in one `QTreeView`.

### 2.2 Model design

`MCLayersModel` is a `QAbstractItemModel` with exactly two levels:

```
row (invalid parent)  → Canvas    internalPointer = MCCanvasNode*   (holds an OBSWeakSource)
row (Canvas parent)   → Element   internalPointer = MCElementNode*  (holds an obs_sceneitem_t*)
```

- **Canvas rows** come from enumerating the main render target's scenes. Order is user-defined and
  persisted in the Job (scene collection) file, exactly as the Scenes list order is today.
- **Element rows** come from `obs_scene_enum_items()`. libobs stores scene items **bottom-first**;
  the tree displays them top-first, so the model reverses the index. Getting this backwards is the
  most common bug in this kind of widget — assert it in a test.
- **Signals to track:** `source_create`/`source_remove`/`source_rename` on the global handler for
  Canvas rows, and `item_add`/`item_remove`/`item_reorder`/`item_visible`/`item_locked` on each
  scene's handler for Element rows. All must be marshalled onto the Qt thread with
  `QMetaObject::invokeMethod` — libobs signals fire on arbitrary threads.
- **Groups:** OBS scene items can be groups, which would imply a third level. Since only three
  element types ship, **groups are disabled** in Phase 1. If they ever come back, the model needs a
  real recursive tree rather than a fixed two-level one — worth keeping the node structs
  polymorphic so that door stays open.

### 2.3 Interaction

| Interaction | Behaviour |
|---|---|
| Select a Canvas | Makes it the program Canvas (what today's Scenes list does) |
| Select an Element | Selects it in the preview for transform; also selects its parent Canvas |
| Drag Element within a Canvas | Reorder (Z-order) |
| Drag Element to another Canvas | Move, or copy with Ctrl. **Reuses `obs_sceneitem` duplication semantics; must handle the shared-vs-copy source question explicitly** |
| Drag Canvas | Reorder Canvases |
| Double-click | Rename inline for a Canvas; open properties for an Element; open the Overlay Editor for an Overlay Element |
| Eye / lock icons | `obs_sceneitem_set_visible` / `set_locked` on Element rows; for Canvas rows, all children at once |
| Right-click | Context menu, scoped to node type |

The tree lives entirely in `frontend/subsea/` and drives upstream behaviour through `OBSBasic`'s
existing public methods (`SetCurrentScene`, `AddScene`, `RemoveScene`, and the scene-item helpers
in `OBSBasic_SceneItems.cpp`). **It does not reimplement them.** That is what keeps
`OBSBasic_Scenes.cpp` and `OBSBasic_SceneItems.cpp` — 2459 lines between them — nearly untouched
and mergeable.

### 2.4 Preview-selection sync — resolved without a seam

The plan assumed `OBSBasicPreview` would have to be modified to notify the tree, and called that
the most merge-fragile change in the fork. It turned out to be unnecessary.

The preview does not push selection to a widget. It calls `obs_sceneitem_select()`, and libobs
emits `item_select` / `item_deselect` on the scene. Upstream's `SourceTree` listens to exactly
that. So the Layers tree listens too, and **libobs is the single source of truth** that the
preview, hotkeys and the tree all read from and write to:

```
preview click ──▶ obs_sceneitem_select() ──▶ item_select signal ──▶ MCLayersModel ──▶ tree
tree click    ──▶ obs_sceneitem_select() ──▶ item_select signal ──▶ (preview reads the same state)
```

Seam #11 is therefore struck from §8: no upstream file is touched for selection at all.

Two things still need care:

- **The loop is real.** Both directions run through the same signal, so a re-entrancy guard is
  mandatory, not defensive. `MCLayersTree::settingSelection_` wraps every path.
- **Program-Canvas changes are not a libobs signal.** Nothing announces "the program Canvas
  changed", so `OBS_FRONTEND_EVENT_SCENE_CHANGED` drives the marker repaint. Without it the marker
  only moves when the dock repaints for some other reason.

Switching Canvas likewise uses `obs_frontend_set_current_scene()` rather than
`OBSBasic::SetCurrentScene()`, which is private — the public frontend API does the same job
without widening an upstream header.

---

## 3. Multi-canvas recording

*(User-facing name: "record multiple Canvases". Internally: one `obs_canvas_t` **render target**
per recorded scene.)*

### 3.1 What exists today

`BasicOutputHandler` (`frontend/utility/BasicOutputHandler.hpp:20`) has exactly **one**
`fileOutput`. Bending it into an N-recording shape would mean heavy edits to a file that changes
upstream constantly — precisely what the fork strategy forbids. So Phase 6 adds a **parallel,
additive** path and leaves it alone. The main Record button keeps working as it does today.

### 3.2 The per-Canvas recorder

The pattern already exists in-tree: the virtual camera holds `virtualCamView` (an `obs_view_t`) and
`virtualCamVideo` (a `video_t*`) at `BasicOutputHandler.hpp:42-45`, renders a chosen scene into
that private view, and attaches an output. `obs_canvas_t` is the modern wrapper around exactly
that mechanism.

```cpp
class MCCanvasRecorder {
    MCRenderTarget renderTarget;  // obs_canvas_create_private(name, &ovi, ACTIVATE | EPHEMERAL)
    OBSEncoder     venc;          // bound to obs_canvas_get_video(renderTarget)
    OBSEncoder     aenc;          // shared across recorders — see §3.3
    OBSOutput      output;        // "ffmpeg_muxer"
    OBSWeakSource  canvas;        // the obs_scene_t being recorded
};
```

Start sequence:

```cpp
obs_canvas_set_channel(renderTarget, 0, canvasSource);   // render only this Canvas
obs_encoder_set_video(venc, obs_canvas_get_video(renderTarget));
obs_encoder_set_audio(aenc, obs_get_audio());
obs_output_set_video_encoder(output, venc);
obs_output_set_audio_encoder(output, aenc, 0);
obs_output_start(output);
```

The overlay is an Element *inside* the Canvas, so this render target composites it and it lands
burned into the file. No extra work.

Canvas flags matter: `ACTIVATE` maps to `MAIN_VIEW` in `obs_view_init`
(`libobs/obs-canvas.c`), which is what keeps a Canvas's sources active while it is not the program
Canvas. `EPHEMERAL` keeps the render target out of the saved Job file. **Verify the active-while-
not-program behaviour empirically in Phase 6 — the whole feature rests on it.**

### 3.3 Can one audio encoder feed several outputs?

Yes. `struct obs_encoder` holds a `DARRAY` of outputs and `obs_encoder_add_output()`
(`libobs/obs-encoder.c:1868`) pushes onto it — fan-out is anticipated by the design.
`obs_output_set_audio_encoder()` (`libobs/obs-output.c`) only refuses when *the output* is already
active, not when the encoder is.

**Caveat:** every recorder must therefore be attached before any of them starts, or a
late-joining recorder needs its own audio encoder. Phase 6 task 6.3 handles this. **Verify
empirically before building on it** — it is load-bearing, and the fallback (one audio encoder per
recorder) costs a little CPU and nothing else.

### 3.4 No clean copy — settled

Every recording has its overlay burned in, and there is no overlay-free variant. This was
confirmed explicitly, and it removes what would otherwise have been the plan's most expensive
optional feature (a duplicate scene per Canvas, doubling encoder load).

Consequence to keep in mind: the sidecar log supports **re-syncing** data to video, not
**re-burning** a different overlay onto an existing file. See
[README §5.2](README.md#52-what-the-sidecar-log-can-and-cannot-do--informational-settled).

### 3.5 Resource guard

Before starting N recorders, count requested concurrent video encodes (N, plus 1 for the main
recording, plus 1 if streaming) and compare against a per-encoder-family limit measured in Phase 0
task 0.7. **Secondary recordings do not add to this count** — see §3.6. Warn, then verify: if
`obs_output_start()` fails for recorder *k*, roll the whole set back. An inspection engineer who
believes four cameras are recording and finds three has lost footage that cannot be re-shot.

Only two encoder families matter on the target hardware: **NVENC and AMF**. QSV and x264 stay
available as fallbacks but are not the design target.

### 3.6 Secondary recordings (clips)

An operator recording the whole dive also wants short clips of the interesting bits, started and
stopped while the main recording continues. Architecturally this is a second `ffmpeg_muxer` output
on the **same Canvas, sharing the same encoders** — and libobs supports exactly that:

| Mechanism | Where | What it gives us |
|---|---|---|
| `obs_encoder_start_internal()` pushes a callback onto a running encoder; only the first callback calls `add_connection()` | `libobs/obs-encoder.c` | An output can join a live encoder |
| `send_first_video_packet()` waits for the next keyframe and prepends SEI | `libobs/obs-encoder.c:1326` | The joining output's file is independently decodable |
| `obs_encoder_add_output()` maintains a DARRAY of outputs | `libobs/obs-encoder.c:1868` | Fan-out is by design, not a hack |

**So a clip costs zero additional encoder sessions.** On consumer AMD/NVIDIA hardware with a
session ceiling, that is the difference between the feature being free and being unaffordable.

#### The keyframe problem, and the packet ring

An output joining mid-stream starts at the next keyframe — up to a full GOP after the button
press. For inspection that is the wrong direction: by the time a human reacts, the event has
already happened.

**There is no libobs API to force a keyframe on demand.** `forceIDR` exists inside both encoders
(`plugins/obs-nvenc/nvenc.c:119`, `plugins/obs-ffmpeg/texture-amf.cpp:747`) but only on
reconfigure, and exposing it would mean changing the libobs encoder interface — a merge liability
not worth taking for this.

The design inverts it instead: keep a **ring buffer of encoded packets** per Canvas covering the
last N seconds, and start the clip from the last keyframe at or before `now − preroll`. The clip
then begins *early*, never late, and the operator gets the run-up to the event for free. Same
machinery as OBS's replay buffer, but followed by continuous recording rather than a fixed-length
save.

The cost is memory: at 50 Mbit/s, 30 s of preroll is roughly 190 MB per Canvas. Bound it, surface
it, and refuse configurations that would exhaust RAM across many Canvases.

#### Container choice

**MKV is the primary format**, which suits this well: it tolerates an abrupt end (a killed process
or a power loss leaves a playable file), and it has no MP4-style moov-atom-at-the-end problem. Clip
files inherit the same container. Remux to MP4 stays available as an explicit post-step, never
automatic.

### 3.7 Snapshots

One button freezes a still from every Canvas at the same instant, previews them, and saves each
with the overlay burned in, without it, or both.

#### What already exists

`frontend/utility/ScreenshotObj.{hpp,cpp}` is the whole capture pipeline — render to a
`gs_texrender`, stage, GPU readback, encode to file — and it is shaped conveniently for this:

| Capability | Why it matters here |
|---|---|
| Constructor takes an `obs_source_t*` | Point it at a Canvas's scene source directly |
| `setSaveToFile(false)` + `imageReady(QImage)` signal | Get an in-memory image for the gallery without touching disk |
| `setSize()` | Capture at a size other than the source's |
| One stage per tick via `obs_add_tick_callback` | N instances created together **render in the same tick**, then stagger their readbacks — simultaneity without a graphics-thread spike |

`OBSBasic::Screenshot()` (`frontend/widgets/OBSBasic_Screenshots.cpp:24`) is the existing call site.

#### The clean variant

Capturing a Canvas *with* its overlay is just capturing the scene. *Without* it is the real problem:
`obs_sceneitem_set_visible()` is a property of the item, shared by every render target drawing that
scene. Hiding the overlay to take a clean still would hide it in the program output and in any
recording running at that moment.

| Approach | Mechanism | Verdict |
|---|---|---|
| **(A) Private duplicate** | `obs_scene_duplicate(scene, name, OBS_SCENE_DUP_PRIVATE_REFS)` (`libobs/obs.h:1677`) — a private scene holding *references* to the same sources, so a capture card is referenced rather than re-opened and the duplicate renders the same live frames. Remove `mc_overlay`-flagged items, render, destroy | **Recommended** |
| **(B) Atomic hide/render/unhide** | Wrap the whole thing in one `obs_queue_task(OBS_TASK_GRAPHICS, ..., wait = true)` (`libobs/obs.h:932`) so no video mix renders in between | Cheaper, but its failure mode is one frame of a client's recorded deliverable missing its overlay — nearly invisible in testing, mortifying in a deliverable |

Take (A). It cannot produce (B)'s failure mode at all.

#### Why a clean *still* is cheap when a clean *video* wasn't

The clean-copy video variant was rejected (§3.4) because it needs a second render target and a
second encoder running for the whole dive. A clean still needs one render pass and no encoder. Same
conceptual problem, two orders of magnitude apart in cost — which is why one is out of scope and the
other is a Phase 7 task.

---

## 4. Video elements

### 4.1 Video Capture Device

Two backends, both already in-tree and both staying enabled:

| Hardware | Plugin | Source id |
|---|---|---|
| Blackmagic DeckLink / UltraStudio | `plugins/decklink` | `decklink-input` |
| AVerMedia and other UVC/DirectShow devices | `plugins/win-dshow` | `dshow_input` |

Phase 2's work is **not** new capture code. It is:

- One unified "Video Capture Device" element that presents both backends behind one picker, so the
  user chooses a *camera*, not a *plugin*
- A simplified property panel: device, resolution/FPS, colour space, audio — and nothing else
  visible by default
- Device-loss handling: a card unplugged mid-dive must show a clear banner and reconnect when it
  returns, not silently render black
- Verified behaviour against both backends, including DeckLink's auto input-format detection

### 4.2 RTSP Camera

**RTSP already works** through the FFmpeg media source (`plugins/obs-ffmpeg/obs-ffmpeg-source.c`)
with `is_local_file = false`. What it lacks is live-camera-appropriate defaults. The relevant
settings, all already present:

| Setting | Default today | What an RTSP camera wants |
|---|---|---|
| `is_local_file` | `true` | `false` |
| `buffering_mb` | `2` (`:125`) | `0` — 2 MB of buffer is seconds of latency |
| `reconnect_delay_sec` | `10` (`:124`) | `1–2`, with backoff |
| `ffmpeg_options` | empty (`:211`) | `rtsp_transport=tcp fflags=nobuffer flags=low_delay` |
| `hw_decode` | off (`:190`) | on, where the GPU supports it |

`ffmpeg_options` is parsed with `av_dict_parse_string(&opts, ..., "=", " ", 0)`
(`shared/media-playback/media-playback/media.c:680`) and passed straight to the demuxer, so
arbitrary AVOptions are available without touching FFmpeg code.

So `plugins/mc-rtsp/` is a **thin wrapper**, not a new decoder: it owns a private `ffmpeg_source`,
sets these defaults, and presents a property panel of URL / username / password / transport /
latency preset. The value added is defaults, credential handling (keep passwords out of the URL in
logs), a visible connection-state banner, and reconnect that actually works.

**Why TCP transport by default:** RTP-over-UDP loses packets silently and produces smeared
macroblocks that look like a camera fault. On a vessel LAN, TCP's retransmission is nearly free and
the failure mode is honest.

---

## 5. Real-time data

### 5.1 Pipeline

```
  transport thread (1 per device)          registry (mutex-protected)     consumers (video thread)
 ┌──────────────────────────────┐        ┌───────────────────────────┐   ┌──────────────────────┐
 │ read bytes                   │        │ name → { double value,    │   │ mc_text  (on tick)   │
 │   ↓ frame assembler          │───────▶│          char*  text,     │──▶│ log writer (Phase 8) │
 │     (delimiter / length /    │        │          uint64 ts_ns,    │   │ websocket vendor API │
 │      sentinel / timeout)     │        │          quality }        │   │ device monitor UI    │
 │   ↓ parser                   │        └───────────────────────────┘   └──────────────────────┘
 │     (delimited / kv / regex  │
 │      / fixed-width / NMEA)   │
 │   ↓ transform                │
 │     (scale, offset, clamp,   │
 │      precision, unit)        │
 └──────────────────────────────┘
```

**Hard rule: no transport ever touches the graphics thread.** Consumers read a snapshot under a
short mutex. A dead RS-232 port must never stutter the preview or drop a recorded frame.

### 5.2 Configuration model

- **Device** — transport + settings + frame delimiter + parser + channel list
- **Parser** — strategy and config (separator string, regex, column widths, NMEA sentence filter)
- **Channel** — `{ name, index_or_key, type, unit, scale, offset, precision, min, max,
  stale_timeout_ms, stale_behaviour }`

`"1.031,5.132,6.122"` with a comma-delimited parser and channels `[CP@0, DEPTH@1, HDG@2]` yields
`CP=1.031`, `DEPTH=5.132`, `HDG=6.122`. Separators are configurable strings, and Phase 3 also ships
key/value, regex-with-named-groups, fixed-width, and NMEA-0183 parsers.

**Where config lives:** the **Rig** (profile) directory, not the Job (scene collection). A device
list is a property of the vessel spread; a Job is one dive. `data-devices.json` beside `basic.ini`.

### 5.3 RS-232 specifics

Since RS-232 is the common case, the serial transport targets its semantics directly: explicit
DTR/RTS assertion (some instruments won't transmit without DTR), hardware and software flow
control, and surfaced framing/parity/overrun counters — which are how you diagnose a wrong baud
rate in the field. Physically these usually arrive as USB-serial adapters, so hot-unplug recovery
matters as much as the protocol itself.

---

## 6. Overlay

### 6.1 Templates are scenes

**Do not build a new compositor.** An overlay template is structurally identical to an OBS scene:
an ordered list of items with transforms and visibility. Every gizmo the editor needs already
exists in `frontend/widgets/OBSBasicPreview.cpp`.

`obs_canvas_create()` accepts a `NULL` `obs_video_info`, in which case no video mix is created —
`libobs/obs-canvas.c` states this explicitly ("A canvas can be created without a mix"). That gives
a pure **container render target**: it holds scenes, participates in save/load, and costs nothing
to render.

```c
/* Created once at startup, persisted in the Job file */
mc_overlay_store = obs_canvas_create("MC Overlay Templates",
                                     NULL,        /* no video mix — container only */
                                     SCENE_REF);  /* holds refs to its scenes */
```

Each template is `obs_canvas_scene_create(mc_overlay_store, "Pipeline Banner")`. Persistence,
undo, naming, and reference counting come for free. Template scenes must **not** appear as
Canvases in the Layers tree — the tree enumerates the main render target only, so this should be
free; verify it, and verify they don't appear in element pickers either.

The frontend already wraps the machinery: `OBS::Canvas` in `frontend/utility/OBSCanvas.hpp`,
`OBSBasic::AddCanvas()` at `frontend/widgets/OBSBasic_Canvases.cpp:26`, save/load at
`OBSBasic_SceneCollections.cpp:901` and `:1271`, and `obs_frontend_add_canvas()` at
`frontend/api/obs-frontend-api.h:255-257`.

### 6.2 Assignment is an Element

Assigning template *T* to Canvas *C* = `obs_scene_add(C, T_source)`, then:

- `obs_sceneitem_set_locked(item, true)` — the pilot can't drag it off-screen
- `obs_sceneitem_set_order_position(item, top)` — overlays draw last
- Stamp `obs_sceneitem_get_private_settings(item)` with
  `{"mc_overlay": true, "mc_template_uuid": "..."}`

In the Layers tree this shows as an **Overlay Element** under the Canvas — which is exactly the
structure you described. Because it is a scene *reference*, editing the template updates every
assigned Canvas live: build once, apply to eight cameras.

### 6.3 The data-bound text source

Rather than reimplementing font rendering, `mc_text` **owns a private `text_gdiplus` source** and
pushes strings into it:

```
mc_text source
  ├── format string: "DEPTH {DEPTH:0.0} m   CP {CP:0.000} V   HDG {HDG:000.0}°"
  ├── on tick (rate-limited):
  │     resolve {NAME:fmt} tokens against the channel registry
  │     if resolved != last_resolved:            <- dirty check, README §5.5
  │         obs_data_set_string(settings, "text", resolved)
  │         obs_source_update(private_text_source, settings)
  └── render: obs_source_video_render(private_text_source)
```

Font, colour, outline, background, alignment, and every OBS filter come from the wrapped source,
surfaced via `obs_properties_add_group()`. The precedent for a text source that polls and
re-renders is in-tree: `read_from_file` mode in `plugins/obs-text/gdiplus/obs-text.cpp` (`tick`
around `:862`).

### 6.4 Overlay Edit mode

A mode toggle that repoints the main preview at the template scene, renders the selected Canvas
underneath at ~30% opacity as a positioning reference, swaps the Layers tree for the Overlay Items
list, and shows safe-area guides. `frontend/widgets/OBSBasic_StudioMode.cpp` does the same
preview-shows-something-else trick and is the reference implementation.

---

## 7. Streaming (WHIP / WebRTC)

`plugins/obs-webrtc/` already implements a complete WHIP output on libdatachannel:

- Registered as output id `whip_output` with flags
  `OBS_OUTPUT_ENCODED | OBS_OUTPUT_SERVICE | OBS_OUTPUT_MULTI_TRACK_AV` (`whip-output.cpp:733`)
- Advertises `h264;hevc;av1` video (HEVC gated on `ENABLE_HEVC`) and `opus` audio (`:737-742`)
- Parses ICE servers from the WHIP response `Link` header per the WHIP draft (`:342`)
- Supports simulcast layers via RID (`ConfigureVideoTrack`, `:165`)

So Phase 9 is configuration and UI, not protocol work:

1. A simplified stream panel: WHIP endpoint URL, bearer token, encoder, bitrate — no service picker
2. Which render target feeds the stream: the program Canvas, or a nominated one
3. Connection state, bitrate, and packet loss in the Health panel
4. Keep SRT and RIST reachable (already supported by `obs-ffmpeg`'s outputs) for links where
   WebRTC's loss-adaptation behaves worse than retransmission — see [README §6](README.md#6-on-webrtc--a-partial-correction)

**Encoder interaction with Phase 6:** streaming adds one more concurrent encode on top of the
per-Canvas recorders. The resource guard must count it.

---

## 8. Upstream seams

The complete list of upstream files this plan modifies. **Keep it short. Review at every merge.**

| # | File | Change | Phase |
|---|---|---|---|
| 1 | `CMakeLists.txt` | `add_subdirectory(shared/mc-data)`, `include(CTest)` | 0 |
| 2 | `plugins/CMakeLists.txt` | `add_obs_plugin(mc-rtsp / mc-data / mc-overlay)` | 2,3,4 |
| 3 | `frontend/CMakeLists.txt` | Add `frontend/subsea/*` sources | 0 |
| 4 | `frontend/OBSApp.cpp` | Application/organisation name, config dir | 0 |
| 5 | `cmake/common/bootstrap.cmake` | Product name/version strings | 0 |
| 6 | `frontend/widgets/OBSBasic.cpp` | One `MCFeatures::apply(this)` call in `OBSInit()` | 0 |
| 7 | `frontend/widgets/OBSBasic.hpp` | Member pointers for our controllers/docks | 0,1,4,6,7 |
| 8 | `frontend/obs-main.cpp` | New CLI flags (`--dump-ui-manifest`, `--dump-channels`) | 0,3 |
| 9 | `frontend/forms/OBSBasic.ui` | Layers dock, overlay-mode toggle, Record/Clip panel entries | 1,4,6,7 |
| 10 | `frontend/widgets/OBSBasic_Docks.cpp` | Register Layers and our other docks; retire Scenes/Sources docks behind a flag | 1,4,6 |
| ~~11~~ | ~~`frontend/widgets/OBSBasicPreview.cpp`~~ | **Not needed.** Selection sync goes through libobs instead — see §2.4 | — |
| 12 | `frontend/widgets/OBSBasic_Recording.cpp` | Notify `MCRecordingManager` on main record start/stop | 6, 7 |
| 13 | `frontend/widgets/OBSBasic_SceneCollections.cpp` | Save/load overlay assignments and Job metadata | 1,4 |
| 14 | `frontend/data/locale/en-US.ini` | Canvas/Element/Layers/Job/Rig strings | 1 |
| 15 | `frontend/settings/*` | Hide pages behind flags; add Data and Stream pages | 1,5,9 |
| 16 | `.github/workflows/*` | Windows-only matrix | 0 |
| 17 | `CMakePresets.json` | `windows-subsea-x64` preset | 0 |
| 18 | `test/cmocka/CMakeLists.txt` | Register new unit tests | 0+ |

Eighteen seams, of which ten are build/branding one-offs that will never conflict meaningfully.
The risky ones are **#9, #10, #11 and #13** — all in frequently-refactored frontend code, and #11
is the one most likely to break on a merge because preview selection handling changes upstream
fairly often. Each is deliberately a small call into `frontend/subsea/`.

---

## 9. Testing infrastructure that already exists

- **cmocka unit tests** — `test/cmocka/` with four existing tests registered via `add_test()`,
  gated behind `ENABLE_UNIT_TESTS`. New parser/registry/format/model tests slot straight in. There
  is currently **no** top-level `enable_testing()`/`include(CTest)`, so Phase 0 task 0.5 adds it.
- **`test/test-input`** — a dummy source/output plugin, useful as a synthetic video source.
- **CLI flags for scripted launches** — `--collection`, `--profile`, `--scene`, `--startrecording`,
  `--portable`, `--multi`, `--safe-mode` (`frontend/obs-main.cpp:958-1043`). `--portable --multi`
  is what lets the harness run against a throwaway config directory.
- **Log files** — every run writes a timestamped log; integration scripts assert on its contents.

Full strategy and scripts: [testing.md](testing.md).
