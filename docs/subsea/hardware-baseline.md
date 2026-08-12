# Hardware baseline

Measured 2026-08-12 for [Phase 0 task 0.7](phase-0-foundation.md). Feeds the resource guard in
[Phase 6 task 6.6](phase-6-multi-record.md).

> ## ⚠ These numbers are provisional
>
> They were taken on the **development laptop, which is not the target machine**. The topside PC
> will be different hardware. Two consequences:
>
> - Treat every figure as a **first data point**, not a limit to design around.
> - **OI-5 stays open.** Re-run `tools/subsea-tests/bench-encoders.ps1` on the real spread PC
>   before Phase 6 sets the guard's defaults.
>
> Laptop-specific distortions to expect: a shared CPU/GPU power budget, and thermal throttling
> that 10-second runs do not capture. A desktop with the same silicon should do better.

---

## Machine under test

| | |
|---|---|
| CPU | AMD Ryzen AI 9 HX 370 — 12 cores / 24 threads |
| GPU 1 | NVIDIA GeForce RTX 5060 (Laptop), driver 32.0.15.9597 |
| GPU 2 | AMD Radeon 890M (integrated), driver 32.0.23033.1002 |
| OS | Windows 11 Pro |
| Record volume | `D:` — **1575–1741 MB/s** sustained write (2 GB, flushed) |
| Tool | ffmpeg 8.0 (see caveat below) |

Both target encoder families are present on one machine, so AMF and NVENC were measured
side by side under identical conditions.

### What was measured, and what it proxies

The benchmark drives **ffmpeg**, not Mission Capture. Driver-level session limits are a property
of the driver, so the "how many can be created" result transfers directly. Throughput does not
transfer exactly: OBS's NVENC and AMF paths encode from GPU textures and avoid the CPU round-trip
ffmpeg performs here, so **real throughput should be no worse than this, and is usually better.**
These are a conservative floor.

`speed` is relative to realtime. **1.00x means exactly keeping up with zero margin** — any hiccup
drops a frame. Useful headroom starts around 1.3x.

---

## Headline finding: session limits did not appear

**No session-creation failure occurred at any point, on either family, up to 12 concurrent
1080p60 encodes.**

This contradicts the assumption the plan was built on
([README §5.1](README.md#51-concurrent-encoder-sessions-phase-6--high)). NVIDIA's historical
GeForce NVENC session cap did not bite — recent drivers have raised or removed it. AMF showed no
cap either.

**So the binding constraint is throughput, not session count.** That changes the shape of Phase 6
task 6.6: the resource guard should be built primarily around a **sustained-throughput and
dropped-frame watchdog**, with a session-count limit as a secondary safety net rather than the
main mechanism.

---

## Results

Minimum `speed` across concurrent sessions; the slowest session is what drops frames.

### 1080p30

| Sessions | NVENC | AMF | x264 (veryfast) |
|---|---|---|---|
| 1 | 9.45x | 14.0x | 11.6x |
| 2 | 5.17x | 8.91x | 7.48x |
| 3 | 3.49x | 6.03x | 5.09x |
| 4 | 2.59x | 4.52x | 3.59x |
| 6 | 1.75x | 3.06x | 2.36x |
| 8 | 1.31x | 2.27x | 1.76x |
| 10 | 1.05x | 1.80x | — |
| 12 | **0.88x** | 1.51x | — |

### 1080p60

| Sessions | NVENC | AMF | x264 (veryfast) |
|---|---|---|---|
| 1 | 5.00x | 7.50x | 5.56x |
| 2 | 2.64x | 4.62x | 3.79x |
| 3 | 1.77x | 3.10x | 2.61x |
| 4 | 1.33x | 2.29x | 1.95x |
| 6 | **0.89x** | 1.51x | 1.30x |
| 8 | 0.67x | 1.16x | **0.97x** |
| 10 | 0.54x | 0.92x | — |
| 12 | 0.45x | 0.75x | — |

**Bold** marks the first count that fell below realtime.

### The surprise: the integrated GPU beat the discrete one

AMF on the Radeon 890M outperformed NVENC on the RTX 5060 at every session count — by roughly
40–70%. Counter-intuitive, but plausible on a laptop: the discrete GPU shares a power budget with
the APU and is likely running well below its desktop envelope, while the 890M's VCN encoder is
efficient and close to the memory it needs.

**Do not generalise this to a desktop.** On a desktop RTX 5060 with its own power budget, the
ordering would very likely reverse. It is a good illustration of why these numbers are provisional.

---

## Provisional guard defaults

Using **1.3x** as the minimum acceptable sustained speed — enough margin to absorb a hiccup
without dropping frames.

| Encoder | 1080p30 | 1080p60 |
|---|---|---|
| NVENC | 8 | 3 |
| AMF | 12+ (not reached) | 6 |
| x264 | 8 | 6 |

Read against the plan's working assumption of 3–4 simultaneous cameras, **every family clears it
comfortably at 1080p30**, and NVENC is marginal at 1080p60 with 4 cameras (1.33x). If 1080p60
multi-camera is a real requirement, that is the case to re-measure first on the target machine.

---

## Known gaps

- **Not the target machine.** The single most important limitation. OI-5 remains open.
- **Short runs.** 10 seconds of content each, so thermal throttling is not represented. A laptop
  under a sustained 4-hour dive will do worse. The Phase 8 soak test is where that shows up.
- **GPU utilisation only captured for NVENC.** The `\GPU Engine(*engtype_VideoEncode)` counter
  returned 0 throughout the AMF runs — the AMD encode engine is evidently exposed under a
  different instance name. The figure is missing, not zero. Worth fixing before the real run.
- **Encode only.** No capture, compositing, overlay rendering, or muxing ran alongside. Real
  recording adds all of those; expect meaningfully lower headroom.
- **No capture hardware.** OI-6 is untouched — see below.

---

## Capture hardware

Nothing was connected at the time of measurement, so the inventory OI-6 asks for is not yet
possible. Two things worth recording now:

- **UVC / DirectShow devices work without driver installation.** AVerMedia, Elgato and generic
  HDMI grabbers use the Windows class driver and are enumerated by `win-dshow` immediately.
- **DeckLink does not.** The `decklink` plugin loads Blackmagic's API at runtime; without
  **Blackmagic Desktop Video** installed it enumerates *zero* devices, with nothing in the UI
  explaining why. This is a per-machine prerequisite for every vessel PC and belongs on the
  deployment checklist.

---

## Reproducing

```bash
powershell -ExecutionPolicy Bypass -File tools/subsea-tests/bench-encoders.ps1
```

Defaults cover `h264_nvenc,h264_amf,libx264` at 1–8 sessions, 1080p30 and 1080p60, 10 seconds
each. Override with `-Encoders`, `-Sessions`, `-Modes`, `-Seconds`, `-RecordDrive`. Results are
written as JSON to `tools/subsea-tests/out/`.

On the real target machine, use a longer duration to surface thermal behaviour:

```bash
powershell -ExecutionPolicy Bypass -File tools/subsea-tests/bench-encoders.ps1 -Seconds 300
```
