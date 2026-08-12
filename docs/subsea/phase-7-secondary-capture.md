# Phase 7 — Secondary capture: clips and snapshots

**Goal:** capture the interesting bit, right now, two ways.

- **Clips** — while a Canvas records the full dive, start and stop *additional* recordings of the
  same Canvas without interrupting the main one.
- **Snapshots** — one button freezes a still from **every** Canvas at the same instant, shows them
  in a preview gallery, and lets the operator save each **with the overlay burned in, without it,
  or both**.

**Prerequisites:** Phases 1, 2 and 4 for snapshots; **Phase 6** additionally for clips.

**Effort:** 4–5 weeks.

**Read first:** [architecture.md §3.6](architecture.md#36-secondary-recordings-clips) and
[§3.7](architecture.md#37-snapshots).

> **Snapshots don't actually depend on Phase 6.** They need Canvases, video elements, and overlays —
> nothing from the recording machinery. If you want them in an operator's hands sooner, tasks
> 7.7–7.11 can be pulled forward to run right after [Phase 4](phase-4-overlay-editor.md). They are
> grouped here because they share a UI area, a hotkey philosophy, and probably an event-marking
> integration with clips.

---

## Why this is two features in one phase

They are the same user intent — *"that's worth keeping, capture it now"* — expressed at two
different weights. They share the Record panel, the hotkey ergonomics, the naming conventions, and
the likely event-marking tie-in from [Phase 8](phase-8-sidecar-log.md). Splitting them would mean
designing the same surface twice.

They are technically unrelated, though: clips ride on the encoder, snapshots ride on the graphics
thread. Nothing in the snapshot tasks depends on the clip tasks or vice versa, so they can be built
in either order or in parallel.

---

## Part A — Clips

### The good news

This feature costs **no additional encoder sessions**. A secondary recording of the same Canvas at
the same settings can share the primary recording's video and audio encoders, verified in the
current tree:

- `obs_encoder_start_internal()` (`libobs/obs-encoder.c`) pushes a new callback onto an
  already-running encoder; only the *first* callback triggers `add_connection()`. Joining
  mid-stream is a supported path, not a hack
- `send_first_video_packet()` (`libobs/obs-encoder.c:1326`) makes each newly-attached output wait
  for the next keyframe and prepends SEI, so the clip file starts as a valid, independently
  decodable stream
- `obs_encoder_add_output()` (`:1868`) maintains a DARRAY of outputs — fan-out is by design

That matters a lot given AMD and NVIDIA consumer GPUs with a hard session ceiling. Three Canvases
recording plus three clips running is **three** encode sessions, not six.

---

### The one real design problem

An output joining a running encoder can only begin at a **keyframe**. With a typical 2-second
keyframe interval, pressing "Clip" could start the file up to 2 seconds *after* the moment the
operator saw the thing they wanted to capture. For inspection work that is backwards — the
interesting event has usually already happened by the time a human reacts.

**There is no libobs API to force a keyframe on demand.** NVENC and AMF both support `forceIDR`
internally (`plugins/obs-nvenc/nvenc.c:119`, `plugins/obs-ffmpeg/texture-amf.cpp:747`) but only
on a reconfigure, and exposing it properly would mean changing the encoder interface in libobs —
a merge liability we should not take on for this.

The better answer inverts the problem: **buffer encoded packets and start the clip in the past.**

> Keep a rolling ring buffer of encoded packets covering the last N seconds. When the operator
> presses Clip, write the buffered packets from the **last keyframe at or before
> `now − preroll`**, then continue live.

The clip then starts *early*, never late. Default preroll of 10 seconds means "I saw something,
hit the button" reliably captures the run-up. This is the same machinery as OBS's replay buffer,
but with continuous recording afterwards rather than a fixed-length save.

**Confirm the default preroll with the user before building** — 10 s is my guess at what an ROV
pilot's reaction time plus "is that worth clipping?" deliberation actually needs. It may want to
be 30.

---

### Clip tasks

### 7.1 — Encoder sharing and the packet ring
`5 days`

- Extend `MCCanvasRecorder` so a Canvas can host **one primary recorder plus N secondary
  recorders**, all sharing the Canvas's render target, video encoder, and audio encoder
- Ring buffer of encoded packets per Canvas, sized to `preroll + one GOP`, holding packets from
  the last keyframe boundary onward. Memory is the constraint: at 50 Mbit/s, 30 s of preroll is
  ~190 MB per Canvas — cap it, show the cost in the UI, and refuse configurations that would
  exhaust RAM across many Canvases
- On clip start: locate the last keyframe at or before `now − preroll`, flush from there into the
  new output, then switch to live packets
- On clip stop: finalise the file cleanly without touching the primary recording

**Verify empirically first:** that a second `ffmpeg_muxer` output attached to a live encoder
produces a valid file with correct timestamps starting from zero. The packet DTS/PTS will carry
the primary recording's timeline, so the muxer needs the offset applied — this is the most likely
place for a subtle bug, and it shows up as a clip that plays with a huge initial seek.

---

### 7.2 — Independence guarantees
`3 days`

The primary recording is the deliverable. A clip is a convenience. That asymmetry must be
structural, not merely intended:

- A clip failing to start, failing to write, or filling the disk **must not** disturb the primary
  recording
- Stopping the primary recording stops any running clips cleanly, finalising their files
- A clip cannot outlive its primary recording
- Disk-full handling stops clips first, primary recording last

Test 4 below is what proves this, and it should be written before the feature is finished.

---

### 7.3 — Clip control UI
`4 days`

Per-Canvas, in the Record panel and mirrored on the Canvas row in Layers:

- A **Clip** button, enabled only while that Canvas's primary recording is running
- Running clips shown with their own elapsed time and file size
- Multiple concurrent clips on one Canvas allowed (overlapping events happen); cap at a
  configurable maximum, default 3
- A visible clip counter for the session
- **Global Clip All** — start a clip on every recording Canvas at once, which is what you want
  when the event is visible on several cameras

The button must be reachable without navigating anywhere. A hotkey is the primary interface for
this feature; the button is the discoverable backup.

---

### 7.4 — Naming and organisation
`2 days`

Clips must be findable six months later, by someone who wasn't there.

- Default: `%JOB%_%CANVAS%_%CCYY%%MM%%DD%_%hh%%mm%%ss%_CLIP%NN%.mkv`, where `%NN%` is
  per-Canvas-per-session sequence
- Optional subfolder per session (`.../clips/`) — recommend on by default so the main deliverables
  aren't buried
- Optional label prompt on clip **stop**, not start — never make the operator type before
  capturing
- Record the parent recording's filename in the clip's manifest so the relationship survives the
  files being moved

---

### 7.5 — Hotkeys
`1 day`

- Per-Canvas clip start/stop
- Global Clip All / Stop All Clips
- Registered through OBS's hotkey system so they work unfocused

Hotkey ergonomics matter here more than anywhere else in the app. A pilot with one hand on the
controls needs a single key, and it must be hard to confuse with Stop Recording.

---

### 7.6 — Data log integration
`2 days`

Each clip gets its own sidecar CSV covering exactly its window, on the same terms as
[Phase 8](phase-8-sidecar-log.md). With preroll, the clip's log starts before the button press
too — the ring buffer's start timestamp is the authority, not the keypress.

Lands after Phase 8's log writer exists; sequence 7.6 last or fold it into Phase 8 if 7 ships
first.

---

## Part B — Snapshots

### What's already in the tree

Most of this feature exists. `frontend/utility/ScreenshotObj.{hpp,cpp}` already implements the full
render → stage → GPU readback → save pipeline:

- It takes an **`obs_source_t*`**, so pointing it at a Canvas's scene source works directly
- `setSaveToFile(false)` plus the `imageReady(QImage)` signal means we can get a `QImage` in memory
  **without touching disk** — exactly what a preview gallery needs
- `setSize()` supports capturing at a size other than the source's
- It advances one stage per tick via `obs_add_tick_callback`, so N instances created together
  render in the same tick and then stagger their readbacks — which is the behaviour we want for
  both simultaneity and not spiking the graphics thread

`OBSBasic::Screenshot()` (`frontend/widgets/OBSBasic_Screenshots.cpp:24`) shows the existing usage.
So Part B is mostly assembly plus one genuinely new problem: the clean variant.

### The clean-variant problem

Rendering a Canvas *with* its overlay is trivial — that's just the scene. Rendering it *without*
is harder, because `obs_sceneitem_set_visible()` is a property of the item, shared by every render
target drawing that scene. Naively hiding the overlay would hide it in the program output and,
worse, in any recording running at that moment.

Two workable approaches:

**(A) Private duplicate — recommended.**
`obs_scene_duplicate(scene, name, OBS_SCENE_DUP_PRIVATE_REFS)` (`libobs/obs.h:1677`) creates a
private scene that holds *references* to the same sources rather than copies — so a capture card is
referenced, not re-opened, and the duplicate renders the same live frames. Remove the items flagged
`mc_overlay`, render that, destroy it.

**(B) Atomic hide / render / unhide.**
Do the whole thing inside one `obs_queue_task(OBS_TASK_GRAPHICS, ..., wait = true)`
(`libobs/obs.h:932`) so no video mix renders in between.

**Take (A).** It is a little more work, but (B)'s failure mode is *one frame of a client's recorded
deliverable is missing its overlay* — a defect that would be nearly impossible to notice in testing
and embarrassing to discover in a deliverable. (A) cannot produce that outcome at all.

Worth noting the symmetry with a decision already made: a clean *video* copy was rejected as too
expensive because it needs a second encoder running continuously. A clean *still* is cheap for
exactly the reason the video one wasn't — one render pass, no encoder, no ongoing cost.

### Snapshot tasks

### 7.7 — Snapshot capture engine
`4 days`

`MCSnapshotSet` — one object per button press, owning up to `2 × N` captures.

- On trigger, enumerate every Canvas and create the capture objects **in the same tick** so all
  Canvases freeze at the same instant. A multi-camera snapshot where camera 1 is 80 ms ahead of
  camera 3 is misleading for a moving event
- Two variants per Canvas: overlaid (the scene as-is) and clean (the private duplicate from above).
  Capture both eagerly so the gallery can toggle between them without a re-capture — the moment has
  passed by then
- Reuse `ScreenshotObj` with `setSaveToFile(false)`; collect `imageReady` into the set
- Skip Canvases with no video (a disconnected camera) and mark them in the gallery rather than
  producing a black tile with no explanation
- Cap concurrent snapshot sets so a held-down hotkey can't queue fifty readbacks

**Files:** `frontend/subsea/MCSnapshotSet.{hpp,cpp}` (new)

**Watch out:** the GPU readback is a real cost spike when N Canvases fire at once. `ScreenshotObj`'s
one-stage-per-tick design already staggers this; do not "optimise" it into a single tick.

---

### 7.8 — Preview gallery
`4 days`

`MCSnapshotDialog` — what the operator sees after pressing the button.

- A grid, one tile per Canvas, each showing the Canvas name and its thumbnail
- Per-tile toggle between **Overlaid** and **Clean** so the operator sees exactly what they'd save
- Per-tile include checkbox; select-all and select-none
- Click a tile to enlarge — checking that the anode is actually in focus is the whole point of a
  preview
- Save controls: **Overlaid / Clean / Both**, then a destination and a Save button
- Discard closes without writing anything

`ThumbnailManager::createView()` (`frontend/utility/ThumbnailManager.hpp:71`) exists for *live*
thumbnails; the gallery wants frozen `QImage`s from 7.7 instead, but the layout work there is worth
reading before building the grid.

**Files:** `frontend/subsea/MCSnapshotDialog.{hpp,cpp}` (new), seam #9

---

### 7.9 — Saving, formats, and naming
`2 days`

- **PNG** by default — lossless, which is what a report deliverable should carry. **JPEG** with a
  quality setting as an option, since inspection reports are frequently size-constrained
- Naming: `%JOB%_%CANVAS%_%CCYY%%MM%%DD%_%hh%%mm%%ss%.png`, with `_CLEAN` appended for the
  overlay-free variant. Never silently overwrite
- Snapshots land in a `snapshots/` subfolder of the Job directory by default, so they don't get
  lost among video files
- A **quick-save** mode that skips the gallery entirely and writes every Canvas with the configured
  default variant. Some operators will want the gallery; a pilot mid-manoeuvre will want one
  keypress and no dialog

---

### 7.10 — Data metadata
`2 days`

A still of a pipeline anomaly is worth much more with its depth, KP, and heading attached.

- Embed the channel snapshot at the capture instant as PNG `tEXt` chunks or JPEG EXIF comments
- Also write a sidecar `.json` per snapshot set — one file listing every image, its Canvas, the
  capture timestamp, and the full channel snapshot. Easier for a reporting tool to consume than
  parsing metadata out of images
- The clean variant carries the same metadata as the overlaid one, which is what makes a clean still
  useful at all: the data isn't in the pixels, so it has to be in the file

Depends on [Phase 3](phase-3-data-core.md)'s registry. Lands naturally alongside
[Phase 8](phase-8-sidecar-log.md) if that ships first.

---

### 7.11 — Snapshot control and hotkeys
`2 days`

- A **Snapshot** button in the Record panel and the main controls, always enabled — snapshots do
  not require a recording to be running
- A global hotkey, and a per-Canvas variant for "just this one"
- A brief non-modal confirmation of what was written, so the operator knows it worked without
  opening a folder
- A session snapshot counter in the Health panel

**Taking a snapshot during a recording must not drop a frame.** This is the same constraint as
everywhere else in the app, and test B4 below is what proves it.

---

## Acceptance criteria

### Clips

- [ ] A clip can be started and stopped while the primary recording runs, producing a separate
      valid MKV
- [ ] Clip and primary recording share one encoder session — verified by counting active encoder
      sessions, not by assumption
- [ ] Clip content starts at or before the button press, never after
- [ ] Clip timestamps start at zero; the file plays immediately with no initial seek
- [ ] Multiple concurrent clips on one Canvas work
- [ ] Clip All starts a clip on every recording Canvas within one frame of each other
- [ ] **No clip failure of any kind affects the primary recording**
- [ ] Stopping the primary recording finalises all its clips
- [ ] Hard-killing the app leaves both primary and clip files playable
- [ ] Ring buffer memory is bounded and reported

### Snapshots

- [ ] One button produces a preview tile for **every** Canvas
- [ ] All Canvases in a set are frozen at the same instant (within one frame)
- [ ] Both variants are available per Canvas, and the clean one contains no overlay pixels
- [ ] **Capturing a clean variant never removes the overlay from the program output or from any
      running recording, for even one frame**
- [ ] The operator can save overlaid, clean, or both, per Canvas
- [ ] Discarding writes nothing
- [ ] Saved PNGs carry the channel snapshot as metadata, and a sidecar JSON lists the set
- [ ] Taking a snapshot during a 3-Canvas recording drops zero frames
- [ ] A disconnected camera produces a labelled tile, not an unexplained black one
- [ ] Quick-save mode writes every Canvas with no dialog

---

## Tests

### Integration — `tools/subsea-tests/t7-clips.ps1` and `t7-snapshots.ps1`

Full text in [testing.md §T7](testing.md#t7--secondary-capture). Both use the distinct-colour
Canvas fixture from [T6](testing.md#t6--multi-canvas-recording).

**Clips —** asserts:

1. **Basic clip** — start primary, wait 20 s, clip for 10 s, stop primary at 60 s. Assert a 60 s
   primary and a ~10 s + preroll clip, both valid MKV
2. **Encoder session count** — query active encoders before and during the clip; assert the count
   did not increase. *This is the test that protects the feature's whole economic case*
3. **Preroll correctness** — the fixture Canvas carries a burned-in timer. Extract the clip's first
   frame and assert its timestamp is `presstime − preroll ± one GOP`, and never later than
   `presstime`
4. **Independence** — start primary + clip, then force the clip to fail by making its path
   unwritable mid-run. Assert the primary recording completes with correct duration and zero
   dropped frames, and that the error names the clip. *Write this one first*
5. **Concurrent clips** — three overlapping clips on one Canvas; assert three valid files with
   correct, different windows
6. **Clip All** — three recording Canvases, one Clip All; assert three clips whose start
   timestamps agree within one frame
7. **Timestamp normalisation** — assert every clip's first PTS is ~0, not the primary recording's
   elapsed time
8. **Parent stop cascades** — stop the primary with a clip running; assert the clip file is
   finalised and playable
9. **Kill test** — hard-kill with primary + 2 clips running; assert all three files are playable
10. **Ring memory** — configure 30 s preroll on 3 Canvases at high bitrate; assert reported buffer
    memory matches the configured bound and total process memory stays flat over 30 minutes

**Snapshots —** asserts:

- **B1. Coverage** — 3 Canvases, one snapshot; assert 3 tiles, each with both variants available
- **B2. Content routing** — assert each tile's dominant colour matches its Canvas, using the same
  one-pixel technique as T6. Catches "all three tiles show camera 1"
- **B3. Clean vs overlaid** — assert the overlaid image's overlay band differs from the flat Canvas
  colour, and the clean image's band **equals** it. That is the whole feature in two assertions
- **B4. No program disturbance** — *the one that matters most.* Record all 3 Canvases for 60 s,
  firing a snapshot every 5 s. Then `ffprobe` each recording: frame count must equal duration × fps
  exactly, `GetStats` must report zero skipped frames, and — extracting frames at each snapshot
  instant — the overlay must be present in every recorded frame. A clean-variant capture that
  briefly removed the overlay from the recording would show up here and nowhere else
- **B5. Simultaneity** — with a burned-in `{@utc}` field on each Canvas, read the timestamp out of
  each tile and assert they agree within one frame period
- **B6. Save variants** — save Both; assert `2 × N` files with correct `_CLEAN` suffixes, and that
  Discard writes nothing
- **B7. Metadata** — assert PNG `tEXt` chunks carry the simulator's known channel values, and that
  the sidecar JSON lists every image with matching values
- **B8. Missing source** — disconnect the RTSP fixture, snapshot, assert that Canvas's tile is
  labelled rather than silently black
- **B9. Hotkey spam** — hold the snapshot hotkey for 5 s; assert the concurrency cap holds, memory
  stays bounded, and no frames are dropped
