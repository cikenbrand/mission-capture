# Phase 8 — WebRTC streaming to a private server

**Goal:** push a live, sub-second feed to your own server so a client rep, a shore-based engineer,
or a superintendent can watch the dive in a browser.

**Prerequisites:** Phase 6 (the resource guard must count the streaming encode).

**Effort:** 2–3 weeks.

**Priority:** you said this is the last part and not crucial. It is deliberately last, and it is
the first thing to cut if the schedule tightens. Nothing in Phases 0–7 depends on it.

**Read first:** [README §6](README.md#6-on-webrtc--a-partial-correction) — there's a correction
about ICE there — and [architecture.md §7](architecture.md#7-streaming-whip--webrtc).

---

## What already exists

This phase is small because OBS ships a complete WebRTC implementation. `plugins/obs-webrtc/`
provides a **WHIP** output (WebRTC-HTTP Ingestion Protocol) built on libdatachannel:

| Fact | Where |
|---|---|
| Output id `whip_output`, flags `OBS_OUTPUT_ENCODED \| OBS_OUTPUT_SERVICE \| OBS_OUTPUT_MULTI_TRACK_AV` | `whip-output.cpp:733` |
| Video codecs advertised: `h264;hevc;av1` (HEVC gated on `ENABLE_HEVC`) | `whip-output.cpp:737-742` |
| Audio: Opus | `whip-output.cpp:737` |
| ICE servers parsed from the WHIP response `Link` header, per the WHIP draft | `whip-output.cpp:342` |
| Simulcast layers via RID | `ConfigureVideoTrack`, `whip-output.cpp:165` |

**ICE is already handled.** It is part of WebRTC's connection establishment, implemented inside
libdatachannel — not a separate framework to integrate. What you stand up server-side is a WHIP
endpoint, and STUN/TURN only if the stream crosses a NAT.

So this phase is configuration, UI, and operational behaviour. No protocol work.

---

## Tasks

### 9.1 — Simplified stream configuration
`3 days`

Replace OBS's service picker, which is entirely about Twitch and YouTube, with a single panel:

- WHIP endpoint URL
- Bearer token
- Encoder and bitrate (with a "match recording encoder" option, which is usually wrong for
  streaming — recording wants quality, streaming wants a bitrate ceiling)
- Resolution and frame rate, defaulting to a downscale (a 1080p60 recording and a 720p30 stream is
  the normal pairing, and it halves the encoder cost)
- Test-connection button that reports precisely what failed: DNS, TLS, HTTP status, ICE, or media

Stored in the Rig (profile), since the endpoint is a property of the vessel setup.

**Files:** `frontend/subsea/MCStreamPanel.{hpp,cpp}` (new), seam #15

---

### 9.2 — Choosing what to stream
`3 days`

Which Canvas feeds the stream:

- **Program Canvas** (follows the operator's selection) — the default, and what a client rep
  usually wants
- **A pinned Canvas** — stream Pilot Cam permanently regardless of what the operator is looking at

Implementation mirrors [Phase 6](phase-6-multi-record.md): a dedicated `obs_canvas_t` render target
with `obs_canvas_set_channel(renderTarget, 0, canvasSource)`, its own video encoder, and the
shared audio encoder. `MCCanvasRecorder` and the streaming path should share a common base — they
differ only in the output type.

Overlays are burned into the stream too, since they are Elements inside the Canvas.

---

### 9.3 — Resource-guard integration
`2 days`

Streaming is one more concurrent encode on top of the per-Canvas recorders. Extend the Phase 6
guard to count it, and make the warning explicit: "3 recordings + 1 stream = 4 encodes; your
measured limit is 3."

Consider a "stream shares the recording encoder" mode when the resolutions match, which trades
flexibility for one fewer session — but only if the benchmark says sessions are the binding
constraint.

---

### 9.4 — Connection resilience
`3 days`

A dive doesn't stop because the uplink hiccups.

- Automatic reconnect with backoff; never a modal dialog
- **Streaming failure must never affect recording.** Recording is the deliverable; streaming is a
  convenience. Verify this holds under every failure mode, including the encoder failing to start
- Connection state, current bitrate, and packet loss in the Health panel
- Log all transitions with timestamps

---

### 9.5 — Server-side documentation
`2 days`

Not code, but the thing that determines whether the feature is usable. Write
`docs/subsea/streaming-setup.md` covering:

- A working MediaMTX configuration for WHIP ingest and WebRTC playback, with the exact settings
- Which ports must be open, and the STUN/TURN question: on a single vessel LAN you likely need
  neither; across the internet you need STUN and probably TURN
- TLS certificates — browsers refuse `getUserMedia`-adjacent WebRTC playback over plain HTTP in
  most configurations, so this bites early
- A minimal browser player page
- The SRT and RIST alternatives (see below) and when to prefer them

---

### 9.6 — Keep SRT and RIST reachable
`1 day`

OBS already supports SRT and RIST through `obs-ffmpeg`'s outputs. For a satellite or 4G uplink they
often behave better than WebRTC — both are built for lossy long-haul links with retransmission,
whereas WebRTC responds to loss by degrading quality. WebRTC wins when the viewer is a plain
browser.

Keep both selectable in the stream panel as alternative protocols. MediaMTX will take SRT in and
serve WebRTC out, which gets you both properties at once — worth documenting as the recommended
topology for anything that isn't a LAN.

---

## Acceptance criteria

- [ ] A stream reaches a local MediaMTX WHIP endpoint and plays in a browser
- [ ] Glass-to-browser latency under 1 second on a LAN
- [ ] Streaming the program Canvas follows operator selection; a pinned Canvas ignores it
- [ ] Overlays appear in the stream
- [ ] Killing the network for 30 s reconnects automatically without operator action
- [ ] **No streaming failure of any kind interrupts or degrades a recording**
- [ ] The resource guard counts the streaming encode
- [ ] Bearer tokens never appear in logs
- [ ] SRT output works as an alternative
- [ ] `streaming-setup.md` gets someone from zero to a working endpoint

---

## Tests

### Integration — `tools/subsea-tests/t9-streaming.ps1`

Full text in [testing.md §T9](testing.md#t9--webrtc-streaming). Requires MediaMTX running locally —
the same dependency [Phase 2](phase-2-video-elements.md) already introduced for the RTSP fixture.

Asserts:

1. **Connect** — start streaming to a local WHIP endpoint; assert MediaMTX reports an active
   publisher within 5 s
2. **Media flowing** — pull the stream back down with `ffprobe` via MediaMTX's RTSP egress; assert
   the expected resolution, frame rate, and codec
3. **Content correctness** — stream a known-colour Canvas; extract a frame from the pulled stream
   and assert the colour, same technique as
   [T6](testing.md#t6--multi-canvas-recording)
4. **Latency** — burn a timer into the streamed Canvas; compare against wall clock at the receiver.
   Assert under 1 s
5. **Reconnect** — stop MediaMTX for 30 s, restart; assert the stream recovers without operator
   action and that reconnect attempts are logged
6. **Recording isolation** — *the most important test here.* Record 3 Canvases while streaming,
   then kill MediaMTX mid-run. Assert all three recordings complete with correct duration and zero
   dropped frames, and that `GetStats` shows no encoder skips
7. **Guard integration** — arm recorders and streaming beyond the configured encode limit; assert
   the guard refuses with a message naming both counts
8. **Token scrubbing** — configure a bearer token, run, grep the log directory. Any hit fails
9. **SRT fallback** — repeat tests 1–3 with SRT output
