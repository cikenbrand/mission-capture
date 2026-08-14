# Phase 3 — Data core

**Goal:** a headless, fully unit-tested engine that turns byte streams into named channels. No
hardware I/O and no configuration UI in this phase — those are [Phase 5](phase-5-transports.md).
This exists so [Phase 4](phase-4-overlay-editor.md) can bind `{DEPTH}` on day one.

**Prerequisites:** Phase 0.

**Effort:** 2–3 weeks.

> ### ✅ Answered by the user, 2026-08-13
>
> **At most four RS-232 devices**, chosen by the operator. Not a soft target — a hard cap, so the
> configuration UI can show four slots rather than an unbounded list, and so the engine can size
> itself once at startup instead of growing.
>
> **10 Hz per device, as the expected rate.** Not a ceiling to survive but a figure to measure
> against: a device configured for 10 Hz and delivering 3 is broken, and the engine can only say so
> if it knows what to expect. This is the same reasoning as the per-Element frame rate in
> [task 2.6](phase-2-video-elements.md#26--latency-and-health-measurement) — a rate that is merely
> *observed* tells you nothing without a rate that was *intended*.
>
> Forty frames a second across four devices is also two orders of magnitude below the point where a
> mutex-protected registry would need replacing with a seqlock. **So the hot path stays simple**,
> and that is now an evidence-based decision rather than an assumption.
>
> **Each port is configured independently**: COM port, baud rate, data bits, stop bits, parity.
> Four devices on one vessel will not agree on these, so they are per-device settings, not global.
>
> **The payload is positional comma-separated floats** — `1.031,5.132,6.122,7.451`.
>
> **The parser does not know what the numbers mean, and must not try.** It extracts values by
> position and nothing else. Meaning comes from an operator-configured map — index 0 → CP,
> index 1 → KP, index 2 → Temperature — held in configuration, not in code.
>
> That separation is the design, not an implementation detail. Every survey system orders its
> fields differently, so any meaning baked into the parser would be wrong on the next vessel. It
> also puts validation in the right place: the parser can only report *"field 3 was not a number"*,
> while the map is what can say *"nothing is assigned to index 3"*.
>
> The consequence for task 3.3 is that **Delimited** is the primary strategy and the index→channel
> map is the artefact that must be easy to get right and hard to get silently wrong — a
> mis-assignment yields plausible numbers under the wrong name, which is the error least likely to
> be caught before it reaches a client. Key/value and regex remain for systems that do not conform.
>
> Working ceiling, sized well above the answers so nothing has to be revisited:
> **≤ 64 channels across ≤ 4 devices, ≤ 20 Hz each.**

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

#### As built — three deviations from the sketch above

Recorded because 3.2 onwards are written against this API, not against the sketch.

1. **`text` is `char text[MC_TEXT_MAX + 1]`, not `const char *`.** The sketch asked for both a
   pointer into registry storage *and* a guarantee that no raw pointer reaches the graphics
   thread. Those cannot both hold. An array makes every `mc_value_t` self-contained, so a reader
   owns its copy outright and there is no lifetime question to get wrong later. Tokens are capped
   at 64 characters; a survey field longer than that is truncated rather than allocated for.

2. **`publish` takes the fields, not a `mc_value_t *`:**
   `mc_registry_publish(reg, name, numeric, text, quality)`. `ts_ns`, `wall_ns` and `seq` are the
   registry's business — a caller that could set them could backdate a reading or fake a sequence
   number, and both would be invisible in the recording afterwards. Scale and offset are applied
   here too, so the caller passes what arrived on the wire and nothing downstream has to remember
   to convert.

3. **`mc_registry_enum` dropped; `mc_registry_snapshot` covers it.** Snapshot already returns every
   channel under one lock, which is the property that matters. A callback invoked while the lock
   is held deadlocks the moment someone writes a callback that reads a channel — an easy mistake
   to make and a hard one to diagnose on a vessel. Snapshot now also returns names alongside
   values, which is the only thing enum offered that it did not.

**Verified, not assumed:** the concurrency test was checked against a build with the registry mutex
removed. The obvious version of that test — short tokens, one writer — passed without any locking
at all. It now uses three writers and 64-character tokens, and fails within a few thousand reads
when the lock is gone.

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

#### As built

- **Time is an argument, not a clock read.** `mc_frame_push` and `mc_frame_tick` take `now_ns`,
  so idle-timeout framing is tested exactly and instantly rather than with sleeps. The transport
  passes `os_gettime_ns()`; the tests pass whatever they like.
- **Not thread-safe, deliberately** — the opposite choice from the channel registry, and for a
  reason: an assembler belongs to the single transport thread reading its port. A lock here would
  guard against a design nobody should build.
- **Framing bytes are stripped, and nothing else is.** No delimiter, no sentinels. The assembler
  stays literal otherwise: a device sending CRLF while configured for `\n` yields frames with a
  trailing `\r`, and trimming that is the parser's job. Silently removing bytes it was not told
  about would corrupt fixed-length binary framing.
- **Sentinel frames exclude both sentinels**, so every mode hands the parser the same shape. Note
  for later: this discards an NMEA checksum trailing the `*`. Nothing verifies checksums today; if
  that is ever wanted, the assembler is where it goes.
- **Blank frames are never emitted.** Two delimiters in a row is not a reading, and passing it on
  becomes a row of BAD values on screen.
- **Over-length frames are dropped, never truncated**, and the assembler resynchronises to the next
  delimiter. Half a survey string parses as a plausible position rather than as an obvious fault.
  `mc_frame_dropped()` counts them, because a climbing drop count is what a wrong baud rate or a
  wrong delimiter looks like from the health panel.

**Verified, not assumed:** the exhaustive chunking harness was checked by breaking the assembler —
dropping partial state between reads. It fails at split `[1,8]` and reports that split by name,
while a single whole-stream push passes clean. That gap is precisely the bug this harness exists
to catch.

**Known limit — OI-64:** a stream joined partway through a frame delivers that remainder as a
properly terminated frame, and no framing layer can tell it from a short real one. 3.3's parser
must reject rows whose field count does not match the configured channel map.

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

#### As built

**Scope: Delimited + NMEA-0183.** Key/value, regex and fixed-width are deferred until a device
needs them — a parser written against no real data is a guess with tests attached.

- **The map is sparse and positional.** Four names on four positions of an eight-field row is the
  normal case, so the parser has no notion of a "correct" field count and unmapped fields cost
  nothing and count for nothing. One name at one position, one position per name, both enforced
  at create time.
- **Each mapping declares whether it expects a number.** `12:01` is a good Time and a bad double;
  without this the Time channel would sit permanently BAD in the health panel. Text fields keep
  their raw token and report no number.
- **One unreadable field marks only its own channel**, exactly as the plan requires. A short row
  marks only the positions it never reached, and those go BAD rather than keeping the previous
  reading — a stale value that still looks current is what ends up on a client deliverable.
- **An empty field is BAD, never zero.** Routine in NMEA when a sounder has no lock, and a depth
  of 0.0 is a very different claim from "no reading".
- **`strtod` must consume the whole token.** `5.2m` is not 5.2; a unit suffix appearing means the
  device is not sending what the map claims.
- **NMEA matches on the sentence tail**, so `DBT` accepts `$SDDBT` and `$IIDBT` alike — the talker
  identifies the box, the sentence identifies the reading. Checksum failure rejects the whole
  sentence, unlike a single bad field, because a checksum says the line is corrupt and no part of
  it can be trusted. Sentences without a checksum are accepted unless configured otherwise.
- **Counts, not log lines.** `rows`, `short_rows`, `bad_fields` and `rejected` are counters; at
  10 Hz a noisy line would bury the log.

**Framing note:** NMEA must be framed on its line terminator, **not** 3.2's sentinel mode, which
strips `$` and `*` and would discard the checksum before the parser saw it. This resolves the loose
end left in the 3.2 notes.

**Verified, not assumed:** two mutations. An off-by-one in short-row detection (`<` for `<=`)
leaves a stale value reading GOOD — and the original suite did not catch it, so the boundary test
was written before the fix was confirmed. The classic `atof` bug, where unparseable input becomes a
confident `0.0`, is caught by two tests.

**OI-64 is addressed, not eliminated.** A fragment is short, so the positions it never reached go
BAD and `short_rows` climbs. The values it *does* carry still land in the wrong channels and still
look plausible; that risk is accepted and the counter is what makes it visible.

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

#### As built

Extends `mc_channel_def_t` rather than adding a module — these are properties of a channel, and
scale/offset already lived there from 3.1.

- **The range is FLAGGED, never applied.** "Clamp with an out-of-range quality flag rather than
  silent clipping" is read here as *do not alter the number at all*. Clipping a 900 m depth to a
  configured 500 would put a reading the instrument never reported into the sidecar log and onto a
  client deliverable, with nothing downstream able to tell it had been invented. The envelope
  asserts "this looks wrong", which is a different claim from "this is what was measured". **If
  the intent was true clipping, this is the line to change** — it is one branch in
  `mc_registry_publish`, and three tests assert the current behaviour.
- **`out_of_range` is a separate bool, not a fifth quality value.** Freshness and plausibility are
  independent questions, and a reading can be stale *and* implausible; an enum could not say both.
- **Range is checked after scale and offset**, per the specified pipeline order, so the envelope is
  configured in display units rather than raw counts.
- **Precision is display-only**, via `mc_registry_format()`. The stored value and the raw token keep
  full precision, because the sidecar log is what a client receives.
- **With no precision configured, the raw token is shown.** A survey system that wrote `12.30` meant
  the trailing zero; reformatting to `12.3` quietly claims less confidence than was reported.
- **`stale_action`** chooses between keeping the last reading on screen for the overlay to grey out,
  and blanking it — the right answer differs per channel.
- **Gaps render `MC_NO_READING` (`--`), never a number.** NODATA and BAD must not be displayable as
  a measurement.

**Verified, not assumed:** an implementation that silently clips to the envelope fails three tests
across both suites.

**Test placement:** the end-to-end pipeline-order test (parse → scale → offset → clamp → precision,
through the real parser) is in `test_mc_parser.c` as the plan specifies. The per-transform unit
tests are in `test_mc_channel.c`, where the registry they belong to is tested.

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

#### As built

**The fixtures are a real capture.** `esp32-survey.txt` and `esp32-survey.timed.txt` are 32 lines
taken off the operator's ESP32 survey simulator on COM3 at 115200 baud — five positional
comma-separated floats, CRLF terminated. Nominally 1 Hz; actually 972–1038 ms line to line, and the
timed fixture preserves that jitter. Code that has only ever seen exact intervals has not been
tested against anything real.

- **Time is an argument**, as in 3.2. A three-second stall and an hour of survey both cost nothing
  to assert.
- **Randomness is a seeded xorshift64\***, carried in the configuration. Fault injection is worth
  nothing if a bad link cannot be reproduced exactly, on any machine, on any run. A zero seed picks
  a fixed default rather than the clock — a test that is only sometimes the same test is not a test.
- **A small read buffer changes the chunking and never the content.** Bytes produced but not
  collected are kept for the next call, which is precisely the property the frame assembler was
  built to survive.
- **`partial_every_n` withholds the terminator as well as truncating**, so the fragment runs into
  the following line. Emitting half a line *with* a terminator would only ever produce a short row;
  this reproduces the mid-stream join behind OI-64 on a desk.
- **A stall is silent, not an error.** A stalled device is indistinguishable from a slow one until
  something times out, and that ambiguity is the situation worth reproducing.
- **The rate note:** the device runs at 1 Hz, an order of magnitude below the 10 Hz the phase
  assumes. Harmless for these tests, and flagged so the discrepancy is a known quantity rather than
  a surprise at Phase 5.

**End to end, on data nobody wrote by hand.** The last test drives the real capture through
simulator → frame assembler → parser → registry in seven-byte chunks: 32 frames, 32 rows, zero
short rows, zero bad fields, `Depth` landing at about 12.9 m, in range, formatted with its unit.
This is the first proof in Phase 3 that the pieces fit together on bytes a device actually sent.

**Verified, not assumed:** discarding the unread remainder of a produced line fails both the
chunking-invariance test and the end-to-end run.

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
