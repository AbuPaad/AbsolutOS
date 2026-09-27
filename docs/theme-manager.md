# NumOS UI Theme Manager — Implementation Plan (additive)

Status: PROPOSAL — no code changed yet. Branch: `gameboy-app`.
Scope: zero-rewrite of the existing LVGL apps. All changes are additive.

---

## 1. What exists right now (grounded in the repo, from repomix + skills)

There is **no global theming**. Colors are defined in three unrelated, duplicated places:

1. `src/ui/Theme.h` — a flat header of `static const uint16_t COLOR_*`/`FONT_*`
   RGB565 constants. Consumed by exactly **2** files: `SystemApp.cpp` and
   `display/DisplayDriver.cpp` (legacy direct-TFT `drawStatusBar`/`renderMenu`).
2. `src/apps/*.cpp` — **~30 LVGL-native apps, each with its own private
   `static constexpr uint32_t COL_*` palette** (242 definitions across the tree)
   applied inline as `lv_color_hex(COL_X)` on their own widgets/screens.
   Examples confirmed: `BridgeDesignerApp` (`0x0D1117` dark), `NeuralLabApp`,
   `SettingsApp` (`0xFFFFFF` light), `MainMenu` has its own
   `COL_STATUS_BAR = 0xFF9900` set. These palettes are mutually inconsistent
   (some light, some dark) and do not read `Theme.h` at all.
3. `src/utils/ColorUtils.h` — a **shared, already-correct** `utils::rgb888to565()`
   constexpr converter (RGB888 → RGB565). This is the seam to build on.

Dispatch: `SystemApp` (`src/SystemApp.{h,cpp}`) is the single owner — `Mode`
enum, `begin()` allocates every app with `new XxxApp()`, `launchApp(id)` →
`switchApp(id)` → `app->load()`, `drawStatusBar()`. LVGL 9.2 is in use and
`LV_USE_THEME_DEFAULT 1` is already on. `build_src_filter` for `[env:emulator_pc]`
is an **explicit no-glob file list** (every new `.cpp` must be added by name).

## 2. The core idea (why this is additive)

Do **not** touch every app. Instead:

- **A `ThemeManager` owns the active `Theme`** (a single struct of named palette
  tokens — colors already managed as RGB888 `uint32_t`, converted to RGB565 at
  boundary via the existing `utils::rgb888to565()`).
- The **dispatcher (`SystemApp`) owns the `ThemeManager`** as a member, exactly
  as you described: "sysdispatch has the theme defined, each app calls that."
- Each app actively themed owns is given a **unchanged API**: it either
  (a) reads tokens from the manager, **or** (b) keeps its existing `COL_*`
  constants but sourced from the theme. Non-migrated apps **keep compiling and
  rendering as today** because their palettes stay local — nothing is removed,
  renamed, or forced through the manager.

"Additive" means: introducing the manager + tokenized Theme is a **superset**.
Every existing app compiles with zero edits. Migrating an app is an opt-in,
mechanical swap (drop its private `COL_*`, include the token header) with a
safety fallback to its current values.

## 3. New files

| Path | Purpose |
|---|---|
| `src/ui/ThemeTokens.h` | Canonical palette token names + semantic mapping (RGBA→role). |
| `src/ui/TexTheme.h` / `src/ui/Themes.h` | The `Theme` struct + built-in palettes (e.g. NumWorks Light, Dark). |
| `src/ui/ThemeManager.h` / `.cpp` | Runtime holder, active-theme pointer, `apply()` re-tint hook, persistence dispatch. |
| `src/ui/ThemeMigration.h` | (optional) compile-time migration helper so Apps can alias `COL_BG`→token safely. |
| `docs/theme-manager.md` | This plan + usage notes (reuse as reference/skill). |

## 4. Theme data model

```cpp
// ThemeTokens.h — semantic token names (identifiers become the API)
enum class ThemeToken : uint8_t {
    Background, Surface, SurfaceAlt, Text, TextMuted, TextOnAccent,
    Primary, PrimaryHover, Accent, Border, Focus, Success, Warn, Danger,
    StatusBarBg, StatusBarText, CardBg, DotGrid, GraphGrid, ...
};

struct Theme {                       // Themes.h
    const char* name;
    uint32_t colors[(int)ThemeToken::Count];   // RGB888, 0xRRGGBB
    bool     dark;
};
```
- Store **RGB888** in the struct (human-readable, consistent with every app's
  own `0x0D1117` style). Convert to RGB565 only when handed to the display, via
  the **existing** `utils::rgb888to565()`.
- Fonts stay as-is (single global `LV_FONT_DEFAULT` set in `lv_conf.h`), so the
  first version is color-only. Sizes/radii/shadow live in `lv_conf`/App layout
  and are **out of scope**.

## 5. ThemeManager (owned by SystemApp)

`src/ui/ThemeManager.{h,cpp}` — a plain class (managed object, not a singleton),
declared in `SystemApp.h`, constructed in `SystemApp::begin()`:

```cpp
class ThemeManager {
public:
    void  init();                       // pick persisted theme, set as active
    const Theme& current() const;       // app-facing getter
    void  setTheme(const Theme&, bool persist);
    void  apply();                      // re-tint active LVGL chrome (status bar etc.)
    uint32_t raw(ThemeToken) const;     // RGB888
    lv_color_t rgb(ThemeToken) const;   // rgb888to565 -> lv_color
private:
    const Theme* _active;
};
```

- `SystemApp.h` has `ThemeManager _themeManager;` (+ a public `themeManager()`
  accessor / `currentTheme()`).
- `begin()`: `_themeManager.init()` runs **before** any app screen is built, so
  `load()` sees the right theme from the start.
- **Persistence**: reuse the existing pattern in `SettingsApp.cpp`
  (`/settings.dat`, present). Store the active theme id alongside the existing
  settings record; extend the read/write paths additively (new field, default to
  theme 0 if absent — backward compatible with old files).

## 6. How apps get the theme (the dispatch, per your ask)

Three tiers, all additive:

**Tier A — Apps that opt in** (recommended for Settings + new apps like GameBoy):
- Constructor takes no theme arg (keep the `new XxxApp()` alloc in
  `SystemApp::begin()` unchanged). Apps call the **dispatcher**:
  `SystemApp::instance()->themeManager().rgb(ThemeToken::Primary)` — but to avoid
  a global, simplest correct seam: pass nothing; apps that need the theme receive
  it via their existing `load()`/`begin()` signature widened with a
  `const Theme&` **default-parameter** (so the old 1-arg call sites still
  compile): `void load(const Theme& t = SystemApp::defaultTheme())`. The default
  keeps the file building pre- and post- migration.

**Tier B — Mechanical migration for existing apps** (SettingsApp, MainMenu,
StatusBar, and any app you want to actually follow a dark theme):
- Replace the app's `static constexpr uint32_t COL_*` block with includes +
  aliases pointing at the token stream, keeping a FALLBACK to the app's current
  values so the picture is bit-identical until a theme explicitly overrides it:
  ```cpp
  // in app .cpp
  #include "ui/ThemeTokens.h"
  #define COL_BG  ui::rgb(ThemeToken::Background)      // falls back internally
  ```
- This is a mechanical find/replace per file; the app's own `lv_color_hex(COL_X)`
  call sites need **zero** changes.

**Tier C — Leave as-is** (deep-engine apps whose palette is intrinsic to the
renderer: `BridgeDesignerApp`, renderers using `ColorUtils::rgb888to565`
directly). Documentation states these are intentionally non-theme-following.

## 7. Re-tinting live chrome (SystemApp's own UI)

`SystemApp::drawStatusBar()` / `renderMenu()` / `renderAppView()` currently read
`COLOR_*` from `Theme.h`. Additive change:
- Have these read `_themeManager.rgb(ThemeToken::StatusBarBg)` etc.
- **Keep `Theme.h` in place** as the default/fallback source so nothing breaks;
  the manager is seeded with the equivalent NumWorks values.

## 8. Changing the theme at runtime (the user-facing path)

- Settings app gets a new "Appearance / Theme" row (Settings already persists via
  `/settings.dat`). Selecting a theme calls `themeManager().setTheme(...)`,
  persists, then:
  - re-applies the status bar + menu chrome (`apply()`),
  - tells the **active** app to rebuild. LVGL 9 pattern: destroy the shared chrome
    and call `app->load()` again after `lv_obj_clean()` of the screen — reuse the
    app teardown path already present in `SystemApp::teardownModeNow()` +
    `returnToMenu()`. Do **not** animate the theme swap inside the same frame as a
    deferred `end()` (the app-teardown crash rule from `numos-app-development`
    applies: never delete a screen mid-animation; apply theme on the new screen
    after a clean transition).
- Add a "Dark" palette as the second shipped theme; NumWorks Light is theme 0.

## 9. Build wiring (required for the emulator)

Every new `.cpp` (`ThemeManager.cpp`) **must** be added by name to
`build_src_filter` under `[env:emulator_pc]` in `platformio.ini`, or the PC
emulator silently won't link it. Firmware envs compile `+<*>` and need nothing.

## 10. Verification

1. `pio run -e emulator_pc` — downloads compile with the new manager + every
   existing app unchanged.
2. Run the existing golden machine: `emulator_pc/program --headless
   --deterministic --script ... --fs-root ...` — current tables render
   **bit-identical** until a theme is switched (proves non-migration didn't
   perturb anything).
3. Existing GB smoke test still passes (`tests/emulator/scripts/gb_smoke.numos`)
   — GB frontend is Tier A opt-in and unchanged by default.
4. Hand-run: boot → menu, open Settings, switch theme → status bar + menu +
   migrated apps re-tint; back out to each app to confirm no crash (LVGL teardown
   rule respected).

## 11. Non-goals / defer

- Fonts, radii, shadows, spacing — remain per-app/lv_conf.
- Auto-adapting every engine renderer to the theme (documented opt-out).
- Hot theme swap while an app is mid-input with no reset — theme takes effect on
  the next clean screen build.