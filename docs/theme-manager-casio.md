# NumOS Master Theme Switch — per-app edit contract

Addendum to `docs/theme-manager.md`. This is the **hand-editing playbook**: the
single contract every app must obey, so a human-or-LLM editing pass can migrate
one app at a time without re-deriving the architecture. The custom theme is a
**Casio-fx-82-style simple look**: different font, sparse layout, minimal chrome —
structurally different from the modern NumOS LVGL screens, so this is a
**two-builder** approach, not a recolor.

Status: PROPOSAL — no code changed.

---

## The one rule (the contract)

Every UI-bearing app's `begin()`/`load()` starts with a **theme switch** that
selects **which of two complete screen builders runs**:

```cpp
void MyApp::begin() {
    if (numos::theme::is("casio")) {
        buildCasioScreen();   // full simple layout, casio font, own widgets
    } else {
        buildModernScreen();  // business as usual — existing code, moved verbatim
    }
}
```

`begin()` is the **only** per-app edit point. Everything that makes the two looks
different (fonts, spacing, nesting, which widgets exist, chrome) lives **inside**
`buildCasioScreen()` / `buildModernScreen()`. Nothing else in the file changes.

## How an app reports its theme (from `SystemApp`, per your ask)

Theme identity is a **string** owned by `SystemApp` (the dispatcher), one
function to read it — no global, no coupling:

```cpp
// SystemApp.h — public, stable
namespace numos { namespace theme {
    // "modern" | "casio" (default "modern"). Persisted via /settings.dat.
    const char* current();        // cheap, call once, cache the result
    bool is(const char* name);    // strcmp wrapper, safe to call at render time
    void select(const char* name); // change + persist + schedule rebuild
}}
```

Apps call **`is("casio")` once in `begin()`**. That single read drives the branch.
The render path never needs the string again.

## The theme string is a build-time/normal UPPER layer; the palette follows

Because the casio look also needs its own colors, `ui::tcol(token)` /
`ui::col(token)` from `theme-manager.md` **key off the same string**. Wiring one
place (`numos::theme::current()`) feeds both the `begin()` branch and the color
indirection. Set it up as:

```cpp
// ui/Theme.h
inline uint32_t tcol(int token) {
    return numos::theme::is("casio") ? CASIO_PALETTE[token] : MODERN_PALETTE[token];
}
```

So even apps you don't structurally redo still pick up casio colors if their
`COL_*` are macro-aliased. Two orthogonal switches, one source of truth.

---

## Per-app edit checklist (mechanical)

For each app below, the edit is 4 steps. The plan is ordered so you can stop
having coverage at any point — unedited apps just keep the modern look.

1. Add `#include "../ui/Theme.h"` (already exists) at the top.
2. Read the theme once: `bool casio = numos::theme::is("casio");`
3. At the top of `begin()` (or the first `_screen` builder), branch:
   ```cpp
   if (casio) { buildCasioScreen(); } else { buildModernScreen(); }
   ```
4. **Move the existing modern-build code untouched** into `buildModernScreen()`
   (a rename + wrap). Write a fresh `buildCasioScreen()` for the dummy-computer
   look. Leave the app's `COL_*` palette in place — `buildModernScreen()` keeps it.

That is the whole diff per app. It is small, readable, and reversible.

## App registry (from MainMenu.cpp APPS[] + SystemApp::begin())

| # | App | File(s) | UI body | Priority for casio look |
|---|-----|---------|---------|------------------------|
| 0 | Calculation | CalculationApp.{h,cpp} | LVGL keypad+display | **HIGH** — this *is* the calculator face |
| 1 | Grapher | GrapherApp.{h,cpp} | LVGL graph canvas | MED |
| 2 | Equations | EquationsApp.{h,cpp} | LVGL solver | MED |
| 3 | Calculus | CalculusApp.{h,cpp} | LVGL derivative/integral | LOW |
| 4 | Statistics | StatisticsApp.{h,cpp} | LVGL tables | LOW |
| 5 | Probability | ProbabilityApp.{h,cpp} | LVGL | LOW |
| 6 | Regression | RegressionApp.{h,cpp} | LVGL | LOW |
| 7 | Sequences | SequencesApp.{h,cpp} | LVGL | LOW |
| 8 | Python | PythonApp.{h,cpp} | LVGL editor | MED |
| 9 | Matrices | MatricesApp.{h,cpp} | LVGL | LOW |
| 10 | Settings | SettingsApp.{h,cpp} | LVGL rows | **HIGH** — hosts the theme picker |
| 11 | Periodic Table | PeriodicTableApp.{h,cpp} | LVGL | LOW |
| 12 | Bridge | BridgeDesignerApp.{h,cpp} | custom-draw engine | OFF (engine look intrinsic) |
| 13 | Circuit | CircuitCoreApp.{h,cpp} | custom-draw engine | OFF |
| 14 | Fluid 2D | Fluid2DApp.{h,cpp} | custom-draw engine | OFF |
| 15 | ParticleLab | ParticleLabApp.{h,cpp} | custom-draw engine | OFF |
| 16 | Neural Lab | NeuralLabApp.{h,cpp} | custom-draw engine | OFF |
| 17 | OpticsLab | OpticsLabApp.{h,cpp} | custom-draw engine | OFF |
| 18 | NeoLang | NeoLanguageApp.{h,cpp} | LVGL editor | LOW |
| 19 | Fractals | FractalApp.{h,cpp} | custom-draw | OFF |
| 21 | Game Boy | GameBoyApp.{h,cpp} | LVGL front-end | MED (keep GB screen its own look) |
| — | Menu/StatusBar | ui/MainMenu.cpp, ui/StatusBar.cpp | LVGL launcher + 24px bar | **HIGH** |

## Casio font

`lv_conf.h` sets `LV_FONT_DEFAULT &lv_font_montserrat_14`. The casio builders
select a distinct font explicitly on their widgets (e.g. a bundled pixel/
segment-style font in `src/fonts/`, or an existing GLCD font via
`lv_obj_set_style_text_font`). Do **not** change `LV_FONT_DEFAULT` globally — the
modern look and the custom engines keep Montserrat. Font choice stays inside each
`buildCasioScreen()`.

---

## Runtime switch flow (user-facing)

1. SettingsApp (id 10) gains an "Appearance" row listing themes
   (`modern`, `casio`).
2. Selecting one calls `numos::theme::select(name)` → persists to `/settings.dat`
   (additive field, default `modern` so old files load fine).
3. `SystemApp` schedules a rebuild: return to menu on the next clean transition
   (respect the LVGL teardown rule — never delete a screen mid-animation; apply on
   the newly-built screen, same pattern as `returnToMenu()` + deferred `end()`).
4. Each app's next `begin()` reads the new string and picks the right builder.

## Verification

- `pio run -e emulator_pc` — every app compiles; unedited apps unchanged.
- Boot in default `modern` — byte-identical screens to today (proves the move was
  a pure rename + branch, no behavior change).
- Switch to `casio` in Settings — Menu, StatusBar, Calculation, Settings show the
  simple look; un-migrated apps still show modern. No crash on the cross-app
  transition.

## Implementation order (if you go ahead)

1. `numos::theme` string API (new, tiny) + `ui::tcol` palette-indirection.
2. Migrate **Settings + Menu + StatusBar** to the branch (they're the skin).
3. Migrate **Calculation** to the casio two-builder (the payoff).
4. Wire a Settings "Appearance" row + persistence.
5. Migrate other LVGL apps MED/LOW as wanted.

---

# Grapher — worked example (how to theme a "radically different" app)

This is the case study for the hard case you asked about. Grapher is
structurally complex (LVGL chrome **and** a pixel-level renderer), yet theming
it reduces to **two clean edits** because its colors live in exactly three
places.

## The three color systems in Grapher

| System | Where | Source | Does it follow the theme? |
|---|---|---|---|
| LVGL chrome (`COL_*`: tab bar, pills, toolbar, buttons) | `GrapherApp.cpp` top block | `static constexpr uint32_t COL_*` | Compile-time constants |
| Grid + axes in the pixel buffer | `GraphView.cpp` | **hardcoded** `0xE0E0E0` / `0x333333` inside `drawGrid()`/`drawAxes()` | **Ignored the palette entirely** |
| Function curves | GrapherApp passes `rgbColor` arg | `FUNC_COLORS[i]` | Passed per-draw |

The LVGL widget tree is built in `GrapherApp::createUI()` (reached from
`begin()`, guarded by `if (_screen) return;`), destroyed in `end()`, shown via
`lv_screen_load_anim(_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 200, 0, false)` in
`load()`.

## Edit 1 — the LVGL shell: two builders off `begin()`

This is the *layout* part you expect to look radically different (casio: flat,
sparse, tiny margins, a basic fx-82 face instead of the tabbed NumWorks chrome).

```cpp
void GrapherApp::begin() {
    if (_screen) return;
    if (numos::theme::is("casio")) createUISimple();  // new: sparse casio shell
    else                           createUI();         // existing, moved as-is
    _tab = Tab::EXPRESSIONS; _focus = Focus::TAB_BAR; _tabIdx = 0;
    switchTab(_tab);
}
```

Mechanical steps (per the master contract):
1. Rename the existing `createUI()` body to `createUISimple()`-equivalent tag — or
   simply gate it: keep `createUI()` as the modern builder and add a new
   `createCasioUI()` that *only* builds the casio widget set.
2. `createCasioUI()` is written fresh: same member widgets (`_screen`,
   `_graphArea`, `_tabBar`, `_panelExpr`…), same names, so the rest of
   `GrapherApp` (key handling, `switchTab`, trace, plot) keeps working untouched.
   The members are already declared in `GrapherApp.h` — you only change what is
   created, not the handles.
3. All `COL_*` constants stay for the modern builder. `createCasioUI()` uses its
   own small casio palette (or `ui::col(token)` if you want them theme-driven).

## Edit 2 — the grid/axes: de-hardcode the two colors

Because `drawGrid()`/`drawAxes()` bake colors in, the casio plot area would stay
modern-white-grey no matter what the shell does. Fix by making them **stable
members** the app can set (default = current values, so nothing changes):

```cpp
// GraphView.h — add
void setGridColor(uint32_t rgb);   // default 0xE0E0E0
void setAxisColor(uint32_t rgb);   // default 0x333333

// GraphView.cpp — replace the hardcoded locals
void GraphView::drawGrid() {
    uint16_t gridColor = utils::rgb888to565(_gridColor);   // was 0xE0E0E0
    ...
}
void GraphView::drawAxes() {
    uint16_t axisColor = utils::rgb888to565(_axisColor);   // was 0x333333
    ...
}
```

Then `GrapherApp` sets them once in `begin()`/`load()`:

```cpp
_graphView->setGridColor(casio ? 0xD8D8D8 : 0xE0E0E0);
_graphView->setAxisColor(casio ? 0x000000 : 0x333333);
```

## Edit 3 (optional) — function-curve colors

`FUNC_COLORS[i]` are already passed per-draw, so if the casio look wants different
curve colors you only touch the `FUNC_COLORS` array — either make it branch in
`begin()` or index a second casio array. No engine changes.

## What you do NOT touch

- Sampling, adaptive refinement, Bresenham rasterization, viewport math,
  stipple/render paths in GraphView — a plotted function is identical under both
  looks.
- `end()` teardown, key handling, `switchTab`, trace/POI logic.

## Why this is the right seam

The thing you feared — "colors scattered through the cpp" — does not happen with
Grapher because all chrome colors are already collected in one `COL_*` block at
the top and all widget creation is funneled through `createUI()` + the tiny
`makeContainer()`/`makeLabel()` helpers. The only truly scattered piece (grid/
axes) was confined to exactly two functions. Two edits, one per lookup surface.

## Build + verify for this app

- Everything compiles: `pio run -e emulator_pc`.
- Default `modern` → Grapher renders **byte-identical** to today (proves the
  `begin()` branch and the de-hardcoding are no-ops at the default).
- `casio` → sparse shell, simple plot, correct grid/axis contrast. Switch to
  Graph tab, plot the template functions, confirm trace/plot still run on the new
  widget handles.