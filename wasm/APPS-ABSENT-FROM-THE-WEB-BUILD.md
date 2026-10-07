# Apps absent from the web build — check before the next WASM build

Standing note for whoever runs the next web/WASM build. Written 2026-10-04, verified against
source, not assumed. Rewritten once: the first version listed 3 apps and was wrong — the real
number is 11. The question is not "is the file present" but "is it in the per-file whitelist".

## The 11 apps the web build cannot run

| app | id | its source files |
|---|---|---|
| Python | 08 | `src/apps/PythonApp.cpp` (+ its interpreter) |
| Matrices | 09 | `src/apps/MatricesApp.cpp` |
| Chemistry | 11 | `src/apps/PeriodicTableApp.cpp`, `ChemCAS.cpp`, `ChemDatabase.h`, `ChemExtraData.h` |
| Bridge | 12 | `src/apps/BridgeDesignerApp.cpp` |
| Circuit | 13 | `src/apps/CircuitCoreApp.cpp`, `Components.cpp`, `ComponentFactory.cpp`, `MnaMatrix.cpp`, `LogicGates.cpp`, `PowerSystems.cpp` |
| Fluid 2D | 14 | `src/apps/FluidApp.cpp` |
| ParticleLab | 15 | `src/apps/ParticleLab*.cpp` |
| Neural Lab | 16 | `src/apps/NeuralLab*.cpp` |
| OpticsLab | 17 | `src/apps/OpticsLabApp.cpp` |
| NeoLang | 18 | `src/apps/Neo*.cpp` |
| Fractals | 19 | `src/apps/FractalApp.cpp` |

Everything else in `src/apps/` **is** reachable: Calculation (0), Grapher (1), Equations (2),
Calculus (3), Statistics (4), Probability (5), Regression (6), Sequences (7), Settings (10),
Game Boy (21), AI (23) — eleven apps. Plus two emulator-only hosts that are not launcher cards:
id 20 (the MathRenderer showcase) and id 22 (the Notes / mdrender demo host).

## Why — it is one decision, not eleven bugs

`wasm/CMakeLists.txt` does not keep its own source list. It derives its closure from
`scripts/emulator_sources.py`, which reads the `build_src_filter` of `[env:emulator_pc]` in
`platformio.ini`. That filter is a **per-file whitelist with no globs** (`platformio.ini:412-559`),
and it carries its own warning at line 544: every new TU must be listed or it "compiles on firmware
and silently vanishes on native". None of the files above are listed, so none are in the web build
either. Whatever keeps them off the desktop emulator keeps them off the web.

Second half of the same fact: `src/hal/NativeHal.cpp` routes only ids
0,1,2,3,4,5,6,7,10,18,20,21,22,23. Everything else falls to the default and prints

    [APP] App %d no implementada en simulador

So even if the sources were added, nothing would launch.

## What this means here

- **They are already excluded. Do nothing and they stay excluded** — that is the correct state.
- **The trap is a glob.** `wasm/CMakeLists.txt:29-32` appends `src/apps/Neo*.cpp` by glob, because
  NeoLanguage's closure is deliberately web-only. A future glob of the same shape would sweep in
  `FractalApp.cpp`, `CircuitCoreApp.cpp`, `OpticsLabApp.cpp` and friends: they would link as dead
  code that can never be reached, and the launcher card would still answer "no implementada en
  simulador". Grep any new glob against the table above before adding it.
- **Never wire an app into the closure just to get a screenshot.** A frame captured from a build
  that cannot launch the app is not that app's frame. This has already happened once: capture files
  landed in `out/` byte-identical to unrelated leftovers — one was literally a file named
  `particlelab-diag-blocked-launcher.ppm`, another matched an old `ai_ask.ppm`. Check what an image
  shows, not what it is named.
- **The website lists these 11 as "Coming soon"** on `/apps/`, with no screenshots and the reason
  stated. Keep it that way; do not add images for them.

## NeoLang is a special case — it runs in the browser, not in the emulator

id 18 is excluded from the normal emulator target on purpose (`platformio.ini:561-564`): its
`file()` builtin can bypass the emulated LittleFS root. It exists only under the opt-in
`[env:emulator_pc_neo_smoke]` target, which adds `-DNUMOS_NEO_APP_SMOKE=1` and `+<apps/Neo*.cpp>`.
The **web** build adds the same `Neo*.cpp` glob unconditionally, so NeoLanguage does run in a
browser while being unavailable in `emulator_pc`. Any capture of it must come from the web build or
the smoke target — never from the standard emulator binary.

## If they are ever wanted in the web build

Ordered, or the result is dead code plus a broken card:

1. add the app's files to `[env:emulator_pc]` `build_src_filter` in `platformio.ini` — the web build
   inherits them from there;
2. a `case <id>:` in `NativeHal::launchApp()`;
3. the canonical name in `canonicalAppName()`;
4. the `open_app` slug in `scriptAppNameToId()`;
5. rebuild, capture, **verify the frame actually shows the app**, then update this note.

## Two smaller gaps that would still bite after those five steps

- **Bridge cannot start its own simulation from a script.** The sim is entered only on
  `KeyCode::SOLVE` (`BridgeDesignerApp.cpp:817`), and `scriptNameToKeyCode()` in `NativeHal.cpp` has
  no token for `SOLVE`. Adds a step 6: a key alias.
- **Chemistry's element alphabet is out of reach.** The script grammar has no `ALPHA_A..ALPHA_F`
  keycodes, which the element entry path needs.

Neither is a reason to skip the exclusion now; both are reasons not to half do the fix later.
