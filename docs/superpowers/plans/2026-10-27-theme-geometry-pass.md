# NumOS Theme Geometry Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reclaim vertical space on the 320×156 canvas by shrinking the Montserrat ladder to 75%, deleting every bottom hint bar, and rebuilding the Calculus and Grapher chrome as floating chips.

**Architecture:** Typography and the status bar change globally and land first, because every downstream height is derived from them. Layout work then proceeds screen by screen (Calculus, then Grapher) against the new rungs. The Grapher's plot grows from 320×59 to 320×137 and its default window becomes 16 px/unit on both axes; `normalizeAspect()` stays as the pan/zoom guard. Casio is regression-captured last, not redesigned.

**Tech Stack:** C++17, LVGL 9.x, PlatformIO (`env:emulator_pc` native + `env:esp32s3_n16r8`), `lv_font_conv`, Python 3 for the generated masks and for PPM assertions.

**Spec:** `~/musings/sserialprintthing/theme-system/14-geometry-pass-spec.md`

## Global Constraints

- Canvas is **320×156, offsetY +54**. Never change `SCREEN_WIDTH` / `SCREEN_HEIGHT` (`src/Config.h:205-212`).
- `src/apps/**` may contain **no colour or font literals**. Read `ui::ThemeManager::current()` tokens and the `ui::font*()` accessors. (`agents.md` §5.6)
- LVGL **9.x APIs only**; no deprecated LVGL 8 calls.
- New `.cpp` files must be added to `build_src_filter` in `platformio.ini` or the emulator silently omits them (`agents.md` §5.5).
- Generated headers in `src/ui/generated/` are never hand-edited; their generator must pass `--check` in any verification claimed (`agents.md` §5.6).
- The old "numos renders byte-identical" gate is **retired** by this pass — text-bearing screens are expected to change. Do not chase `AE=0` on `launcher_smoke.ppm` / `calc_1_plus_2.ppm`.
- Type rungs are exactly **15 / 11 / 9 / 8** (display / body / small / x-small). Math faces and `unscii_8` do not change.
- Function slots: **dynamic, max 5**.

## Review Focus

Inputs and conditions the spec implies but no task's tests fully exercise; each has a pinning test in the task that owns the code.

1. **9px text on the real 1.53" panel may be unreadable** — a person expects to read the status bar clock without squinting. Pinned in Task 1 by a device capture before chrome is locked.
2. **`7` typed mid-expression must stay a digit** — a person expects SHIFT+7 to open help and plain 7 to insert 7, including while the cursor is inside an expression. Pinned in Task 3.
3. **The legend with 0 or 1 functions must not draw an empty box** — a person expects nothing (or one row) rather than a stub. Pinned in Task 6.
4. **Opening the editor while a plot is being dragged/traced** — a person expects the plot to keep rendering correctly behind a 0.6× panel. Pinned in Task 6.
5. **`MAX_FUNCS` 5 vs the Casio split clamp** — a person on the Casio layout expects no lost or duplicated slot. Pinned in Task 7.

---

### Task 1: Montserrat rungs at 15 / 11 / 9 and the ink-measurement tool

**Files:**
- Create: `src/fonts/montserrat_15.c`, `src/fonts/montserrat_11.c`, `src/fonts/montserrat_9.c` (generated)
- Create: `scripts/gen_montserrat_ui_fonts.sh`
- Create: `scripts/measure-ink.py`
- Modify: `src/lv_conf.h:210-227`, `src/ui/Theme.cpp:128-132`, `src/ui/ThemeFonts.h` (comment block only)

**Interfaces:**
- Produces: `const lv_font_t lv_font_montserrat_15`, `..._11`, `..._9` — symbol names the rest of the pass references through `Theme`/`lv_conf`.
- Produces: `scripts/measure-ink.py <ppm> --region x0,y0,x1,y1 --json` → prints `{"ink":{"x0":..,"y0":..,"x1":..,"y1":..},"count":N}`; `count == 0` means an empty region. Exit 1 when the region holds ink outside `--clip`.

- [ ] **Step 1: Generate the three faces and prove they exist**

```bash
lv_font_conv --font assets/fonts/Montserrat-Regular.ttf --size 11 \
  --bpp 4 --format lvgl --no-compress --lv-include lvgl.h \
  --range 0x20-0x7F -o src/fonts/montserrat_11.c
# repeat for --size 9 and --size 15
grep -c "lv_font_montserrat_11" src/fonts/montserrat_11.c   # expect >= 1
```

Wrap these three invocations in `scripts/gen_montserrat_ui_fonts.sh`, and run
`scripts/add_font_provenance_header.py` in the same script so each `.c` starts with the
Montserrat `OFL-1.1` header (spec §2; the standing rule in `numos-theme-system`).

- [ ] **Step 2: Enable them and re-point the numos ladder**

`src/lv_conf.h`: add `#define LV_FONT_MONTSERRAT_15 1`, `_11 1`, `_9 1`; set
`LV_FONT_DEFAULT` to `&lv_font_montserrat_11`.

`src/ui/Theme.cpp:128-132` numos initialiser becomes:

```cpp
/* fontUi        */ &lv_font_montserrat_11,
/* fontUiSmall   */ &lv_font_montserrat_9,
/* fontUiXSmall  */ &lv_font_montserrat_8,
/* fontDisplay   */ &lv_font_montserrat_15,
/* fontMono      */ &lv_font_unscii_8,
```

Do **not** touch the Casio initialiser at lines 173-177 — it uses `casio_math_*` for every UI
rung.

- [ ] **Step 3: Build and run the launcher capture**

```bash
PLATFORMIO_BUILD_DIR=.pio/build pio run -e emulator_pc
SDL_VIDEODRIVER=dummy .pio/build/emulator_pc/program --headless --deterministic \
  --script tests/emulator/scripts/launcher_smoke.numos --frames 400 \
  --screenshot out/type_launcher.ppm --quiet
python3 scripts/measure-ink.py out/type_launcher.ppm --region 0,0,320,156
```

Expected: exit 0, ink present. Compare against a pre-change capture: the ink bbox must be
**smaller** in height (this is the only assertion that proves the size actually changed).

- [ ] **Step 4: Commit**

```bash
git add src/fonts/montserrat_*.c scripts/gen_montserrat_ui_fonts.sh scripts/measure-ink.py \
        src/lv_conf.h src/ui/Theme.cpp
git commit -m "theme: generate Montserrat 15/11/9 and move the numos ladder to 75%"
```

---

### Task 2: Status bar at 18px, globally

**Files:**
- Modify: `src/ui/StatusBar.h:45` (`HEIGHT`), `src/ui/StatusBar.cpp:57-120`

**Interfaces:**
- Produces: `ui::StatusBar::HEIGHT == 18`. Every screen's `BAR_H` formula already reads this constant, so consumers need no edit — verify that rather than assuming it.

- [ ] **Step 1: Change the constant and the bar's own font**

`HEIGHT 24 → 18`. In `create()`, set the clock/title/angle labels to `ui::fontUiSmall()`
and shrink the battery rect by 6px vertically, keeping it right-aligned at 4px margin.

- [ ] **Step 2: Prove no screen still assumes 24**

```bash
grep -rn "StatusBar::HEIGHT" src/ | grep -v "StatusBar\."
grep -rn "= 24;.*BAR_H\|BAR_H *= *24" src/
```

Expected: the second command finds nothing; the first lists every consumer, and each one is
either `StatusBar::HEIGHT + 1` or reads `StatusBar::HEIGHT` directly.

- [ ] **Step 3: Capture three screens and assert the bar band**

Run `launcher_smoke`, `calc_1_plus_2`, `grapher_smoke`; for each:

```bash
python3 scripts/measure-ink.py out/<name>.ppm --region 0,0,320,18 --clip 0,0,320,18
```

Expected: exit 0 for all three — all status-bar ink sits inside the top 18 rows. Any exit 1
means a label still overflows the new height.

- [ ] **Step 4: Commit**

```bash
git add src/ui/StatusBar.h src/ui/StatusBar.cpp
git commit -m "statusbar: 18px on every screen"
```

---

### Task 3: HelpChip, HelpOverlay, SHIFT+7

**Files:**
- Create: `src/ui/HelpChip.{h,cpp}`, `src/ui/HelpOverlay.{h,cpp}`, `src/ui/help/HelpContent.h`
- Modify: `src/SystemApp.cpp` (key guard), `platformio.ini` (`build_src_filter`)
- Test: `tests/emulator/scripts/numos_help_smoke.numos`

**Interfaces:**
- `void ui::HelpChip::create(lv_obj_t* parent, int x, int y);` — draws a 30×12 chip at absolute
  coordinates in `parent`; no state, no callbacks.
- `void ui::HelpOverlay::create(lv_obj_t* parent, AppId app);` / `void close();` /
  `bool isOpen() const;` — full-canvas panel, `AC` closes.
- `SystemApp` gains `bool _helpOpen;` and handles `SHIFT + KeyCode::NUM_7` in `handleKey`
  after `_km->resolve(...)` and before `switch (_mode)`.

- [ ] **Step 1: Write the failing script**

`tests/emulator/scripts/numos_help_smoke.numos`:

```
log "help: open from the calculator"
wait 200
assert_app Calculator
key shift
key 7
wait 40
screenshot out/numos_help_open.ppm
key ac
wait 40
screenshot out/numos_help_closed.ppm
# plain 7 must still insert a digit, not open help
key 7
wait 20
assert_result "7"
```

- [ ] **Step 2: Run it and watch it fail**

```bash
SDL_VIDEODRIVER=dummy .pio/build/emulator_pc/program --headless --deterministic \
  --script tests/emulator/scripts/numos_help_smoke.numos --frames 400 --quiet; echo "exit=$?"
```

Expected: non-zero, or `numos_help_open.ppm` identical to `numos_help_closed.ppm` — help does
not exist yet.

- [ ] **Step 3: Implement the chip, the overlay, and the guard**

Chip: 30×12, dark fill, `⇧7` in `fontUiXSmall()` + the question icon as an A8 mask recoloured
to `textOnAccent`. Overlay: `bg` fill covering 0,0→320,156, one row per entry in
`ui::help::kKeyTable[app]`, each row `fontUiSmall()` key label + `fontUi()` description;
`AC` in `handleKey` closes it and returns before app routing.

The guard order is the load-bearing part: `SHIFT+7` is intercepted **before** apps see digits,
and only when `_km->isAlpha()`-style modifier state is `SHIFT`. Plain `7` falls through
untouched.

- [ ] **Step 4: Run the script; assert the two captures differ and the digit still lands**

```bash
python3 - <<'PY'
a=open('out/numos_help_open.ppm','rb').read(); b=open('out/numos_help_closed.ppm','rb').read()
print("overlay changed pixels:", sum(x!=y for x,y in zip(a,b)))
PY
```

Expected: a large non-zero diff (thousands of bytes), and the script's `assert_result "7"`
passing.

- [ ] **Step 5: Add the new sources to the emulator filter, rebuild, commit**

`platformio.ini` `env:emulator_pc` `build_src_filter`: add `+<ui/HelpChip.cpp>`,
`+<ui/HelpOverlay.cpp>`.

```bash
git add src/ui/HelpChip.* src/ui/HelpOverlay.* src/ui/help/HelpContent.h src/SystemApp.cpp \
        platformio.ini tests/emulator/scripts/numos_help_smoke.numos
git commit -m "ui: help chip + overlay, SHIFT+7"
```

---

### Task 4: Delete every bottom hint bar

**Files:**
- Modify: every app that draws a bottom hint label — start from
  `grep -rn "contentH - 1[68]\|SCREEN_H - 1[68]\|HINT" src/apps/*.cpp`

**Interfaces:**
- Consumes: `ui::HelpChip::create(parent, x, y)` from Task 3.

- [ ] **Step 1: Enumerate the hint bars before touching anything**

```bash
grep -rn "contentH - 1[68]\|SCREEN_H - 1[68]" src/apps/*.cpp
```

Record the list in the task's commit message body. Expected: Calculus, Integral, Matrices,
Probability, Regression, Sequences, Statistics, Settings, and the Grapher's info bar among the
hits.

- [ ] **Step 2: Delete the labels and anchor a HelpChip in each freed corner**

For each app: remove the hint label object and its `lv_obj_set_pos`, then place
`ui::HelpChip::create(<parent>, SCREEN_W - PAD - 30, SCREEN_H - 16)`. Do not repurpose the
freed rows for larger content in this task — that is per-screen work and belongs to Tasks 5-6.

- [ ] **Step 3: Assert the bottom band is clean on every touched app**

For each app, capture and run:

```bash
python3 scripts/measure-ink.py out/<app>.ppm --region 0,140,320,156 --clip 0,140,286,156
```

Expected: exit 0 — the only ink in the bottom band is the help chip, which ends at x=316.

- [ ] **Step 4: Commit**

```bash
git add src/apps/*.cpp
git commit -m "apps: remove bottom hint bars, anchor help chip bottom-right"
```

---

### Task 5: Calculus — mode chip and the box that owns the bottom

**Files:**
- Create: `scripts/gen_calculus_mode_icons.py`, `src/ui/generated/CalculusModeIcons.generated.h`, `assets/images/calculus_ddx.png`, `assets/icons/math_integral_x.svg`
- Modify: `src/apps/CalculusApp.h`, `src/apps/CalculusApp.cpp:326-480`, `src/SystemApp.cpp` (SHIFT+EXE)
- Test: `tests/emulator/scripts/calculus_chip_smoke.numos`

**Interfaces:**
- Produces: `const lv_image_dsc_t kCalcIconDeriv;` / `kCalcIconIntegral;` (A8, 26×40 source).
- Produces: `enum class CalcMode : uint8_t { Derivative, Integral };` on `CalculusApp`, with
  `CalcMode mode() const;` and `void setMode(CalcMode);`.
- Consumes: `ui::fontUi()/fontUiSmall()`, `ui::HelpChip`.

- [ ] **Step 1: Generate the two mode masks with a `--check` mode**

`assets/images/calculus_ddx.png` is the owner-supplied glyph cropped to its 26×40 ink bbox
(already produced once; commit the PNG, not the source BMP). The script reads it plus
`assets/icons/math_integral_x.svg` and emits A8 masks into
`src/ui/generated/CalculusModeIcons.generated.h`, mirroring `gen_casio_arrow_masks.py`
structure: `--check` re-renders and compares, exiting non-zero on drift.

```bash
python3 scripts/gen_calculus_mode_icons.py
python3 scripts/gen_calculus_mode_icons.py --check   # expect exit 0
```

- [ ] **Step 2: Write the failing script**

`calculus_chip_smoke.numos`: open Calculus, capture (derivative active), `SHIFT+EXE`, capture
(integral active), assert `assert_calculus_operation differentiate|integrate` after a commit in
each mode, and screenshot both. Run with `--frames 900`; expect failure on
`assert_calculus_operation integrate` because the toggle does not exist.

- [ ] **Step 3: Replace the tab strip with the row + chip**

Delete `_tabDerivative` / `_tabIntegral` and `TAB_H`. Add the `MODE_ROW_H = 18` row with the
orange `Introduce f(x):` label at `PAD` and a 60×20 two-cell chip at
`SCREEN_W - PAD - 60`. Cells draw the masks with
`lv_obj_set_style_image_recolor(accent | bgPane)`; the selected cell fills `accent` and
recolors its mask to `textOnAccent`. The input box becomes
`(PAD, BAR_H + MODE_ROW_H, SCREEN_W - 2*PAD, SCREEN_H - (BAR_H+MODE_ROW_H) - PAD)`.

- [ ] **Step 4: Wire SHIFT+EXE and re-run**

In `SystemApp::handleKey`, before app routing:
`if (ev.action == PRESS && ev.code == KeyCode::EXE && _km->isShift()) { if (activeAppId() == AppId::Calculus) { static_cast<CalculusApp*>(activeApp())->toggleMode(); } return; }`
Add `void CalculusApp::toggleMode();` to the header — it flips `_mode` and restyles the two
chip cells. No new `KeyCode` is introduced: the chord is resolved in `SystemApp`, which is the
only place that can see the `SHIFT` modifier without stealing digits from apps.

Run the script; expect exit 0 and the two captures to differ. Then the band check:

```bash
python3 scripts/measure-ink.py out/calculus_chip.ppm --region 0,18,320,38 --clip 0,18,320,38
```

Expected: exit 0 — label and chip both inside the 18px row.

- [ ] **Step 5: Commit**

```bash
git add scripts/gen_calculus_mode_icons.py src/ui/generated/CalculusModeIcons.generated.h \
        assets/images/calculus_ddx.png assets/icons/math_integral_x.svg \
        src/apps/CalculusApp.* src/SystemApp.cpp tests/emulator/scripts/calculus_chip_smoke.numos
git commit -m "calculus: mode chip replaces the tab strip; SHIFT+EXE toggles"
```

---

### Task 6: Grapher — 137px plot, floating legend, chips, 0.6x editor

**Files:**
- Modify: `src/apps/GrapherApp.h:106-135, 231-232`, `src/apps/GrapherApp.cpp:341-360, 590-690, 2622-2660`
- Test: `tests/emulator/scripts/grapher_chips_smoke.numos`

**Interfaces:**
- Consumes: `ui::HelpChip`, `ui::fontUiSmall()/fontUiXSmall()`.
- Produces: `GrapherApp::PLOT_Y`, `PLOT_H`, `GRAPH_CANVAS_W`, `GRAPH_CANVAS_H`,
  `MAX_FUNCS == 5`, `DEFAULT_X_SPAN == 20.0f`.
- Produces: `void rebuildLegend();` (absolute `lv_obj` overlay, no layout container) and
  `void openEditor(int slot);`.

- [ ] **Step 1: Write the failing script**

`grapher_chips_smoke.numos`: open Grapher, commit `y=x`, switch to Graph, flush the deferred
replot (`ac` + `0`), screenshot. Then `1`, `EXE`, screenshot the editor, `AC`. Then `AC` and
`SHIFT+7`, screenshot help. Run it; expect the plot measure below to fail (137px tall plot does
not exist yet).

- [ ] **Step 2: Change the constants, buffer and default window**

Apply spec §3.3 verbatim: `MAX_FUNCS 5`, `BAR_H 19`, `TAB_H/TOOLBAR_H/INFO_BAR_H` deleted,
`PLOT_Y = BAR_H`, `PLOT_H = SCREEN_H - PLOT_Y` (137), `GRAPH_CANVAS_*` derived from those.
In `createGraphPanel()`, drop the toolbar and info-bar containers; the plot area becomes
`(0, PLOT_Y, SCREEN_W, PLOT_H)`.

Default viewport: `_xMin=-10, _xMax=+10`, and `_yMin/_yMax` derived from
`DEFAULT_X_SPAN * PLOT_H / GRAPH_CANVAS_W / 2` about y=0. `normalizeAspect()` is **kept
unchanged** — it is now a no-op on the default view and still guards pan/zoom.

- [ ] **Step 3: Flatten the top chrome into overlays**

Mode chips: three 24×16 cells right-aligned at `y = PLOT_Y + 3`, 2px apart, 4px right margin;
active = `accent`. Legend: `rebuildLegend()` creates one 26-wide `lv_obj` overlay,
`legendH = 4 + 13*n`, `y = PLOT_Y + (PLOT_H - legendH)/2`, a row per *defined* function only
(zero functions → no legend object at all, which is Review Focus #3), each row = number in
`fontUiXSmall()` + 7px dot in the function's colour. `MAX_FUNCS` drives the table size, so the
list is dynamic up to 5 by construction.

- [ ] **Step 4: The 0.6x editor**

`openEditor(slot)` creates a 192×94 modal at screen centre (64,31) containing a `MathCanvas`
reusing the existing expression-editor construction path, a 20×12 colour-dot chip at panel
(4,78) bottom-left, and a 30×12 `EXE ✓` chip at panel (158,78). No header bar, no hint text.
`EXE` commits → `stopEditing()` + `_plotDirty = true`; `AC` closes. Re-plot must not fire while
the panel is open (Review Focus #4): keep `replot()`'s existing `_graphArea`/`_graphCanvas`
guard and assert the plot pixels outside the panel are unchanged by the screenshot diff in
Step 5.

- [ ] **Step 5: Verify geometry, all aspect scripts, and the editor overlay**

```bash
for s in grapher_aspect_circle_smoke grapher_aspect_line_smoke grapher_aspect_sin_smoke \
         grapher_aspect_sideways_parabola_smoke grapher_aspect_small_circle_smoke \
         grapher_graph_smoke grapher_chips_smoke; do
  SDL_VIDEODRIVER=dummy .pio/build/emulator_pc/program --headless --deterministic \
    --script tests/emulator/scripts/$s.numos --frames 900 --quiet >/dev/null; echo "$s exit=$?"
done
python3 scripts/measure-ink.py out/grapher_chips_smoke.ppm --region 0,19,320,156 --clip 0,19,320,156
```

Expected: every script exit 0, and the plot region holds ink (a grid plus one curve). Then
assert the grid is square by measuring tick pitch: 16 px both axes (`measure-ink.py` on a grid
row band, or the existing `compare-ppm.py`).

- [ ] **Step 6: Commit**

```bash
git add src/apps/GrapherApp.h src/apps/GrapherApp.cpp \
        tests/emulator/scripts/grapher_chips_smoke.numos
git commit -m "grapher: 137px plot, floating legend and mode chips, 0.6x editor"
```

---

### Task 7: Casio regression capture and the change record

**Files:**
- Create: `~/.hermes/change-records/2026-10-27-theme-geometry-pass.md`
- Modify: nothing (this task only measures)

**Interfaces:**
- Consumes: every prior task; produces no code.

- [ ] **Step 1: Capture the Casio layout on the three canonical screens**

Run `casio_calc_strip`, `grapher_smoke` under the Casio theme (Settings picker or the
`ALPHA+AC` hotkey), and the launcher. Save as `out/casio_after_*.ppm`.

- [ ] **Step 2: Diff against pre-pass captures and classify every difference**

```bash
python3 scripts/compare-ppm.py out/casio_before_calc.ppm out/casio_after_calc.ppm
```

Expected: differences confined to (a) the status bar's 6px height change and (b) text painted
in the Casio math face being *unmoved*. Any Casio text that shifted size means a Montserrat
rung leaked into the Casio initialiser — that is a bug, not a design change.

- [ ] **Step 3: Confirm the Casio slot clamp at `MAX_FUNCS == 5`** (Review Focus #5)

Open the Casio grapher split and count the visible numbered slots; assert 5, and that no slot
is clipped by `CASIO_COL_H`.

- [ ] **Step 4: Write the change record**

One markdown file listing every modified path, the three generated font files, the two
generated icon headers, the new scripts, PSRAM delta (+49,920 B), and the retired
byte-identical gate with the reason.

- [ ] **Step 5: Commit**

```bash
git commit -am "docs: change record for the geometry pass"
```
