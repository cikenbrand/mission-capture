# Coding conventions

Fork-specific rules. Everything not covered here follows upstream OBS
([CODESTYLE.md](../../CODESTYLE.md)) — including formatting, which CI enforces with `clang-format`
and `gersemi`. **Do not change `.clang-format`.** Reformatting the tree would conflict on every
merged file, forever, and it is the cheapest mergeability win we have.

---

## 1. The naming rule that matters most

Mission Capture renames OBS's concepts in the **user interface only**. The code keeps upstream's
names. That split is what keeps `git merge upstream/master` survivable.

| UI says | Code says | libobs type |
|---|---|---|
| Canvas | `scene` | `obs_scene_t` |
| Element | `sceneItem` | `obs_sceneitem_t` |
| Layers | *(the tree widget)* | `MCLayersTree` |
| Job | `sceneCollection` | — |
| Rig | `profile` | — |

**Never rename an upstream identifier to match the UI vocabulary.** `OBSBasic::AddScene()` stays
`AddScene()`. Renaming it would touch hundreds of call sites and conflict on every merge, for a
cosmetic gain in code nobody but us reads.

### ⚠ `obs_canvas_t` is *not* a Canvas

libobs has its own `obs_canvas_t`: a render target with its own video mix. It is unrelated to what
the user calls a Canvas. Both will appear in the same file in Phase 6.

```cpp
class MCCanvasRecorder {
    MCRenderTarget renderTarget;  // obs_canvas_t   -- NEVER named "canvas"
    OBSWeakSource  canvas;        // obs_scene_t    -- the user's Canvas
};
```

**Every `obs_canvas_t` variable is named `renderTarget`.** No exceptions. If this slips, the
multi-recording code becomes unreadable within a month. Full rationale in
[architecture.md §0](architecture.md#0-naming-before-anything-else).

---

## 2. Where our code lives

| Location | Contents |
|---|---|
| `frontend/subsea/` | All fork-specific frontend code. Prefix types `MC` |
| `shared/mc-data/` | Qt-free static library — parsers, channel registry, transports |
| `plugins/mc-*/` | Our obs-modules |
| `frontend/cmake/subsea.cmake` | Our source list, so `frontend/CMakeLists.txt` needs one added line |

New behaviour goes in new files. Upstream files are touched only at the seams listed in
[architecture.md §8](architecture.md#8-upstream-seams), and every seam should be a short call into
`frontend/subsea/`.

Mark every edit inside an upstream file so the next person knows it is ours and why:

```cpp
/* Mission Capture: <what and why, briefly>. See frontend/subsea/<Header>.hpp */
```

---

## 3. User-facing strings

All UI text goes through the locale system — `QTStr("Some.Key")` / `Str("Some.Key")` — never a
literal in C++. Add keys to `frontend/data/locale/en-US.ini`.

Use the fork's vocabulary in every new string: **Canvas, Element, Layers, Job, Rig**. The words
*Scene*, *Source*, *Scene Collection* and *Profile* must not appear in user-visible text; the T1
terminology sweep fails the build if they do.

Two words to avoid for their own reasons:

- **"canvas" in the video-resolution sense.** Upstream writes "base (canvas) resolution"; say
  "base output resolution" instead, or it reads as though it means a Canvas.
- **"OBS".** Only in deliberate attribution — the About box and the importer, which genuinely
  names other applications.

English only ships. `frontend/data/locale.ini` is trimmed to `en-US`; the other 76 translation
files stay in the tree so upstream's updates keep merging, but they carry the old vocabulary and
are never offered.

---

## 4. Threading

- **Nothing blocks the graphics thread.** Transports, file I/O and network calls belong on their
  own threads; consumers take a short-lived snapshot. A stalled COM port must never stutter the
  preview or drop a recorded frame.
- **libobs signals fire on arbitrary threads.** Marshal to Qt with
  `QMetaObject::invokeMethod(...)` before touching any widget.
- Prefer `obs_queue_task(OBS_TASK_GRAPHICS, ...)` over reaching into the render loop.

---

## 5. Failing safely

This runs on a vessel, during a dive that cannot be repeated. Two habits follow:

- **Degrade, don't crash.** A missing `objectName`, an absent device, a dead socket — log a
  warning and carry on. `MCFeatures::apply()` is the model: it reports how many objects it could
  not find and keeps going.
- **Never block the operator with a modal.** Errors go to a banner, the status bar or the log.
  A modal dialog over the record button during a dive is unacceptable.

---

## 6. Before marking a task done

See the definition of done in [PROGRESS.md](PROGRESS.md). In short: it builds clean, its tests
pass with a run report, the acceptance criteria it touches carry `-Criterion` tags, and anything
left behind is in the open-items register rather than in your head.
