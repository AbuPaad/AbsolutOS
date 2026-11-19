# NumOS — EEZ Studio UI Staging Spec

Status: PROPOSAL — no code changed yet. Owner: firmware (NumOS/AbsolutOS).
Companion to `docs/theme-manager.md` and `docs/theme-manager-casio.md`.
Audience: the solo founder (designs in EEZ Studio) + the agent (intermediary transform).

## 1. Purpose

EEZ Studio is used as a **separate staging ground** for visual UI design. The firmware
never consumes EEZ output directly. Instead:

```
EEZ Studio (design)  ->  export to staging/eez/<app>/   (raw, untouched)
                      ->  agent transform  ->  src/ui/generated/eez/<app>/  (generated, never hand-edited)
                      ->  adapter code in src/apps/ or src/ui/eez_adapter/  (hand-written glue)
                      ->  build + flash
```

The agent is the intermediary: it reads the raw export, rewrites hardcoded colors/fonts
into NumOS theme-token calls, strips the LVGL default theme, and emits the transformed
files plus a small adapter. The founder designs; the agent integrates.

This is the only sane path because:
- NumOS has a runtime `ThemeManager` (tokens, Casio/NumOS themes, runtime swap) that
  EEZ's hardcoded `lv_color_hex(...)` output cannot respect.
- `agents.md` forbids hand-editing generated headers.
- Flash is tight (~85% occupied, ~977 KiB/slot); every byte of generated code must be
  audited, not blindly shipped.

## 2. Non-goals

- Shipping EEZ's `eez-framework` / flow engine. Use **plain LVGL export only**
  (`EEZ_FOR_LVGL` undefined). The flow engine adds a large dependency + RAM cost
  and duplicates NumOS's own app/input routing.
- Replacing the hand-written LVGL apps wholesale. EEZ staging is for **new screens
  and chrome** (splash, status bar, menus, new apps) where visual iteration matters;
  existing apps stay as-is unless explicitly migrated.
- Editing raw exports in place. `staging/eez/` is read-only input.

## 3. EEZ Studio project setup (exact)

Create one EEZ project per app/screen-group, not one giant project.

| Setting | Value | Why |
|---|---|---|
| Template | LVGL | Plain LVGL export path |
| LVGL version | 9.2.x (match firmware `lvgl/lvgl@^9.2.0`) | API parity; 9.3+ risks API drift |
| Display width | 320 | canvas width (`docs/DESIGN_DIMENSIONS.md`) |
| Display height | 156 | canvas height |
| Export mode | Plain LVGL (do NOT enable EEZ Flow) | avoids eez-framework dep |
| Theme in EEZ | Define two EEZ themes: `numos` (dark) and `casio` (light LCD) | visual preview only; agent swaps colors later |
| Styles | Use EEZ "Styles" for reusable chrome (panel, row, button) | generated as `lv_style_t` → easier to retokenize |
| Fonts | Do NOT add custom fonts in EEZ; use LVGL default in designer | NumOS owns fonts via `ui::font*()`; agent replaces |
| Images | OK to add bitmaps; agent registers them in `src/ui/generated/eez/<app>/images.*` | memory cost must be audited |
| Object names | See §4 | agent maps by name |

Export target: `staging/eez/<app>/` (see §7).

## 4. Naming convention (designer-facing)

EEZ object/widget names become C identifiers in `objects_t` and are the join key
between design and firmware. Follow these exactly so the agent can map them.

**Screens:** `scr_<app>`  (e.g. `scr_grapher`, `scr_settings`). One screen per app
unless the app genuinely has tabs; then `scr_<app>_<tab>`.

**Objects (widgets):** `obj_<app>_<role>` where role is one of:
`bg`, `panel`, `header`, `title`, `body`, `row`, `row_focus`, `btn`, `btn_label`,
`label`, `label_dim`, `input`, `checkbox`, `icon`, `divider`, `footer`, `status`.

Examples: `obj_settings_panel`, `obj_grapher_title`, `obj_menu_row_focus`.

**Styles (EEZ Styles panel):** `st_<role>` — `st_panel`, `st_row`, `st_btn`,
`st_title`, `st_label`, `st_label_dim`, `st_divider`. Reuse across screens; do not
create per-screen styles.

**Rules:**
- ASCII, lowercase, underscores only. No spaces, no hyphens (they break C idents).
- Every interactive widget that NumOS routes input to must be named (unnamed widgets
  get `obj_1`-style auto names and are unaddressable).
- Text content in labels is placeholder; final strings live in firmware (i18n later).
  Use `@@TOKEN@@` placeholders for any text that must be swapped (e.g. `@@TITLE@@`).

## 5. Theme token contract

EEZ themes/styles use placeholder hex colors. The agent maps each EEZ color slot to
a NumOS `Theme` token. **Designer: pick any hex that reads right in the EEZ
simulator; the agent replaces the value, not the slot.** Keep the placeholder
consistent per role so the diff is reviewable.

### Color mapping (EEZ placeholder → NumOS token)

| EEZ role / style | NumOS token (`ui::ThemeManager::instance().current().…`) | Notes |
|---|---|---|
| Screen background | `bg` | main screen bg |
| Drawing canvas bg | `bgCanvas` | graph/math canvas only |
| Card / panel fill | `bgPane` | |
| Header / top chrome | `bgHeader` | |
| Toolbar / bottom bar | `bgToolbar` | |
| Primary text | `text` | |
| Secondary / hint text | `textDim` | |
| Text on accent/header | `textOnAccent` | |
| Primary accent | `accent` | focus edge, links |
| Second accent / series 2 | `accentSecondary` | |
| Hairline borders | `border` | |
| Focused row/card fill | `focus` | |
| Focus outline | `focusBorder` | |
| Math intermediate step | `stepHighlight` | |
| Math final result | `result` | |
| Error / warning | `danger` | |
| Graph series 1..4 | `plot[0..3]` | |
| Monochrome icon ink | `iconInk` | |
| Per-app surface (dark apps) | `appSurface(appId)` | AI, Game Boy, labs; see Theme.h |

Also `AppColours` (radiusRow, radiusPane, borderWidth) for corner/border dials —
EEZ corner radius placeholders map to `cornerRadius`/`borderWidth` tokens, not
literal px.

### Font mapping (EEZ font → NumOS accessor)

| EEZ font use | NumOS accessor (`ui::…()`) |
|---|---|
| Body labels, menus, buttons | `fontUi()` |
| Hints, status bar | `fontUiSmall()` |
| Smallest headings | `fontUiXSmall()` |
| Splash logo, headings | `fontDisplay()` |
| Editor / code | `fontMono()` |
| Math rendering | `mathFonts.*` (never EEZ) |

Rule: no app names a font face. `LV_FONT_DEFAULT` in generated code is replaced
with the appropriate accessor based on the widget role (label → fontUi, title →
fontDisplay, etc.). The agent infers role from object name + EEZ font size.

### Layout dials

EEZ corner radius / border width placeholders → `cornerRadius`, `borderWidth`,
`shadows` tokens. The agent maps by nearest role; exact values come from the
active theme, not the design.

## 6. Intermediary transform rules

Given a raw export in `staging/eez/<app>/`, the agent produces
`src/ui/generated/eez/<app>/` by applying these rules. Raw files are never modified.

**R1. Drop the LVGL default theme.** Remove:
```c
lv_theme_t *theme = lv_theme_default_init(dispp, ...);
lv_disp_set_theme(dispp, theme);
```
from `create_screens()`. NumOS owns theming via `ThemeManager`.

**R2. Tokenize colours.** Every `lv_obj_set_style_*_color(obj, lv_color_hex(0x……), …)`
becomes `lv_obj_set_style_*_color(obj, lv_color_hex(numos_theme_<token>()), …)`.
Mapping by EEZ style name + object role per §5 table. Unmapped → keep literal + flag.

**C/C++ boundary.** EEZ exports plain C; NumOS themes are C++ (`ui::` namespace/classes).
Generated `.c` files cannot call `ui::ThemeManager` directly. They go through the
C-linkage wrappers in `src/ui/ThemeC.h` (`numos_theme_bg()`, `numos_font_ui()`, …),
implemented in `src/ui/ThemeC.cpp`. When the theme contract gains a token that
generated code needs, add the wrapper in both files first. The adapter glue
(`src/ui/eez_adapter/`) is C++ and may call `ui::` directly.

**R3. Tokenize fonts.** `LV_FONT_DEFAULT`/EEZ face → `numos_font_ui()/numos_font_uiSmall()/numos_font_uiXSmall()/numos_font_display()/numos_font_mono()`
by widget role (generated .c uses the C wrappers; adapter .cpp may call `ui::font*()`).

**R4. Tokenize radii/borders.** `lv_obj_set_style_radius`, `border_width` placeholders
→ theme tokens where a matching dial exists; else keep literal and flag.

**R5. Lazy screen creation (optional, recommended).** `create_screens()` builds every
screen at boot (EEZ #676). For NumOS, split: keep `create_screen_<app>()` callable
per-app and have the app's `load()` invoke it on first use, caching the pointer.
Agent generates a `ensure_<app>_screen()` wrapper. Default: keep EEZ's all-at-once
only if total screen count ≤ 3.

**R6. Keep the objects struct.** `objects_t objects` global stays — it gives named
handles for runtime re-theming and input wiring. Agent may split into per-screen
structs if memory demands (EEZ issue #646).

**R7. Strip EEZ fonts/images unless used.** If the design uses no bitmaps, drop
`images.c`/`fonts.c` from the build filter. If used, register them in the staging
dir and audit size against flash budget.

**R8. Input wiring stubs.** EEZ generates no input handlers in plain mode. Agent
adds an `eez_adapter_<app>.cpp` that maps `objects.*` to NumOS `KeyCodes`/
`KeyboardManager` callbacks (focus, activate, back). This file IS hand-edited glue
and lives in `src/apps/` or `src/ui/eez_adapter/`.

**R9. No edits to generated files.** `src/ui/generated/eez/` is regenerated on every
transform; manual changes go in adapter files only. `agents.md` rule applies.
Consistent with the existing generators, the transform tool ships a `--check` mode
that fails when a committed generated file has drifted from its raw export — run it
in CI/pre-commit.

**R10. Transform report.** Each run emits `staging/eez/<app>/transform-report.md`
listing: files generated, color slots mapped, unmapped slots flagged, estimated
flash delta, and any `@@TOKEN@@` text placeholders needing firmware strings.

## 7. Staging directory layout

```
AbsolutOS/
  staging/               # gitignored, read-only input from EEZ exports
    eez/
      <app>/
        screens.c
        screens.h
        styles.c
        styles.h
        ui.c
        ui.h
        images.c / images.h   (optional)
        fonts.c / fonts.h     (usually dropped)
        transform-report.md    (agent-generated)
  src/
    ui/
      generated/           # agent output, never hand-edited
        eez/
          <app>/
            screens.c / .h
            styles.c / .h
            ui.c / .h
            images.*       (if used)
      eez_adapter/         # hand-edited glue
        eez_adapter_<app>.cpp / .h
```

`staging/` added to `.gitignore`. `src/ui/generated/` committed (it's the build input)
but treated as generated: changes only via transform.

## 8. Workflow (step-by-step)

1. **Founder (EEZ):** create/edit project per §3, §4. Preview in EEZ simulator.
2. **Founder:** export to `staging/eez/<app>/` (EEZ "build"/wrench icon).
3. **Founder:** notify agent ("transform <app>").
4. **Agent:** read raw export, apply R1–R10, write `src/ui/generated/eez/<app>/`
   + adapter stub, emit transform report.
5. **Founder:** review diff + transform report; resolve flagged slots.
6. **Founder:** add generated .cpp/.h to `build_src_filter` in `platformio.ini`
   (emulator + device envs) — agent lists exact lines.
7. **Founder:** build emulator, run smoke tests, build device, flash.
8. **Iterate:** re-export → re-transform → diff. Never edit generated files by hand.

## 9. Constraints & pitfalls

- **Flash budget.** Audit every transform report's flash delta against the ~977 KiB/slot
  headroom. Drop unused images/fonts first.
- **Pre-create-all-screens memory.** EEZ #676. Use R5 lazy creation for apps with
  >3 screens.
- **Theme swap at runtime.** Because colors are now token calls reading
  `ThemeManager::current()` at draw time, a theme swap + screen recreate re-tints
  automatically — but only if widgets are recreated or styles re-applied. Document
  per-app whether it rebuilds on theme change (match existing apps' pattern).
- **Custom fonts.** EEZ fonts are dropped (R7); text uses `ui::font*()`. If a
  design needs a face NumOS lacks, add it to the font ladder first, don't inline.
- **Input.** EEZ plain mode has no input; all routing is adapter code (R8). Budget
  time for this — it's the bulk of integration work.
- **No hand-editing generated.** `agents.md` rule. Adapter files are the only
  hand-edited surface.
- **Version drift.** Pin EEZ export to LVGL 9.2.x; re-verify after any EEZ upgrade.

## 10. What the agent automates vs. the founder

| Step | Agent | Founder |
|---|---|---|
| EEZ project setup / design | — | yes |
| Export to staging | — | yes |
| Transform (R1–R10) | yes | review |
| Adapter input wiring | stub only | finish + test |
| build_src_filter edits | propose lines | apply |
| Build / flash | — | yes (firmware owner) |
| Flash budget audit | propose | approve |

## 11. Appendix — before/after (color tokenization)

Raw EEZ (`staging/eez/settings/screens.c`):
```c
lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_text_color(obj, lv_color_hex(0xff2137f3), LV_PART_MAIN | LV_STATE_DEFAULT);
```

Transformed (`src/ui/generated/eez/settings/screens.c`):
```c
lv_obj_set_style_bg_color(obj, lv_color_hex(ui::ThemeManager::instance().current().bg), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_text_color(obj, lv_color_hex(ui::ThemeManager::instance().current().text), LV_PART_MAIN | LV_STATE_DEFAULT);
```

Mapping chosen from the EEZ style name (`st_panel` → bgPane, `st_title` → text) and
object role. Unmapped slots are flagged, never guessed.

## 12. Open questions

- Does the founder want EEZ bitmaps (icons) or keep the existing bundled icon set?
  (Memory cost.)
- Should theme swap rebuild EEZ screens or just re-apply styles? Needs per-app decision.
- EEZ project location: inside `staging/eez/` (exported C only) or also the `.eez-project`
  source file for version control? Recommend committing `.eez-project` under
  `docs/eez-projects/` for reproducibility.

## 13. EEZ Flow as a logic reference (optional, separate project)

EEZ Flow is EEZ Studio's visual flowchart programming layer. It can model arbitrarily
complex application logic (screen changes, conditionals, variable updates) without
leaving the designer. **In NumOS it is never shipped.** Its sole purpose is to serve
as a robust, visual, executable reference for an app's state machine and logic, which
is then translated by hand into NumOS's C++ app contract.

Use this when an app has non-trivial state (view switching, focus rings, portal
polling, multi-step wizards) and a visual flowchart is clearer than reading C++.

### Workflow difference

1. Design the UI **and** the flow in EEZ Studio with Flow enabled (project type
   "LVGL 9.0 + Flow"). Export to `staging/eez-flow/<app>/`.
2. The export defines `EEZ_FOR_LVGL` and uses `eez_flow_init`/`eez_flow_tick`
   (different code shape from plain mode).
3. The agent reads the flow-generated C as a **reference document**: extract the
   state machine, transitions, variable updates, and screen-change conditions.
4. Translate into NumOS's app contract (`begin`/`end`/`load`/`handleKey`/`update`),
   the `View` enum, focus indices, and `KeyboardManager` routing. Logic lives in
   `src/apps/<App>.cpp`, not in generated code.
5. The layout (positions, sizes, colours) is still tokenized per R1-R4 and shipped
   via `src/ui/generated/eez/<app>/` (plain-mode export) OR re-derived from the
   flow export's screen creation code.
6. The flow export itself is **not** compiled into firmware and **not** added to
   `build_src_filter`.

### What translates, what doesn't

| EEZ Flow concept | NumOS equivalent |
|---|---|
| Flow variable | member variable on the app class |
| Screen change action | `load()` / view switch (`_view = View::X`) |
| Condition / branch | `if` in `handleKey` or `update` |
| Loop / repeat | `for` in `createRows()` / `buildWifiView()` |
| Timer / delay | `update()` polling (`_lastPollMs`) |
| Event handler | `handleKey(const KeyEvent&)` case |

### Do not

- Ship `eez-framework` or flow-generated `ui.c` into firmware.
- Treat the flow export as firmware-ready code; it is a spec, not a build input.
- Let the flow's implicit state machine override NumOS's deferred-teardown and
  `KeyboardManager` routing -- those are invariants (`numos-app-development`).
