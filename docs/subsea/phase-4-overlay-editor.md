# Phase 4 — Overlay editor

**Goal:** flip the preview into an overlay editing mode where the user places text, images, and
live data fields; save that arrangement as a **reusable Overlay Template**; assign one template to
many Canvases.

**Prerequisites:** Phase 1 (Layers tree), Phase 2 (so you design overlays over real video, not a
colour bar), Phase 3 tasks 3.1 (channel registry) and 3.5 (simulator). The rest of Phase 3 can land
in parallel.

**Effort:** 4–6 weeks. The largest phase, and the headline feature.

**Read first:** [architecture.md §6](architecture.md#6-overlay) — the design hinges on templates
*being* OBS scenes rather than a new object type, and on the render-target naming rule in
[§0](architecture.md#0-naming-before-anything-else). Everything below assumes both.

---

## Tasks

### 4.1 — Template store
`3 days`

- Create a **container render target** at startup:
  `obs_canvas_create("MC Overlay Templates", NULL, SCENE_REF)`. A `NULL` `obs_video_info` means no
  video mix, so it costs nothing to render (`libobs/obs-canvas.c`, `obs_canvas_create_internal` —
  "A canvas can be created without a mix")
- Templates are scenes in that container, created with `obs_canvas_scene_create()`
- CRUD: create, rename, duplicate, delete — deletion warns and lists the Canvases affected
- Save/load rides on the existing scene-collection persistence
  (`frontend/widgets/OBSBasic_SceneCollections.cpp:901`, `:1271`)

**Files:** `frontend/subsea/MCOverlayTemplate.{hpp,cpp}` (new), seam #13

**Watch out:** template scenes must not appear as **Canvases** in the Layers tree. The tree
enumerates the main render target only, so this should be free — verify it, and verify templates
also don't leak into the Add Element picker.

---

### 4.2 — `mc_text` data-bound source
`5 days`

The `plugins/mc-overlay/` module. Design in
[architecture.md §6.3](architecture.md#63-the-data-bound-text-source).

- Owns a private `text_gdiplus` source; surfaces its properties via `obs_properties_add_group()`
  so the user gets the full font/colour/outline/background UI without us reimplementing it
- Format string with `{CHANNEL}` and `{CHANNEL:spec}` tokens; `spec` is a printf-ish precision
  (`0.00`), a width/pad (`000.0`), or `hex`/`raw`
- Literal braces via `{{` / `}}`
- Per-item stale behaviour: last value / placeholder / colour change / hide
- Unknown channel renders as a visible `{?NAME}` marker rather than silently blanking — a mistyped
  channel name should be obvious in the preview, not at 200 m depth

**Test:** `test/cmocka/test_mc_format.c` — a pure function, so test it hard.

---

### 4.3 — Overlay Edit mode controller
`6 days`

`MCOverlayMode` — the piece that makes it feel like an editor rather than a Canvas.

On entering:

1. Remember program state; repoint the main preview's render callback at the template scene
2. Render the currently-selected **Canvas** underneath at configurable opacity (default 30%) as a
   positioning reference — this is why Phase 2 comes first: designing a banner over real subsea
   video is a different exercise from designing it over a test pattern
3. Swap the Layers tree for the Overlay Items list (4.4)
4. Show safe-area guides (title-safe 4:3 and 16:9) and a rule-of-thirds grid, both toggleable
5. Show a mode banner so nobody mistakes edit mode for live

On exiting: restore everything, including dock layout and preview scaling.

**Reference implementation:** `frontend/widgets/OBSBasic_StudioMode.cpp` does the same
preview-shows-something-else trick.

**Critical constraint: overlay editing must be safe during a live recording.** Inspection crews
*will* fix a wrong job number mid-dive. Edits apply live to every assigned Canvas, which is the
desired behaviour — but the mode switch itself must not touch the program render path, restart a
source, or drop a frame. Design for it, then prove it with test 4 below.

**Files:** `frontend/subsea/MCOverlayMode.{hpp,cpp}` (new), seams #7, #9

---

### 4.4 — Overlay Items list
`4 days`

- Item list with visibility and lock toggles and drag-to-reorder. Reuse `MCLayersTree`'s delegate
  work from Phase 1 rather than building a third list widget
- Add-item toolbar with the types below, each landing pre-styled rather than as a raw default
- Property panel appropriate to the selected item; alignment tools (align left/centre/right,
  top/middle/bottom, distribute)
- Numeric position and size entry — "put it exactly 40 px from the bottom" is a normal request

---

### 4.5 — Item types
`5 days`

| Item | Implementation | Notes |
|---|---|---|
| **Text** | `text_gdiplus` | Static labels |
| **Data field** | `mc_text` (4.2) | The headline item |
| **Image** | `image_source` | Client and contractor logos |
| **Box / panel** | `color_source` | Background plates for legibility over dark subsea video |
| **Timestamp** | `mc_text` with `{@utc}` / `{@local}` pseudo-channels | Must be in the deliverable |
| **Recording timer** | `mc_text` with `{@rectime}` | Elapsed time of *this Canvas's* recording |
| **Canvas name** | `mc_text` with `{@canvas}` | Lets one template label every camera correctly |

The `@`-prefixed pseudo-channels are what make a single template reusable across cameras — the
same template reads "Pilot Cam" on one Canvas and "Manip Cam" on another. Resolve them per
rendering context, not globally.

Ship 3–5 starter templates (pipeline inspection, general visual inspection, diver panel) so the
first-run experience isn't a blank canvas.

---

### 4.6 — Assignment to Canvases
`4 days`

- Assignment dialog: template on one side, Canvas checkboxes on the other, batch apply/unassign
- Also reachable from the Canvas context menu in Layers, and from the New Job wizard
- Mechanics: `obs_scene_add()` the template source, lock the item, move it to the top, stamp
  private settings `{"mc_overlay": true, "mc_template_uuid": "..."}`
- The assignment appears in the Layers tree as an **Overlay Element** under the Canvas — the
  structure described in [README §1.2](README.md#12-terminology)
- **Re-assert position on Canvas changes** — if a user adds an Element afterwards, the overlay must
  stay on top. Hook the scene's `item_add` signal
- Deleting a template unassigns it everywhere, with a confirmation naming the affected Canvases

**Files:** `frontend/subsea/MCOverlayAssign.{hpp,cpp}` (new), seam #13

---

### 4.7 — Update-rate control
`2 days`

Addresses [README §5.5](README.md#55-text-re-rasterisation-cost-phase-4--medium).

- Per-item update rate, default **4 Hz**, max 30 Hz
- Dirty check on the *resolved string*: if `"DEPTH 12.3 m"` is unchanged, skip the update. With
  sensible precision settings this eliminates most re-rasterisations
- Global budget: cap total overlay text updates per second; log a warning when the cap bites, so
  it's diagnosable rather than mysterious
- Surface overlay update rate and re-rasterisation count in the Health panel

---

### 4.8 — Import / export
`2 days`

`.mcovl` — a JSON bundle with the template scene definition, the channel names it references, and
base64-embedded images. Templates need to move between vessels and jobs without carrying a whole
Job file.

On import, list referenced channels that don't exist in the current Rig so the user knows what to
configure.

---

## Acceptance criteria

- [ ] A template built once and assigned to 8 Canvases appears identically on all 8
- [ ] Editing the template updates all 8 live, including while recording
- [ ] Each assignment shows as an Overlay Element under its Canvas in the Layers tree
- [ ] A data field shows live simulator values and formats them per its spec
- [ ] Stale data triggers the configured stale behaviour within the timeout
- [ ] Overlay items cannot be selected, moved, or deleted from the normal (non-edit) preview
- [ ] `{@canvas}` resolves per Canvas, not globally
- [ ] Entering and leaving overlay mode during a recording drops zero frames
- [ ] 12 data fields at 4 Hz cost under 2% additional CPU
- [ ] Export → import on a different machine reproduces the template exactly
- [ ] Deleting a template cleanly unassigns it everywhere, leaving no dangling Elements

---

## Tests

### Unit — `test/cmocka/test_mc_format.c`
Format-string engine: tokens, specs, escapes, unknown channels, stale values, pseudo-channels, and
malformed format strings. Skeleton in [testing.md §T4-unit](testing.md#t4-unit--format-engine).

### Integration — `tools/subsea-tests/t4-overlay.ps1`
Full text in [testing.md §T4](testing.md#t4--overlay-editor). Asserts:

1. **Render correctness** — load a fixture Job with a known template, run the simulator with fixed
   values, capture a screenshot via `GetSourceScreenshot`, and compare a cropped region against a
   golden PNG within tolerance. Goldens live in `fixtures/golden/` and are re-blessed deliberately
2. **Value propagation** — push a new value via `SetSimulatorData`, wait one update period,
   screenshot again, assert the region changed
3. **Multi-Canvas consistency** — switch through 8 assigned Canvases, screenshot each, assert the
   overlay region is identical except the `{@canvas}` field
4. **Edit during recording** — start recording, enter overlay mode, move an item, exit, stop;
   `ffprobe` and assert frame count matches duration × fps with zero gaps. *This protects the most
   important safety property in the phase*
5. **Stale rendering** — stall the simulator, screenshot, assert the stale presentation appears;
   resume and assert recovery
6. **Performance** — 12 fields at 4 Hz for 10 minutes; assert the CPU delta against a no-overlay
   baseline is within budget and memory is flat
