# NumOS UI — Product Requirements for a Designer

> Scope: the **on-screen UI** of NumOS (firmware). Hardware/chassis specs live in `DESIGN_DIMENSIONS.md`. This doc is the contract a designer hands back screens against. Source of truth for tokens: `src/ui/Theme.h`, `src/ui/Theme.cpp`, `src/ui/ThemeManager.h`, `src/Config.h`. Recon: `notes`/reports from the theme-system skill.

---

## 1. Product context

- **Product:** drop-in replacement PCB for the Casio fx-82 shell. Calculator first — "keypress → math immediately, menus tucked away." Not a pocket computer with a calculator app.
- **Two looks, one device:** `NumOS` (modern, NumWorks-inspired) and `Casio ClassWiz` (authentic LCD). Switchable at runtime via `ALPHA`+`AC` (doc 10) and the Settings picker. A theme = **layout + interaction + skin**; layouts are only two ever (`NumOS`, `Casio`). Skins (palette/fonts/chrome) are the open third-party slot.
- **No touch.** Keypad-only input. All focus/selection is keyboard-driven.
- **Two SKUs of feel:** modern rounded (NumOS) vs crude LCD (Casio). Casio is **not** NumOS recoloured — different arrangement of the same pixels.

## 2. Canvas & resolution (hard constraints)

| Item | Value | Source |
|---|---|---|
| Glass panel | ILI9341 320×240, landscape rot 1 | `src/Config.h:21,214` |
| Exposed canvas (firmware) | **320 × 156 px** | `src/Config.h:205-213` |
| Vertical offset | +54 px from top (54 top, 30 bottom black bars) — **not centered** | `src/Config.h:177-189` |
| Host sim default | device proxy 320×156 @ +54; `NUMOS_HOST_FULL_PANEL` → 320×240 offset 0 (WASM) | `src/Config.h:190-213` |
| Status bar height | 24 px (NumOS) | `src/ui/StatusBar.h:46` |
| Design must fit 320×156 | black bars are produced by the offset, never by app code | `src/Config.h:183-185` |

**Rule for designer:** every screen is authored at 320×156. Do not design for 320×240 and crop. Geometry constants (`SCREEN_W/H`, `StatusBar::HEIGHT`, `TAB_H`, `TOOLBAR_H`, `CARD_W/H`) are single-source in `Config.h`; a theme **tints**, it never moves.

## 3. Design systems

### 3.1 NumOS theme — modern

**Palette (RGB888, `src/ui/Theme.cpp:97-140`):**

| Token | Hex | Role |
|---|---|---|
| `bg` | `#FFFFFF` | main screen background |
| `bgCanvas` | `#FFFFFF` | drawing surface / math graph canvas |
| `bgPane` | `#F5F5F5` | cards / rows / panels fill |
| `bgHeader` | `#FF9900` | top chrome / status bar fill (NumWorks orange) |
| `bgToolbar` | `#FF9900` | bottom / tab toolbar fill |
| `text` | `#000000` | primary ink |
| `textDim` | `#808080` | secondary / hint ink |
| `textOnAccent` | `#000000` | text on accent/header fills |
| `accent` | `#FF9E00` | primary accent |
| `accentSecondary` | `#FFB531` | second accent / chart line 2 |
| `border` | `#D0D0D0` | hairline borders |
| `focus` | `#E3F2FD` | focused row/card fill |
| `focusBorder` | `#4A90D9` | focus outline |
| `stepHighlight` | `#1565C0` | math intermediate step |
| `result` | `#E05500` | math final result |
| `danger` | `#B71C1C` | errors / warnings |
| `plot[0..3]` | `#1565C0 #E05500 #2E7D32 #6A1B9A` | graph series ramp |
| `iconInk` | `#FFFFFF` | monochrome provider marks on dark rows |

**Chrome dials:** `cornerRadius 6`, `borderWidth 0`, `shadows true`, `graphLineWidth 1` (`src/ui/Theme.cpp:123-127`).

**Typography ladder:**

| Rung | Face | Size | Role |
|---|---|---|---|
| `fontUi` | Montserrat | 14 | body: labels, menus, buttons |
| `fontUiSmall` | Montserrat | 12 | hints, status bar |
| `fontUiXSmall` | Montserrat | 10 | smallest heading |
| `fontDisplay` | Montserrat | 20 | splash logo, headings |
| `fontMono` | unscii-8 | 8 | editor / code |
| `mathFonts` | STIX | 18/12/8 (ems 18/12/8) | math expressions |

Splash: "NumOS" logo in `fontDisplay` orange `#FF9500` centered; version line `fontUiSmall` grey `#909090` bottom (`src/ui/SplashScreen.cpp:53-72`).

**Launcher:** 24 px orange status bar + 3-column card grid on `#F5F5F5` with a dot grid, scrollable (`src/ui/MainMenu.cpp:104-110,625-688`). Card: **94×78 px**, radius 8, icon box **44×44 radius 10**, 6 px gaps, 8 px grid padding (`src/ui/MainMenu.cpp:73-83`). Focused card: 3 px blue border `#0074D9`, transparent fill, card shadow 4 px @ 20% (`src/ui/MainMenu.cpp:1132-1154`). Grid navigates 3 columns with horizontal/vertical wrap-around.

**Interaction model (NumOS):** CardGrid launcher · Grid2D focus · Scroll paging · wrap true · focusFill true · softkeyRow false · numberedSlots false · scrollChevron false · splitGraph false · back ToLauncher · tallListRows false (`src/ui/nav/InteractionModel.cpp:26-40`).

**Dark surfaces (stay dark, must not turn green):** App ids with overrides — `kGrapher=1`, `kGameBoy=21`, `kAi=23` (`src/ui/Theme.h:100-104`). `kNUMOSSurfaces` pins GameBoy black `#000000`/rows `#1C1C1C`, AI black `#000000`/pane `#101418`/text `#DDDDDD`, Grapher bg `#F5F5F5`/accent `#4A90E2` (`src/ui/Theme.cpp:43-92`). Reason: dark for legibility; numos white/black tokens cannot express them.

### 3.2 Casio theme — crude LCD

**Palette (RGB888, `src/ui/Theme.cpp:142-185`):** monochrome LCD green, all chrome the same green.

| Token | Hex | Role |
|---|---|---|
| `bg`/`bgCanvas`/`bgPane`/`bgHeader`/`bgToolbar` | `#78864E` | LCD green (everything) |
| `text` | `#000000` | primary ink |
| `textDim` | `#3E6B2F` | secondary ink |
| `textOnAccent` | `#FFFFFF` | text on accent |
| `accent` | `#333A21` | dark accent |
| `accentSecondary` | `#1B5E20` | second accent |
| `border` | `#2E5016` | visible dark hairline |
| `focus` | `#1E88CA` | focused row fill |
| `focusBorder` | `#0B3D91` | focus outline |
| `stepHighlight` | `#0B3D91` | math step |
| `result` | `#001405` | math result |
| `danger` | `#B71C1C` | errors |
| `plot[0..3]` | `#0B3D91 #C62828 #2E7D32 #6A1B9A` | graph series |
| `iconInk` | `#000000` | black mark on light LCD rows |

**Chrome dials:** `cornerRadius 0`, `borderWidth 1`, `shadows false`, `graphLineWidth 2` (`src/ui/Theme.cpp:168-171`).

**Typography:** single CC-BY-SA math face (Casio-Calculator-Font) at **18/12/12/18**; `fontMono` stays unscii-8. Math font set `casio_math` 18/12/8 with ems 18/12/8. No 26 pt rung — display collapses to 18 (`src/ui/Theme.cpp:173-182`, `src/ui/Theme.h:202-203`). Licence: CC-BY-SA-3.0 — attribution + ShareAlike notice required on the generated `.c` (`assets/fonts/LICENSES/`).

**Launcher:** 4×2 numbered MenuList pages (p1/p2 = 8 slots, p3 = 6), full-bleed, **no status bar** (`src/ui/MainMenu.h:118,123`).

**Calculation:** suppresses the NumOS StatusBar; thin Casio strip — mode letter left, "Math" + history arrow right; content-sized input band (`src/apps/CalculationApp.cpp:186,204`; `src/apps/CalculationApp.h:135-144`).

**Grapher:** split two-pane — numbered function list / value table on the left, live graph on the right (`src/ui/nav/InteractionModel.h:82-87`; `.cpp:55`).

**Softkey row:** shown at bottom under Casio profile (`src/ui/nav/InteractionModel.h:73`; `.cpp:50`).

**Interaction model (Casio):** MenuList launcher · Linear focus · PageTurn · wrap false · softkeyRow true · focusFill false (ink-only focus) · numberedSlots true · scrollChevron true · splitGraph true · back ToLauncher · tallListRows true (29 px rows, 3 whole rows) (`src/ui/nav/InteractionModel.cpp:42-58`).

**Dark surfaces:** `appSurfaces = nullptr` — apps inherit theme tokens; no per-app dark overrides (`src/ui/Theme.cpp:184`). All apps turn LCD-green.

## 4. Types of objects on screen (inventory)

Designer must deliver specs for each:

1. **Status bar (NumOS only)** — 24 px bar: clock, centered title, modifier, DEG/RAD badge, battery icon, bottom separator. Uses `fontUiSmall`; colours `COL_BG #1A1A1A`, `COL_TEXT #CCCCCC`, `COL_SEP #333333`, modifier `#FFD700`, battery `#66CC66`/`#FFCC00`/`#FF4444` (`src/ui/StatusBar.h:46-50`; `src/ui/StatusBar.cpp:89,116,279-284`).
2. **Launcher cards (NumOS)** — 94×78, radius 8, icon box 44×44 radius 10.
3. **Menu list rows (Casio)** — 29 px tall, numbered slots, ink-only focus.
4. **Calculation strip (Casio)** — mode letter + "Math" + history arrow.
5. **Math expression area** — `MathCanvas` renders AST with math fonts; 3 px cursor blinking 500 ms (`src/ui/MathRenderer.h:161,244-246`).
6. **Graph view** — RGB565 PSRAM buffer, typical 320×160, white clear, grid/axes, curve stroke via `setCurveWidth` (`src/ui/GraphView.h:47,61,128-133`).
7. **Settings rows** — corner radius 6 (`src/ui/Theme.cpp:123`).
8. **Splash** — logo + version line.
9. **Softkey row (Casio)** — bottom softkeys.
10. **Dialogs / numbered menus** — Casio-style numbered slots.
11. **AI app icons / provider marks** — monochrome marks via `iconInk`.
12. **GameBoy frame** — black letterbox.

## 5. Color usage rules (goals)

- `bg` = main screen background; `bgCanvas` = drawing surface (math graph) — dark apps keep their canvas and adopt themed chrome/accent (open decision resolved: `bgCanvas` exists exactly for this).
- `bgPane` = cards/rows/panels fill; `bgHeader`/`bgToolbar` = chrome bars.
- `text`/`textDim` = primary/secondary ink; `textOnAccent` = ink on header fills.
- `accent` = primary accent (actions, selection); `accentSecondary` = second accent / chart line 2.
- `border` = hairlines; `focus`/`focusBorder` = keyboard focus (fill + outline).
- `stepHighlight`/`result` = math semantics (intermediate step vs final result) — do not reuse for decoration.
- `danger` = errors/warnings.
- `plot[4]` = graph series ramp (4 distinct colours).
- `iconInk` = monochrome provider-mark ink (white on numos dark rows, black on casio LCD rows).
- Dark apps (AI, GameBoy, Grapher) keep their own surface via `appSurfaces`; never force them to the theme `bg`.

## 6. Typography rules

- **No app names a font face.** Apps call `ui::fontUi()/fontUiSmall()/fontUiXSmall()/fontDisplay()/fontMono()` (`src/ui/ThemeFonts.h:24-43`), live reads of `ThemeManager::current()`.
- `buildLvglTheme()` passes the theme's `fontUi` as LVGL's default font, so widgets inherit for free.
- NumOS: Montserrat 14/12/10/20 + unscii-8 mono; math STIX 18/12/8.
- Casio: single CC-BY-SA face 18/12/12/18; mono unscii-8.
- If a design needs a face NumOS lacks, add it to the font ladder first (with licence + provenance header) — never inline a face in generated code.

## 7. Icons & graphics

- **NumOS:** 64×64 RGB565 bitmaps in PROGMEM (`src/ui/Icons.h`); MainMenu renders geometric vector icons per app id on 44 px rounded boxes using white/light accent (`src/ui/MainMenu.cpp:749-778`); AI app uses generated `AiModelIcons` (`src/apps/AiApp.cpp:45`).
- **Casio:** page-turn arrows are A8 alpha masks recoloured at draw time via `image_recolor` (`src/ui/generated/CasioArrowMasks.generated.h:8-13`); bundled launcher icons 64×64 RGB565; AI icons use `iconInk` via `lv_obj_set_style_image_recolor` (`src/apps/AiApp.cpp:1044-1049`).
- **IconLib rule:** apps name symbols, never files/array names. Resolve chain: theme override → layout default → never fail. `IconId.h` is the vocabulary; adding an icon is one enum entry; each layout's set carries a `static_assert` on `Icon::_Count` so a forgotten icon is a compile error (`numos-theme-system` skill).
- **Formats:** NumOS RGB565/indexed; Casio A1 1-bit masks recoloured by theme (~8.4 KB for 30 icons).
- **ArtStyle** covers procedural art (graph traces, optics, particles): `lineWidth`, `antiAlias`, `filled`, `markersHollow`.

## 8. Interaction / navigation

| Aspect | NumOS | Casio |
|---|---|---|
| Launcher | CardGrid | MenuList (4×2 pages) |
| Focus topology | Grid2D | Linear |
| Paging | Scroll | PageTurn |
| Wrap | true | false |
| focusFill | true | false (ink-only) |
| softkeyRow | false | true |
| numberedSlots | false | true |
| scrollChevron | false | true |
| splitGraph | false | true |
| tallListRows | false | true (29 px, 3 whole rows) |
| back policy | ToLauncher | ToLauncher |

Keymap is **shared** across themes (same verbs, different topology). A theme never authors a keymap. Navigation behaviour differs between NumOS and Casio (grid vs list, 2D focus vs linear, scroll vs page-turn, softkey row, back policy).

## 9. Designer constraints (do-nots)

1. **No literals in apps.** Any `lv_color_hex(0x…)` or `COL_*` in `src/apps/` is a bug. Tokens only.
2. **Geometry is single-source.** `SCREEN_W/H`, `StatusBar::HEIGHT`, `TAB_H`, `TOOLBAR_H`, `CARD_W/H` stay in `Config.h`. A theme tints, it does not move.
3. **Dark-canvas apps must not turn green.** Use `bgCanvas` for drawing surfaces; keep chrome/accent themed.
4. **Casio is not NumOS recoloured.** Different arrangements of the same pixels.
5. **Screen size is the only shared constant** between the two layouts.
6. **Theme tints, never resizes.** Layout never changes geometry metrics.
7. **Designer-generated code (EEZ/SquareLine/LVGL Pro):** drop the designer's default theme (`lv_theme_default_init` fights `ThemeManager`); tokenize by style name + object role, never by eye; `@@TOKEN@@` placeholders for text; regen overwrites patches (never hand-edit generated files; integration goes in `src/ui/eez_adapter/`); designer fonts are dropped (use `ui::font*()` accessors); generated C needs C-linkage wrappers via `src/ui/ThemeC.h`.

## 10. Deliverables expected from designer

- Refined palette for both themes (tokens already defined; designer may propose adjustments).
- Component specs with dimensions/states: status bar, launcher card/menu row, calc strip, math area, graph, settings row, splash, softkey row, focus states, dialogs.
- Icon sets per layout: NumOS RGB565 + vector geometric icons; Casio A1 1-bit masks + numbered glyphs.
- Font choices with licence + provenance (OFL for Montserrat/STIX; CC-BY-SA for Casio face).
- Dark-surface app palettes: AI, GameBoy, Grapher (and the lab/visualisation family: Optics, Neural, Particle, Fluid2D, PeriodicTable, Tutor — keep dark).
- Interaction-state visuals: focus, selected, disabled, error, for both layouts.

## 11. Open questions

- Third-party skin authoring surface (open fork: LVGL XML licence forbids third-party-facing editors; public surface TBD).
- Whether the lab/visualisation family (Optics/Neural/Particle/Fluid2D/PeriodicTable/Tutor) gets `bgCanvas` overrides or stays fully dark.
- SD card absent on v1; no file-picker UI needed for v1.
- Camera module sold as add-on; no camera-preview UI in v1 scope.

---

*Source: `src/ui/Theme.h`, `src/ui/Theme.cpp`, `src/ui/ThemeManager.h`, `src/Config.h`, `src/ui/MainMenu.*`, `src/ui/StatusBar.*`, `src/ui/SplashScreen.*`, `src/ui/MathRenderer.h`, `src/ui/GraphView.h`, `src/nav/InteractionModel.*`, `src/apps/CalculationApp.*`, `src/ui/Icons.h`, plus skills `numos-theme-system` and `numos-project-vision`.*