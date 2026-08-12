# Phase 6 — Multi-Canvas recording

**Goal:** select any subset of Canvases and record them simultaneously, each to its own file, with
its overlay burned in.

**Prerequisites:** Phases 1, 2 and 4. Phase 0 task 0.7's hardware benchmark must be done before
design decisions here get locked.

**Effort:** 3–5 weeks. The riskiest phase.

**Read first:** [architecture.md §3](architecture.md#3-multi-canvas-recording), and
[§0](architecture.md#0-naming-before-anything-else) for the naming rule this phase depends on.

---

## Naming, because this phase is where the collision bites

Every recorded **Canvas** (`obs_scene_t`) gets its own **render target** (`obs_canvas_t`). Two
different things, one of which the user calls a canvas.

```cpp
class MCCanvasRecorder {          // records one user-facing Canvas
    MCRenderTarget renderTarget;  // the obs_canvas_t — never called "canvas"
    OBSWeakSource  canvas;        // the obs_scene_t — the user's Canvas
};
```

If a variable of type `obs_canvas_t*` is ever named `canvas` in this phase, the code becomes
unreadable within a month. Enforce it in review.

---

## The risk, stated up front

Recording 4 Canvases means 4 concurrent video encodes. GPU encoder session limits, GPU throughput,
CPU headroom, and disk write bandwidth all become binding constraints, and they bind differently
on every machine. See [README §5.1](README.md#51-concurrent-encoder-sessions-phase-6--high).

**If Phase 0 task 0.7 has not been done, do it before starting this phase.** Designing the
resource guard around a guessed limit is how you ship an app that silently drops frames on camera
4 during a client's dive.

---

## Tasks

### 6.1 — Per-Canvas recording configuration
`3 days`

Recording settings stored in each Canvas's private settings, so they travel with the Job:

```json
{ "mc_record": {
    "enabled": true,
    "filename_suffix": "PilotCam",
    "encoder": "inherit",          /* or an explicit encoder id */
    "quality": "inherit",
    "resolution": "inherit",       /* or "1920x1080" for a downscaled proxy */
    "split_minutes": 30
}}
```

"Inherit" resolves against a global multi-record section in the Rig, so the common case is one
setting changed in one place.

Surfaced in the Layers tree as a record-arm toggle on each Canvas row, and in the Record panel
(6.4).

---

### 6.2 — `MCCanvasRecorder`
`6 days`

One instance per recorded Canvas. Owns a render target, encoders, and an output.

```cpp
class MCCanvasRecorder {
public:
    bool Start(std::string_view filenameBase);
    void Stop(bool force = false);
    bool Active() const;
    RecorderStats Stats() const;   // frames, dropped, bytes, elapsed, last error

private:
    MCRenderTarget renderTarget;   // obs_canvas_create_private(name, &ovi, ACTIVATE | EPHEMERAL)
    OBSEncoder     venc;           // bound to obs_canvas_get_video(renderTarget)
    OBSEncoder     aenc;
    OBSOutput      output;         // "ffmpeg_muxer"
    OBSWeakSource  canvas;         // the obs_scene_t being recorded
    OBSSignal      startSig, stopSig, stoppingSig;
};
```

Start sequence and the audio-encoder sharing question are in
[architecture.md §3.2–3.3](architecture.md#32-the-per-canvas-recorder).

**Verify empirically before building on it:** that one audio encoder can feed several outputs
started at the same moment. The evidence says yes (`obs_encoder_add_output` maintains a DARRAY of
outputs, `libobs/obs-encoder.c:1868`) but it is load-bearing. If it doesn't hold, fall back to one
audio encoder per recorder — a little CPU, nothing else changes.

**Also verify:** that an `EPHEMERAL | ACTIVATE` private render target keeps its sources active
while its Canvas is not the program Canvas. That is the entire premise of recording a Canvas the
operator isn't looking at. `ACTIVATE` maps to `MAIN_VIEW` in `obs_view_init` and is what should
provide it.

**No clean-copy variant.** Confirmed out of scope — every recording has its overlay burned in.
That removes the duplicate-scene machinery the earlier draft carried and halves this phase's worst
case encoder load.

**Container is MKV.** It survives an abrupt end with a playable file, which matters more here than
anywhere else in the app. Remux to MP4 stays a manual post-step.

**Design for Phase 7 while you're here.** [Secondary recordings](phase-7-secondary-capture.md)
attach a second output to this recorder's *live* encoders. Keep the encoder references reachable
and the packet path interceptable, and Phase 7 becomes additive rather than a refactor. Concretely:
don't make `venc`/`aenc` private implementation details of `Start()`.

---

### 6.3 — `MCRecordingManager`
`4 days`

Owns the set of recorders and every lifecycle rule:

- Start all armed / stop all / start-stop individually
- **Atomic group start:** prepare every recorder, then start them. If any fails, roll back the
  whole set and report which Canvas and why. An engineer who believes four cameras are recording
  and finds three has lost footage that cannot be re-shot
- Coexistence with the main recording (`BasicOutputHandler`) — independent; starting one must not
  affect the other. Optionally chained: "the main Record button also starts armed Canvases" as a
  setting
- Handle Canvas deletion, rename, and Job switch while recording (block the switch, or stop
  cleanly first — do not try to be clever)
- Emit frontend events so the Record panel, the Layers tree indicators, and any external
  integration can follow along

**Files:** seam #12 — one notification call in `OBSBasic_Recording.cpp`

---

### 6.4 — Record panel
`4 days`

The pilot-facing surface. One row per Canvas:

| Canvas | ● Rec | Elapsed | Size | Dropped | Status |
|---|---|---|---|---|---|

- A checkbox to arm, a per-row start/stop, and a big **Start All / Stop All**
- Red dot, elapsed time, growing file size — visible proof it is working
- Dropped-frame count turns amber then red; degradation must be visible *during* the dive, not
  discovered afterwards
- Free-space readout and estimated remaining recording time at the current aggregate bitrate
- Right-click → open containing folder

The same record state also shows as an indicator on the Canvas row in the Layers tree, so a pilot
watching one panel sees it either way.

---

### 6.5 — Hotkeys
`1 day`

Per-Canvas record toggles plus global start-all/stop-all, registered through OBS's hotkey system so
they work when the window isn't focused.

---

### 6.6 — Resource guard
`4 days`

Reads its limits from `hardware-baseline.md` (Phase 0 task 0.7), overridable in settings.

- **Pre-flight:** requested concurrent encodes = armed Canvases + main recording + streaming
  ([Phase 9](phase-9-webrtc-streaming.md)). Compare against the configured per-family limit.
  Secondary recordings ([Phase 7](phase-7-secondary-capture.md)) do **not** count — they share
  an existing encoder
- **Model AMF and NVENC specifically.** They are the target hardware and they fail differently:
  NVENC refuses to create a session past its cap, while AMF tends to degrade throughput instead.
  The guard needs a limit for the first and a dropped-frame watchdog for the second
- **Warn before starting** with a message that says what is requested, what the limit is, and what
  to do: fewer Canvases, lower resolution, a different encoder family
- **Verify after starting:** if a session fails to create, roll back per 6.3
- **Monitor while running:** aggregate dropped frames, encoder lag, disk write latency; escalate
  visibly
- **Disk bandwidth check:** sum the configured bitrates against a measured sustained write rate for
  the target volume. Warn within 30% — network drives and consumer SSDs with exhausted SLC caches
  are the usual culprits

---

### 6.7 — Filename templating
`2 days`

Extend `os_generate_formatted_filename` with `%JOB%`, `%CANVAS%`, `%CLIENT%`, `%VESSEL%`, `%DIVE%`
from the Job metadata set in the New Job wizard.

Default: `%JOB%_%CANVAS%_%CCYY%%MM%%DD%_%hh%%mm%%ss%`

Guarantee uniqueness — never silently overwrite. Sanitise Canvas names into filesystem-safe
strings, and respect Windows' path length limit: a long job name plus a long Canvas name plus a
deep network path will exceed 260 characters, and it will happen on a real job.

---

### 6.8 — Auto-split and continuity
`3 days`

- Split every N minutes or N GB (default 30 minutes), with sequence-numbered parts
- **Split must be gapless.** `ffmpeg_muxer` supports file splitting; verify frame accounting across
  the boundary rather than trusting it
- All recorders split on the same boundary so parts line up across cameras — this matters
  enormously when reviewing a multi-camera pass
- Write a small playlist/index file per session listing the parts in order

---

## Acceptance criteria

- [ ] 3 Canvases record simultaneously to 3 separate files with correct, distinct content
- [ ] Each file has the correct Canvas's overlay burned in
- [ ] Files have correct duration (±0.5 s), resolution, frame rate, and no dropped frames
- [ ] Starting and stopping recorders individually while others run does not disturb them
- [ ] Group start is atomic: a failure rolls back and names the failing Canvas
- [ ] The main Record button still works as before, alongside per-Canvas recorders
- [ ] Exceeding the encoder limit produces a clear warning, not silent failure
- [ ] Auto-split produces gapless parts, aligned across all recorders
- [ ] Record state is visible in both the Record panel and the Layers tree
- [ ] A 4-hour soak with 3 recorders shows flat memory and no file corruption
- [ ] Hard-killing the app leaves every in-progress file playable

---

## Tests

### Integration — `tools/subsea-tests/t6-multirecord.ps1`

Full text in [testing.md §T6](testing.md#t6--multi-canvas-recording).

The core trick: build each fixture Canvas from a **distinct solid colour** plus a burned-in Canvas
name. Verification then becomes deterministic — extract a frame, check the dominant colour.

Asserts:

1. **Three-Canvas basic** — record 30 s from 3 Canvases; `ffprobe` each for duration (±0.5 s),
   resolution, frame rate, and `nb_read_frames` ≈ duration × fps
2. **Content routing** — extract a frame at t=15 s from each file and assert the dominant colour
   matches that Canvas. *This catches "all three files contain camera 1" — the most likely and most
   damaging bug in the phase*
3. **Overlay burn-in** — assert the overlay region differs from the flat background colour and
   matches the golden crop for that Canvas's template
4. **Independent control** — start 3, stop #2 at 10 s, stop the rest at 30 s; assert durations of
   30/10/30 s and no frame loss in #1 and #3 at the moment #2 stopped
5. **Coexistence** — run the main recording concurrently; assert 4 valid files
6. **Atomic rollback** — force a failure (unwritable path on recorder #2); assert no files left
   behind and that the error names Canvas #2
7. **Auto-split alignment** — 1-minute split, record 3.5 minutes on 3 Canvases; assert 4 parts
   each, boundaries within one frame across recorders, and that the parts sum to the full duration
8. **Kill test** — hard-kill mid-recording; assert every file is playable with a plausible duration
9. **Encoder-limit behaviour** — arm more Canvases than the configured limit; assert the warning
   appears and *nothing* starts, rather than a partial start
10. **Soak** — 3 recorders for 4 hours; sample memory and handles every minute, assert flat;
    `ffprobe` every file for integrity

Tests 2 and 7 are the ones worth writing first.
