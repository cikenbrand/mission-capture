# Phase 8 — Sidecar data log and hardening

**Goal:** every recording is accompanied by a timestamped data log and a manifest, so data can be
re-synced to video, reported on, and audited after the dive.

**Prerequisites:** Phases 3, 5, 6, 7.

**Effort:** 3–4 weeks (event marking added).

> ### 🔔 Two things to settle with the user before starting
>
> **1. Is there a client deliverable format this must match?** You deferred this and asked to be
> reminded here — task 8.2 fixes the CSV column order, header naming, and filename convention, and
> retrofitting a client's convention after the first job has shipped is genuinely painful.
>
> Ask specifically about: required columns and their order, header naming (`DEPTH_m` vs `Depth (m)`
> vs a client code), timestamp format and timezone, decimal separator, file naming, and whether an
> IMCA or in-house DVR spec applies. If the answer is still "none", the format below is a
> reasonable house standard and we proceed with it.
>
> **2. Event marking.** Agreed in principle, deferred for detail — and it has to be settled *with*
> the column order rather than after, because it changes the CSV schema. Questions to work through:
> is the event vocabulary fixed, free-text, or a configurable list per Job? Does an event get its
> own row, its own column, or its own file? Should the event hotkey also start a
> [clip](phase-7-secondary-capture.md)? Can events be edited or deleted afterwards, or is the log
> append-only for audit reasons?

**Scope, settled:** the log carries **device data from RS-232 / TCP / UDP only** — no application
telemetry, no system events (beyond whatever event marking turns into). Overlays are burned in with
no clean copy, so the log supports **re-syncing** data to video, not re-burning a different overlay.
See [README §5.2](README.md#52-what-the-sidecar-log-can-and-cannot-do--informational-settled).

---

## Output files

For a recording `J2451_PilotCam_20260812_143000.mkv`:

| File | Purpose |
|---|---|
| `J2451_PilotCam_20260812_143000.csv` | Timestamped channel values |
| `J2451_PilotCam_20260812_143000.manifest.json` | Everything needed to interpret the CSV and reproduce the setup |
| `J2451_PilotCam_20260812_143000.raw.log` *(optional)* | Raw device lines with receive timestamps |

Clips from [Phase 7](phase-7-secondary-capture.md) get the same set, covering exactly the clip's
window — including its preroll, since the packet ring's start timestamp is the authority, not the
keypress.

---

## Tasks

### 8.1 — Log writer
`3 days`

`shared/mc-data/src/mc-log-writer.c` — deliberately in the Qt-free library so it is unit-testable.

- One writer per recording, opened on output start and closed on output stop
- Buffered writes with a periodic flush (default 1 s) so a crash loses at most one second
- Own thread; never blocks a transport thread or the graphics thread
- Bounded queue with an explicit overflow policy: drop and count, then record the drop count in
  the manifest. Silent loss is unacceptable in an audit trail
- Configurable rate: **on-change** (a row whenever any channel updates) or **fixed Hz** (a row at
  a regular interval with the latest values), default 1 Hz fixed

---

### 8.2 — CSV format
`2 days`

```csv
# mission-capture data log v1
# recording: J2451_PilotCam_20260812_143000.mkv
# started_utc: 2026-08-12T14:30:00.123Z
utc_iso8601,local_iso8601,media_seconds,CP_V,DEPTH_m,HDG_deg,_quality
2026-08-12T14:30:00.123Z,2026-08-12T22:30:00.123+08:00,0.000,1.031,5.132,6.122,GGG
2026-08-12T14:30:01.123Z,2026-08-12T22:30:01.123+08:00,1.000,1.033,5.140,6.118,GGG
```

- `media_seconds` is the offset from recording start — the column that makes resync trivial
- Column headers are `NAME_unit`; units are sanitised for CSV safety
- A compact `_quality` column, one character per channel (`G`ood / `S`tale / `B`ad / `N`odata),
  rather than a quality column per channel
- Full precision in the log regardless of the overlay's display precision
- RFC 4180 quoting for any text channel that could contain a comma or quote
- JSONL as an alternative format for the same content, selectable per profile

**Settle the column order and naming against the client's deliverable spec if there is one** — see
the reminder at the top of this phase. Retrofitting a column convention after
the first job is painful.

---

### 8.3 — Manifest and clock sync
`3 days`

The manifest is what makes the CSV interpretable in five years.

```json
{
  "version": 1,
  "recording": { "file": "...", "canvas": "Pilot Cam", "started_utc": "...", "stopped_utc": "...",
                 "duration_s": 1834.5, "parts": ["..._01.mkv", "..._02.mkv"] },
  "job":       { "number": "J2451", "client": "...", "vessel": "...", "rov": "..." },
  "video":     { "width": 1920, "height": 1080, "fps": 30.0, "encoder": "obs_nvenc_h264_tex",
                 "cqp": 20, "container": "matroska" },
  "overlay":   { "template": "Pipeline Banner", "template_uuid": "...", "items": [ ... ] },
  "channels":  [ { "name": "CP", "unit": "V", "device": "Survey String", "index": 0,
                   "scale": 1.0, "offset": 0.0 } ],
  "devices":   [ { "name": "Survey String", "transport": {...}, "parser": {...} } ],
  "sync": {
    "wall_ns_at_start":  1786012200123000000,
    "obs_ns_at_start":   84512300000,
    "measured_offset_ns": 0,
    "notes": "media_seconds = (row.wall_ns - wall_ns_at_start) / 1e9"
  },
  "counters": { "rows_written": 1834, "rows_dropped": 0, "device_errors": 3, "frames_dropped": 0 }
}
```

The `sync` block is the load-bearing part. Capture both clocks at the same instant on the output
start signal, and document the mapping explicitly rather than leaving future-you to infer it.

Known limitation worth documenting in the manifest itself: **serial and network data arrive with
unknown transport latency.** The timestamp is time-of-receipt, not time-of-measurement. If a
device emits its own timestamp field, prefer it and record which was used.

---

### 8.4 — Raw log
`1 day`

Optional per device: append every received frame with a receive timestamp, verbatim, before
parsing. When a parser turns out to have been misconfigured for an entire dive, this is the only
thing that saves the data.

Rotate at a size limit; count and report bytes written.

---

### 8.5 — Integration with multi-record and clips
`3 days`

- One log per recorder — camera 1's CSV covers camera 1's recording window exactly
- Logs follow auto-split: either one CSV per part, or one CSV for the session with a part column.
  **Recommend one per session with a part column** — analysts want one file
- **One log per clip** ([Phase 7](phase-7-secondary-capture.md)), covering the clip's window
  including its preroll. The packet ring's start timestamp is the authority, not the keypress —
  otherwise the clip's first seconds of video would have no data rows
- A clip's manifest names its parent recording, so the relationship survives the files being moved
- If a recorder or clip fails to start, no orphan CSV is left behind
- Log files land in the same directory as their video

---

### 8.6 — Event marking
`3 days` — **scope this with the user first; see the callout at the top of this phase**

Promoted from "future tooling" to a built feature, since it was agreed in principle. Held here
rather than earlier because it changes the CSV schema and must be designed alongside task 8.2's
column order.

Sketch, pending that conversation:

- A hotkey (and a button) that stamps an event at the current moment, into every running
  recording's log
- Event vocabulary configurable per Job — a list of buttons the operator can hit without typing
- Optionally also starts a [clip](phase-7-secondary-capture.md), which is the natural pairing:
  "I see an anode" is one intent, not two
- Append-only in the log for audit integrity, with corrections added as new rows rather than edits

---

### 8.7 — Post-dive review tooling (specification only)
`1 day to specify`

Not built in this phase; specified so Phase 8's outputs are shaped correctly for it.

- A small standalone tool (Python or C#) that loads a manifest + CSV + video and produces a
  synchronised review view or a report export
- Export to the client's reporting format
- Clip and event browsing across a whole job

---

### 8.8 — Hardening and soak
`4 days`

The phase where we stop adding and start breaking things:

- 8-hour soak: 3 recorders, 2 data devices, overlay updating, auto-split on. Sample memory,
  handles, GDI objects, and disk every minute
- Fault injection throughout: pull the serial cable, unplug a capture card, fill the disk, kill
  the network, hard-kill the process, and pull the power (a VM snapshot works for the last one)
- Verify every failure mode leaves playable video and a valid, truncated-but-parseable CSV
- Review every log message an operator might see for whether it says what to *do*
- Write the field troubleshooting guide from what the soak actually surfaced

---

## Acceptance criteria

- [ ] Every recording produces a CSV and a manifest
- [ ] Row count matches expectation: duration × rate, ±1
- [ ] `media_seconds` is monotonic, starts at ~0, and ends within one interval of the duration
- [ ] Channel values in the CSV match the simulator's known sequence exactly
- [ ] Quality flags correctly reflect injected stale and bad periods
- [ ] The manifest fully describes the channel set, overlay template, and encoder settings
- [ ] Hard-killing the app leaves a CSV that parses (last partial line tolerated)
- [ ] Zero dropped rows under normal load; drops are counted and reported when forced
- [ ] Multi-record produces one correct log per recorder, with correct windows
- [ ] 8-hour soak: flat memory and handles, all outputs valid

---

## Tests

### Unit — `test/cmocka/test_mc_log_writer.c`
CSV escaping, header generation, quality-string encoding, `media_seconds` computation, buffer
overflow accounting, and clean close on truncation. Skeleton in
[testing.md §T8-unit](testing.md#t8-unit--log-writer).

### Integration — `tools/subsea-tests/t8-sidecar.ps1`
Full text in [testing.md §T8](testing.md#t8--sidecar-log). Asserts:

1. **Basic log** — record 60 s with the simulator at 1 Hz; assert 60 ±1 rows, correct headers,
   and monotonic timestamps
2. **Value fidelity** — the simulator emits a known ramp; assert every CSV value matches the ramp
   at the expected index
3. **Sync accuracy** — burn the media timestamp into the video via an overlay field, extract
   frames at known `media_seconds` values, OCR or template-match the burned timestamp, and assert
   agreement within one frame period. *This is the test that proves the feature's central claim*
4. **Quality flags** — stall the simulator for 10 s mid-recording; assert exactly that window is
   flagged `S`
5. **Multi-record** — 3 recorders; assert 3 CSVs with correct, non-overlapping windows and
   Canvas-appropriate content
6. **Auto-split** — assert the part column tracks the video parts correctly
7. **Crash resilience** — hard-kill at 30 s; assert the CSV parses, has ~30 s of rows, and that at
   most the final line is truncated
8. **Overflow accounting** — drive 200 channels at 100 Hz on-change; assert either zero drops or a
   drop count that exactly matches the manifest's `rows_dropped`
9. **Soak** — 8 hours, all features on; assert flat resource usage and valid outputs throughout
