# Phase 5 — Transports and configuration UI

**Goal:** real hardware I/O — serial, UDP, TCP — plus the configuration UI that lets a field
engineer point the app at a device and name its fields without reading a manual.

**Prerequisites:** Phase 3 (the whole thing).

**Effort:** 3–4 weeks.

> ### 🔔 Ask the user before starting
>
> **Confirm the real channel count and data rate**, and the RS-232 line settings actually in use
> (baud, parity, stop bits, flow control, whether DTR must be asserted). This decides the serial
> transport's buffer sizing and whether the frame assembler needs a fast path.
>
> **Also worth asking now:** do you need the app to *send* anything back to the survey system —
> a status echo, an event string on record-start? Currently planned receive-only
> ([README §2](README.md#2-confirmed-decisions)). Cheap to add here, awkward to bolt on
> later.

**The point of this phase is commissioning.** An inspection engineer plugs a cable into a survey
computer they've never seen, gets a stream of numbers, and needs to be recording with a correct
overlay within ten minutes. Task 4.4 is where that is won or lost.

---

## Tasks

### 5.1 — Serial transport (Win32)
`4 days`

- `CreateFile("\\\\.\\COM12", ...)` with overlapped I/O so a dead port never hangs the thread
  (note the `\\.\` prefix — required for COM10 and above)
- `SetCommState` / `SetCommTimeouts` from a DCB: baud, data bits, parity, stop bits, flow control
  (none / RTS-CTS / XON-XOFF), plus explicit DTR/RTS control (some devices need DTR asserted to
  transmit at all)
- Port enumeration via SetupAPI, showing friendly names ("USB Serial Port (COM12)") — critical
  when a laptop has eight virtual ports
- **Reconnect loop with exponential backoff** — USB-serial adapters drop out and come back with
  the same port number. Recovery must be automatic and must be visible in the UI
- Surface error counters: framing errors, parity errors, overruns, buffer overflows. These are
  how you diagnose a wrong baud rate in the field

**Watch out:** hot-unplug of a USB adapter can make a blocking read hang indefinitely. Overlapped
I/O with a cancellation event is the fix; a `WaitForSingleObject` with timeout is the test.

---

### 5.2 — Network transports
`4 days`

| Transport | Config | Notes |
|---|---|---|
| **UDP listener** | Bind address, port, multicast group + interface | Most common for survey data broadcast on a vessel LAN. Multicast interface selection matters on multi-NIC survey PCs |
| **TCP client** | Host, port, reconnect backoff, keepalive | Connecting to a survey system's data server |
| **TCP server** | Bind address, port, max clients | When the survey system wants to push to us |

All share the frame assembler from Phase 3. For UDP, one datagram is usually one frame, but do not
assume it — run datagrams through the assembler anyway so a device that packs two readings per
packet still works.

Handle: interface changes, the survey PC rebooting, and a socket that connects but never sends
(dead-peer detection via the stale timeout, surfaced as a device-level warning).

---

### 5.3 — Device manager dialog
`4 days`

`MCDataDeviceDialog`:

- Device list with live state: connected / reconnecting / error, rate in lines/sec, error counts,
  time since last frame
- Add / edit / duplicate / remove; enable/disable without deleting
- **Raw monitor pane** — a live scrolling view of exactly what is arriving, with ASCII/hex toggle,
  pause, and "copy last 100 lines". This is the single most useful debugging tool in the product
  and it should be one click away, not buried
- Test-connection button that reports precisely why a connection failed

---

### 5.4 — Parser configuration wizard
`5 days` — **the highest-value UI in the phase**

`MCParserWizard`. The flow:

1. **Capture a sample.** A "Listen" button grabs the next N lines from the live device. Or paste
   a sample. Or load from file
2. **Auto-detect framing and separator.** Score candidate separators (`,` `;` `\t` `|` space)
   by how consistently they split the sample into the same field count. Show the guess, let the
   user override. Auto-detect NMEA by the `$`/`*checksum` shape
3. **Show the split.** A table: one row per captured line, one column per field, with the live
   value updating in the header. The user sees immediately whether the split is right
4. **Name the fields.** Click a column header, type `CP`. Set unit, precision, scale/offset,
   stale timeout. Leave columns unnamed to ignore them
5. **Validate.** Flag columns whose values don't parse as numbers, columns whose field count
   varies across sample lines, and duplicate channel names
6. **Preview.** Show the resolved channel table with live values before committing

Support the non-comma cases from the start: other separators, key=value, regex with named groups,
fixed-width columns, NMEA sentences. The wizard picks a sensible default parser type from the
sample and lets the user switch.

---

### 5.5 — Channels view
`2 days`

A dock (or a page in the device dialog) listing every channel across every device: name, current
value, unit, quality, age, source device. Sortable, filterable. Amber for stale, red for bad.

This is what the engineer glances at to answer "is the data alive?" — it should be readable from
two metres.

---

### 5.6 — Settings integration and status
`2 days`

- A **Data** page in the simplified Settings dialog
- A status-bar indicator: all devices healthy / N degraded / N down, click-through to the device
  manager
- Optional non-modal toast when a device disconnects — but never a modal dialog. A modal error
  box that blocks the record button during a dive is unacceptable
- Log device state transitions to the app log with timestamps for post-dive diagnosis

**Files:** seam #15

---

## Acceptance criteria

- [ ] Serial data at 115200 baud, 50 Hz, parses with zero loss over an hour
- [ ] Unplugging and replugging a USB-serial adapter recovers automatically within 5 seconds
- [ ] UDP unicast and multicast both receive; multicast interface selection works on a multi-NIC box
- [ ] TCP client reconnects after the server restarts, with backoff, without leaking sockets
- [ ] The wizard's auto-detect picks the right separator on all fixture samples
- [ ] An engineer unfamiliar with the app can go from cable to named channels in under 10 minutes
      (test this with a real person, not with a script)
- [ ] No device error path can produce a modal dialog
- [ ] A misconfigured baud rate produces a diagnosable error, not silence

---

## Tests

### Prerequisite tooling
`com0com` (or an equivalent null-modem emulator) to create a virtual COM pair — one end for the
test script to write, the other for the app to read. Setup documented in
[testing.md §Tooling](testing.md#tooling-prerequisites).

### Integration — `tools/subsea-tests/t5-transports.ps1`
Full text in [testing.md §T5](testing.md#t5--transports). Asserts:

1. **Serial loopback** — write a known sequence to `COM_A`, app reads `COM_B`, assert channel
   values via the websocket vendor API
2. **Serial reconnect** — close and reopen the writer's handle mid-stream; assert the app
   recovers and resumes publishing within the backoff window
3. **Baud mismatch** — write at 9600 while the app expects 115200; assert framing errors are
   counted and surfaced, and that the app does not hang or crash
4. **UDP unicast + multicast** — send from a `System.Net.Sockets.UdpClient`, assert receipt;
   repeat with a multicast group
5. **TCP client resilience** — start a listener, let the app connect, kill the listener, restart
   it, assert reconnection; assert socket handle count is stable (no leak) over 20 cycles
6. **TCP server mode** — app listens, script connects and sends, assert receipt
7. **Separator auto-detect** — run the detection function over every fixture sample and assert the
   expected separator (this one is a unit test, in `test_mc_parser.c`)
8. **Throughput** — 200 lines/sec for 10 minutes on serial; assert zero dropped lines (sequence
   numbers contiguous) and flat memory
