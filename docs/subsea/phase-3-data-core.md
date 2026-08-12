# Phase 3 — Data core

**Goal:** a headless, fully unit-tested engine that turns byte streams into named channels. No
hardware I/O and no configuration UI in this phase — those are [Phase 5](phase-5-transports.md).
This exists so [Phase 4](phase-4-overlay-editor.md) can bind `{DEPTH}` on day one.

**Prerequisites:** Phase 0.

**Effort:** 2–3 weeks.

> ### 🔔 Ask the user before starting
>
> **How many channels, at what rate?** You deferred this. It sizes the registry's data structures
> and decides whether a mutex-protected hash table is adequate or the hot path needs a seqlock.
> 20 channels at 1 Hz and 200 channels at 50 Hz are different engineering problems. Ask again at
> the start of [Phase 5](phase-5-transports.md), which sizes the transports.
>
> Working assumption until told otherwise: **≤ 64 channels across ≤ 4 devices, ≤ 20 Hz each.**
> The design below holds comfortably at 10× that; it is stated so the assumption is explicit, not
> because it is a limit.

**Why now:** see [README §4.1](README.md#41-sequencing-notes).

---

## Tasks

### 3.1 — Channel registry
`3 days` — *unblocks Phase 4, so land this first*

```c
/* shared/mc-data/include/mc-data/mc-channel.h */

typedef enum {
    MC_QUALITY_GOOD,      /* fresh, parsed cleanly */
    MC_QUALITY_STALE,     /* no update within stale_timeout_ms */
    MC_QUALITY_BAD,       /* present but unparseable */
    MC_QUALITY_NODATA     /* never received */
} mc_quality_t;

typedef struct {
    double        numeric;      /* after scale/offset; NaN if non-numeric */
    const char   *text;         /* raw token, always populated */
    uint64_t      ts_ns;        /* os_gettime_ns() at receive */
    uint64_t      wall_ns;      /* UTC wall clock at receive */
    mc_quality_t  quality;
    uint64_t      seq;          /* monotonic update counter */
} mc_value_t;

mc_registry_t *mc_registry_get(void);                                   /* process singleton */
bool  mc_registry_declare(mc_registry_t *, const mc_channel_def_t *);   /* config time */
void  mc_registry_publish(mc_registry_t *, const char *name, const mc_value_t *);
bool  mc_registry_read(mc_registry_t *, const char *name, mc_value_t *out);
size_t mc_registry_snapshot(mc_registry_t *, mc_value_t *out, size_t max);  /* all, atomically */
void  mc_registry_enum(mc_registry_t *, bool (*cb)(void *, const char *, const mc_value_t *), void *);
```

Design notes:

- **Threading:** writers are transport threads, readers are the graphics thread and the log
  writer. A `pthread_mutex` (OBS's portable wrapper) around a hash table is fine at the expected
  scale; a reader takes a value copy and releases immediately. Revisit only if profiling says so.
- **Text lifetime:** `text` points into registry-owned storage; `mc_registry_read` copies into a
  caller-provided buffer. Never hand a raw pointer to the graphics thread.
- **Staleness is computed on read**, not by a timer thread — one less thread, no wakeups.
- **Name rules:** case-sensitive, `[A-Za-z0-9_]`, max 32 chars. Reject anything else at declare
  time so format-string parsing stays unambiguous.

**Test:** `test/cmocka/test_mc_channel.c`

---

### 3.2 — Frame assembler
`2 days`

Turns a byte stream into discrete frames. Strategies:

| Strategy | Config | Notes |
|---|---|---|
| Line delimiter | `\n`, `\r`, `\r\n`, or custom | Most common |
| Fixed length | N bytes | Binary-ish protocols |
| Start/end sentinel | e.g. `$`…`*` | NMEA, many gyros |
| Idle timeout | N ms of silence | Fallback for devices with no framing at all |

Must handle: partial frames across reads, frames split across many reads, multiple frames in one
read, a garbage prefix before the first sentinel, and a **max frame length** guard so a device
spewing bytes with no delimiter cannot grow the buffer without bound.

**Test:** `test/cmocka/test_mc_frame.c` — feed byte streams chopped at every possible boundary
and assert identical framing regardless of chunking. This is the classic place bugs hide.

---

### 3.3 — Parsers
`4 days`

| Parser | Example input | Config |
|---|---|---|
| **Delimited** | `1.031,5.132,6.122` | Separator string (not just `,`), quote char, trim, collapse-repeats, index→channel map |
| **Key/value** | `CP=1.031;DEP=5.132` | Pair separator, kv separator, key→channel map |
| **Regex** | anything | Pattern with named capture groups → channel names |
| **Fixed width** | `  1.031  5.132` | Column offsets and widths |
| **NMEA-0183** | `$SDDBT,12.3,f,3.7,M,2.0,F*2A` | Sentence filter + field index map; checksum validation |

Notes:

- NMEA is worth building in rather than leaving to regex: depth sounders, gyros, and USBL systems
  all speak it, and getting checksum handling right once is better than every user getting it
  wrong in a regex.
- **Malformed input must never throw away good channels.** If field 7 of 12 is garbage, publish
  the other eleven and mark channel 7 `MC_QUALITY_BAD`.
- Field count mismatches (short or long lines) are normal in the field, not exceptional. Log at
  debug level, count them, surface the count in the UI — but do not spam the log.

**Test:** `test/cmocka/test_mc_parser.c` — table-driven, including a deliberate corpus of
malformed lines.

---

### 3.4 — Channel transforms
`2 days`

Per channel, applied in this order: **parse → scale → offset → clamp → precision**.

- `scale`/`offset` for unit conversion (raw counts to volts, feet to metres)
- `min`/`max` clamp with an out-of-range quality flag rather than silent clipping
- `precision` for display only — the sidecar log keeps full precision
- `unit` string, used by the overlay and as a CSV column header suffix
- `stale_timeout_ms` and stale behaviour

**Test:** folded into `test_mc_parser.c`.

---

### 3.5 — Simulator transport
`2 days`

`mc-transport-sim` — the workhorse for every later phase's tests.

- **File playback mode:** replay a text file line-by-line at N Hz, looping, optionally honouring
  per-line inter-arrival timestamps
- **Synthetic mode:** generate per-channel sine/ramp/random/constant values at N Hz
- **Fault injection:** drop lines, emit malformed lines, stall for N seconds, emit partial frames.
  Being able to *reproduce* a bad data link on a desk is worth more than any other test tool here

Fixture files live in `tools/subsea-tests/fixtures/data/`.

---

### 3.6 — Configuration persistence
`2 days`

`data-devices.json` in the **profile** directory (rig config, not job config — see
[architecture.md §4.2](architecture.md#52-configuration-model)).

```json
{
  "version": 1,
  "devices": [{
    "id": "uuid", "name": "Survey String", "enabled": true,
    "transport": { "type": "sim", "file": "fixtures/data/survey.txt", "rate_hz": 10 },
    "framing":   { "type": "line", "delimiter": "\r\n", "max_frame_bytes": 4096 },
    "parser":    { "type": "delimited", "separator": ",", "trim": true },
    "channels": [
      { "name": "CP",    "index": 0, "unit": "V", "precision": 3, "stale_ms": 3000 },
      { "name": "DEPTH", "index": 1, "unit": "m", "precision": 1, "stale_ms": 3000,
        "scale": 1.0, "offset": 0.0, "min": 0, "max": 4000 },
      { "name": "HDG",   "index": 2, "unit": "deg", "precision": 1, "stale_ms": 3000 }
    ]
  }]
}
```

Include a `version` field from day one and write a loader that tolerates unknown keys — the schema
will change during Phases 4 and 6.

---

### 3.7 — Plugin shell and websocket vendor API
`2 days`

`plugins/mc-data/` owns device lifetimes: load config on module load, start transports, stop on
unload, and restart on Rig switch.

An obs-websocket **vendor API** named `mc-data` exposing:

| Request | Returns |
|---|---|
| `GetChannels` | Every channel: name, value, text, unit, quality, age |
| `GetChannel` | One channel |
| `GetDevices` | Device list with connection state and error counters |
| `SetSimulatorData` | Push a line directly into a simulator device |

Since the deployment is a single machine, this stays a **test hook** rather than becoming a
product feature — it is how every later integration script asserts on data without screen-scraping.
Build it anyway: the alternative is untestable data plumbing, which is the worst kind.

Phases 1, 4, 6 and 8 add sibling vendor APIs (`mc-layers`, `mc-overlay`, `mc-record`, `mc-stream`)
on the same pattern, so establish the conventions here — request naming, error shape, and JSON
casing.

Also add `--dump-channels <path>` for tests that don't want a websocket client.

**Files:** seams #2, #8

---

## Acceptance criteria

- [ ] `"1.031,5.132,6.122"` with three declared channels yields `CP=1.031`, `DEPTH=5.132`,
      `HDG=6.122`
- [ ] All five parsers pass their table-driven test corpora, malformed cases included
- [ ] Framing is identical regardless of how the byte stream is chunked
- [ ] A channel with no update for longer than its timeout reads `MC_QUALITY_STALE`
- [ ] Simulator drives 50 channels at 50 Hz with negligible CPU and zero allocations in the hot path
- [ ] No transport code path can block the graphics thread (verified by review and by a stall
      injection test)
- [ ] `ctest` passes; run the parser tests under ASan or Application Verifier at least once
- [ ] Config round-trips through save/load unchanged

---

## Tests

### Unit — `test/cmocka/test_mc_parser.c`, `test_mc_frame.c`, `test_mc_channel.c`

Registered in `test/cmocka/CMakeLists.txt` following the existing pattern. Skeleton in
[testing.md §T3-unit](testing.md#t3-unit--parser-and-registry).

### Integration — `tools/subsea-tests/t3-data-core.ps1`

Full text in [testing.md §T3](testing.md#t3--data-core). Asserts:

1. Launch with a simulator device configured from a known fixture
2. Wait for data, dump channels, and assert every value matches the fixture's expected values
3. Stall the simulator for longer than the stale timeout; assert quality flips to `STALE`
4. Resume; assert it flips back to `GOOD`
5. Inject 1000 malformed lines; assert the app stays up, good channels keep updating, and the
   error counter increments by exactly 1000
6. Run 50 channels at 50 Hz for five minutes; assert no memory growth (sample the working set)
   and no dropped updates (sequence numbers are contiguous)
