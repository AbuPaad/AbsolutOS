# NumOS App Trim — Change Record

Date: 2026-11-01
Repo: /home/fih/musings/Absolut-CAS/firmware/AbsolutOS

## Goal

Free flash/RAM headroom for bitchat BLE support (NimBLE-Arduino ~562 KB flash, ~41 KB RAM).
Target: remove non-CAS/computational apps that are illegible at the 320x156 canvas
and heavy on flash.

## Apps cut (9)

| ID | App | Reason | Size (lines) |
|----|-----|--------|-------------|
| 8  | PythonApp (MicroPython IDE) | scripting, not CAS/computational | 936 |
| 11 | PeriodicTableApp (periodic table + molar mass + balancer) | chemistry reference, heavy | 1,121 |
| 12 | BridgeDesignerApp (bridge structural sim, Verlet physics) | sim, illegible at screen res | 904 |
| 13 | CircuitCoreApp (SPICE-like circuit sim, MNA, Lua MCU) | sim, heaviest app | 2,068 |
| 14 | Fluid2DApp (Jos Stam stable fluids) | sim, heavy | 1,524 |
| 15 | ParticleLabApp (falling-sand/Powder Toy) | sim | 777 |
| 16 | NeuralLabApp (MLP playground) | ML, illegible at screen res | 1,198 |
| 17 | OpticsLabApp (2D ray tracing) | sim | 602 |
| 18 | NeoLanguageApp (NeoLanguage compiler IDE) | language tool, not CAS | 1,051 |

## Apps explicitly kept

- GameBoy (21) — emulator, user requested keep
- FractalApp (19) — viz, left for next round
- AiApp (23) — AI wrapper, network-dependent, left for next round
- Settings (10), Calculation (0), Grapher (1), Equations (2), Calculus (3),
  Statistics (4), Probability (5), Regression (6), Sequences (7), Matrices (9)
  — all CAS/computational, keep
- MathVisual (20) — debug-only, guarded by NUMOS_MATH_VISUAL_APP_ENABLED

## How the trim was done (reversible)

Each app was wired into 6 places in SystemApp. All references removed:

1. SystemApp.h: `#include "apps/XApp.h"` lines removed
2. SystemApp.h: `enum class Mode` entries (APP_X) removed
3. SystemApp.h: member pointer declarations (`XApp* _xApp`) removed
4. SystemApp.cpp constructor: initializer list entries (`_xApp(nullptr)`) removed
5. SystemApp.cpp begin(): `new XApp()` instantiation lines removed
6. SystemApp.cpp initApps(): `_apps.emplace_back(id, ...)` lines removed
7. SystemApp.cpp launchApp(): `else if (id == N)` branches removed
8. SystemApp.cpp switchApp(): `case N: _mode = Mode::APP_X` lines removed
9. SystemApp.cpp teardownModeNow(): `case Mode::APP_X` lines removed
10. SystemApp.cpp update(): `else if (_mode == Mode::APP_X)` branches removed
11. SystemApp.cpp handleKey(): `case Mode::APP_X` blocks removed
12. SystemApp.cpp handleKey(): InputContext condition simplified

Source files for cut apps remain on disk (src/apps/XApp.cpp/.h) but are no longer
compiled or referenced. To restore, re-add the wiring lines above and rebuild.

## Files modified

- src/SystemApp.h
- src/SystemApp.cpp

## Native build (NativeHal.cpp)

Left untouched — reverted to original. The cut app source files remain on disk so the
native emulator build still compiles. NativeHal references to BridgeDesignerApp,
Fluid2DApp, and NeoLanguageApp are retained for emulator smoke tests.

## Source files on disk

All cut app source files remain in src/apps/ (not deleted). They are excluded from
the firmware build via build_src_filter in platformio.ini. The native emulator build
still compiles them.
EOF
echo "written"
## Reversibility

No source files deleted. All cut app code remains in src/apps/. Re-adding the
wiring lines (documented above with app names and IDs) restores byte-identical
behavior. The wiring lines are standard and can be re-inserted from git history
or this document. platformio.ini build_src_filter exclusions must also be removed
to re-enable compilation of the cut app .cpp files.
