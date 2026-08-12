# Phase 2 — Video elements

**Goal:** the two video Element types that carry the whole product — **Video Capture Device**
(Blackmagic and AVerMedia) and **RTSP Camera** — behaving like inspection kit rather than like
generic OBS sources.

**Prerequisites:** Phase 1 (the Layers tree and the element-type filter).

**Effort:** 2–3 weeks.

**Read first:** [architecture.md §4](architecture.md#4-video-elements).

**The framing that keeps this phase small:** almost none of this is new capture or decode code.
OBS already has DeckLink, DirectShow, and FFmpeg-based network playback. The work is *unification,
defaults, and failure behaviour* — which is precisely where OBS's generic-purpose choices are wrong
for a dive.

---

## Tasks

### 2.1 — Unified Video Capture Device element
`4 days`

Today a user must know whether their camera is a DeckLink or a DirectShow device before they can
add it. An inspection engineer should just pick a camera.

- One element type that enumerates both backends into a single device list:
  - `decklink-input` (`plugins/decklink`) for Blackmagic DeckLink / UltraStudio
  - `dshow_input` (`plugins/win-dshow`) for AVerMedia and other UVC/DirectShow devices
- Device entries show a friendly name and the backend as a subtitle, so it's diagnosable but not
  in the way
- On selection, create the appropriate underlying source with sensible settings and hide the
  backend distinction from then on
- Unknown or third-party capture cards fall through to DirectShow, which is what most of them
  present as anyway

**Files:** `plugins/mc-capture/` or a frontend-side factory in
`frontend/subsea/MCAddElementDialog.cpp` — prefer the frontend factory, since it needs no new
source type and therefore no new plugin

---

### 2.2 — Simplified capture properties
`3 days`

Visible by default: device, resolution/FPS, colour space/range, audio input, buffering. Everything
else behind "Advanced".

Backend-specific notes worth honouring:

- **DeckLink** auto-detects input format. Expose that as a toggle and default it on; a manual
  override matters when a camera outputs something non-standard
- **DirectShow** devices vary wildly in which resolution/FPS combinations actually work. Keep
  OBS's existing "Device Default" behaviour as the default and let the user pin values when needed
- Colour range (full vs limited) is a recurring source of "the video looks washed out" tickets on
  SDI. Surface it prominently rather than burying it

---

### 2.3 — Device loss and recovery
`3 days` — the task that earns its keep offshore

A capture card unplugged, power-cycled, or reset mid-dive must not silently render black.

- Detect loss per backend and surface a **banner across the Element's area in the preview**:
  "Signal lost — reconnecting…"
- Automatic reconnect with backoff; recover without user action when the device returns
- Log every transition with a timestamp, so a post-dive log review can answer "when did camera 2
  drop?"
- If it happens during a recording, keep recording — a black or frozen segment with a correct
  timeline is far more useful than a truncated file

**Watch out:** DeckLink signal loss (cable pulled) and device loss (card reset) are different
events with different recovery paths. Test both.

---

### 2.4 — RTSP Camera element
`5 days`

`plugins/mc-rtsp/` — a thin wrapper owning a private `ffmpeg_source`, not a new decoder. The
settings that matter are all already exposed
([architecture.md §4.2](architecture.md#42-rtsp-camera)):

| Setting | OBS default | Our default | Why |
|---|---|---|---|
| `is_local_file` | `true` | `false` | It's a network stream |
| `buffering_mb` | `2` | `0` | 2 MB is seconds of latency on a live camera |
| `reconnect_delay_sec` | `10` | `2`, with backoff | 10 s of black on a dive is a long time |
| `ffmpeg_options` | empty | `rtsp_transport=tcp fflags=nobuffer flags=low_delay` | See below |
| `hw_decode` | off | on where supported | Frees CPU for the encoders in Phase 6 |

`ffmpeg_options` is parsed with `av_dict_parse_string(..., "=", " ", 0)`
(`shared/media-playback/media-playback/media.c:680`) and handed to the demuxer, so arbitrary
AVOptions are reachable without touching FFmpeg code.

**TCP transport by default** because RTP-over-UDP drops packets silently and produces smeared
macroblocks that look like a camera fault. On a vessel LAN, retransmission is nearly free and the
failure mode is honest. Expose a UDP option for the cameras that need it.

The property panel: URL, username, password, transport (TCP/UDP), latency preset
(Lowest / Balanced / Most stable), and an advanced escape hatch for raw `ffmpeg_options`.

**Credentials:** accept username and password as separate fields and compose the URL internally.
Never log a URL containing a password — scrub it in every log line and in the Phase 7 manifest.

---

### 2.5 — Add Element picker
`2 days`

Three big buttons — Video Capture Device, RTSP Camera, Overlay — instead of OBS's long source
list. Each leads straight into the relevant configuration, with device discovery already run.

The Overlay button is stubbed here and wired in [Phase 4](phase-4-overlay-editor.md).

**Files:** `frontend/subsea/MCAddElementDialog.{hpp,cpp}` (new)

---

### 2.6 — Latency and health measurement
`2 days`

- Measure and display glass-to-glass-ish latency per Element where possible (capture timestamp to
  render), and at minimum the source's own reported buffering
- Per-Element health in the Layers tree and the Health panel: connected / reconnecting / no signal,
  plus frame rate actually received versus expected
- A dropped-frame counter per Element — "the RTSP camera is delivering 12 fps, not 25" is the kind
  of thing that must be visible during the dive, not inferred afterwards

---

## Acceptance criteria

- [ ] A DeckLink and an AVerMedia device can both be added through one picker without the user
      knowing which backend is involved
- [ ] DeckLink auto input-format detection works; manual override available
- [ ] An RTSP camera connects and displays within 3 seconds of adding it
- [ ] RTSP end-to-end latency under 500 ms on a LAN with the Lowest preset
- [ ] Pulling and restoring an SDI cable recovers automatically, with a visible banner throughout
- [ ] Unplugging and replugging a USB capture device recovers automatically
- [ ] A device dropping mid-recording does not truncate or corrupt the file
- [ ] RTSP credentials never appear in any log line or manifest
- [ ] Add Element offers exactly three choices
- [ ] Per-Element received frame rate is visible and accurate

---

## Tests

### Integration — `tools/subsea-tests/t2-video-elements.ps1`

Full text in [testing.md §T2](testing.md#t2--video-elements).

**Fixture:** a local RTSP server serving a known test pattern, so the tests need no real camera.
`ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=25 -f rtsp rtsp://127.0.0.1:8554/test` against
MediaMTX gives a deterministic, timestamped source — and MediaMTX is the same tool
[Phase 9](phase-9-webrtc-streaming.md) uses for WHIP, so it earns its place in the toolchain.

Asserts:

1. **RTSP connect** — add an RTSP element pointed at the fixture; assert video within 3 s and that
   the received frame rate is 25 ± 1
2. **Low latency** — the fixture pattern includes a burned-in timer; screenshot the preview and
   compare the displayed timer against wall clock. Assert under 500 ms with the Lowest preset
3. **RTSP reconnect** — kill the RTSP server, assert the banner appears within 3 s; restart it,
   assert recovery within 5 s without user action
4. **Reconnect during recording** — record 60 s with the server killed from t=20 s to t=35 s;
   `ffprobe` and assert the file is 60 s with a continuous timeline and no gap in frame count
5. **Credential scrubbing** — configure an RTSP URL with a password, run, then grep the entire log
   directory for the password string. *Any hit fails the suite*
6. **Capture device enumeration** — assert both backends' devices appear in one list with correct
   friendly names (needs real hardware; skipped with a warning in CI)
7. **Element health reporting** — throttle the RTSP fixture to 12 fps, assert the reported rate
   tracks it within 1 fps
8. **Picker** — assert Add Element offers exactly three types, and that with
   `MCFeatures::AllSourceTypes=true` the full OBS list returns
