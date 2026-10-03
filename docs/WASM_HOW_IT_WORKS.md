# How the WASM target works (`emulator_web`)

Operator-facing reference for the browser build. The design rationale lives in
`docs/specs/NUMOS_WEBASSEMBLY_PORT_ARCHITECTURE_SPEC.md` and its siblings; this file is
"what actually happens when you type `wasm/build.sh`".

## 1. The pipeline

```
wasm/build.sh Release
  ├─ asserts `emcc --version` == wasm/emscripten.version   (exact match, aborts otherwise)
  ├─ python3 scripts/emulator_sources.py  →  out/wasm/build-release/generated/emulator_sources.cmake
  ├─ emcmake cmake -S wasm -B out/wasm/build-release
  ├─ cmake --build ... --target emulator_web
  └─ node wasm/package.mjs                →  out/wasm/release/
```

Output is an **ES6 module** (`-sEXPORT_ES6=1`) exporting `createNumosModule`, plus the host
shell. `out/wasm/<cfg>/` is what you serve; the shell is `<numos-emulator>`.

The web target is a *separate target*, not a configuration of `emulator_pc`. Both compile with
`NATIVE_SIM`, which is what makes `src/hal/NativeHal.cpp` supply the whole runtime (there is no
Arduino, no LVGL driver task, no real scheduler).

## 2. The source list is derived, never written

`wasm/CMakeLists.txt` lists **no** project sources. It runs `scripts/emulator_sources.py`, which
parses `[env:emulator_pc]`'s `build_src_filter` out of `platformio.ini` and globs `src/<pattern>`,
emitting `generated/emulator_sources.cmake`. Consequences:

- **Adding a file to the emulator's filter also adds it to the web build.** There is one list.
  Adding an app to the browser is usually *not* a port.
- Conversely a file missing from the emulator's filter is missing here too — which is why
  `src/SystemApp.cpp` and `src/main.cpp` are compiled for the device but **not** for the emulator
  or the browser. The app dispatch, `handleKey`, and the theme guard the emulator uses all live in
  `NativeHal.cpp` instead. When something behaves differently in the browser than on the device,
  check that the code you are editing is in this closure at all.
- Membership count: `python3 scripts/emulator_sources.py --repo . --cmake /tmp/web.cmake`.

`wasm/CMakeLists.txt` additionally globs `src/apps/Neo*.cpp` (web-only translation units).

## 3. `lib/` is not covered by that glob

Anything under `lib/` that a web-built file needs has to be wired by hand, or the link fails with
undefined symbols even though every `.cpp` compiled.

| Dependency | How the web build gets it |
| :--- | :--- |
| `lib/giac` | `numos_giac` static target |
| `lib/libtommath` | `numos_tommath` static target |
| `lib/md4c` | `numos_md4c` static target (`src/mdrender` links it) |
| `lib/WalnutCGB` | header-only — an **include path** on `emulator_web`, no library |

## 4. Storage: IDBFS, seeded by a preload

The link exports `FS`/`IDBFS` (`-sFILESYSTEM=1`, `-lidbfs.js`) so the littlefs image persists in
the browser. A fresh profile has no image at all, and the failure mode is silent — an app that
reads config either renders a blank or falls back to a default menu, never an error. So the link
preloads a seed image:

```
--preload-file=${NUMOS_ROOT}/tests/emulator/fs@/
```

That is the browser equivalent of the PC emulator's `--fs-root tests/emulator/fs`.
`wasm/numos-persistence.js` handles write-back after boot.

## 5. Input: everything is `KeyCode::` — there is no text command channel

This is the rule the host page must not break.

The C ABI is the only way in:

```c
int numos_send_logical_key(int keyCode, int actionCode);   // EMSCRIPTEN_KEEPALIVE
```

`keyCode` is validated against the `KeyCode` enum and `actionCode` against `KeyAction`
(`PRESS` / `RELEASE` / `REPEAT`) inside `NativeHal.cpp:4427`. Numbers below `KeyCode::ALPHA`
(`2`) or above `KeyCode::GREATER` are rejected outright — the runtime is not a string parser and
will not accept key names.

The chain end to end:

```
on-screen button            data-key-code="50"          (numos-keypad.js catalog)
  → #logicalDown(50)        press/release edge counting (one physical button may map twice)
  → sendLogicalKey(50, 1)   → module._numos_send_logical_key(50, KEY_PRESS)
  → NativeHal dispatch      → the same path a hardware matrix key takes
```

`wasm/numos-keypad.js` is the catalog: `NUMOS_LOGICAL_KEYS` maps `logicalId → code → label`.
It is hand-audited, **not** generated, and `tests/wasm/keycode-catalog.mjs` fails if any code
drifts from `src/input/KeyCodes.h` (currently 79/79). Run it whenever you touch either side:

```
node tests/wasm/keycode-catalog.mjs
```

**The emulator's `.numos` script language does not exist in the browser.** `--script`,
`keydown alpha`, `key ac`, `assert_menu_focus`, `assert_theme` are the *test* harness. In the
browser the equivalent is a sequence of `sendLogicalKey` calls — which is exactly how the page's
theme button is built (see §7).

## 6. Output: the firmware talks back over stdout

Firmware → page is a text channel, because the page cannot poke at LVGL. `NativeHal` emits one
line per state change on the runtime's stdout, which the shell hooks through the module's
`print` / `printErr` options:

| Channel | Shape | Fires |
| :--- | :--- | :--- |
| `@ctx` | `@ctx <id> <slug> <modifier>` | on context change **or** bare modifier change (SHIFT/ALPHA), `modifier` = `none` when absent |
| `@app` | `@app <id> <slug> <name>` | only on context change, with the long name already resolved |

The split is deliberate: `@ctx` is fine-grained state (`Ctx` enum + slug + modifier), `@app` is a
coarse "an app opened" event carrying the display name, so the page needs no slug→name table of
its own. Slugs are shared with the keypad context map (`scripts/gen_key_context.py` keeps the C++
and JS copies identical).

`-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString','callMain','FS','IDBFS']` plus
`_numos_is_ready`, `_numos_request_shutdown`, `_numos_send_logical_key`, `_numos_context_id`,
`_numos_diagnostic_state` are the whole host surface.

## 7. The host shell (`<numos-emulator>`)

`wasm/numos-emulator-element.js` is a custom element that owns the canvas, the on-screen keypad,
the power/restart controls and the runtime log. Actions are `data-action="..."` buttons; all
input goes through `sendLogicalKey` per §5.

- **Theme button** (`data-action="theme"`) — sends the ALPHA+AC hotkey as four explicit edges:
  `ALPHA press → AC press → AC release → ALPHA release` (KeyCode `2`/`10`). The edges matter: a
  complete ALPHA press+release releases the modifier before AC arrives and the combo never fires.
  Enabled only once the runtime is accepting input.
- **App tips** — `APP_TIPS` is a slug→tip map in the element; `@app` is parsed and the tip is
  shown under the context strip, cleared when the app closes. Driven entirely by the firmware
  event, so a tip can only appear for an app the runtime actually entered.

## 8. Verify

```bash
# 1. keycode catalog still agrees with KeyCodes.h
node tests/wasm/keycode-catalog.mjs

# 2. source closure
python3 scripts/emulator_sources.py --repo . --cmake /tmp/web.cmake

# 3. the build itself (needs the pinned emcc)
source ~/emsdk/emsdk_env.sh && wasm/build.sh Release

# 4. serve out/wasm/release/ and walk it in a browser
```

Emscripten is pinned and enforced: `wasm/emscripten.version` must equal `emcc --version` or
`build.sh` aborts. Installed here as `~/emsdk` 6.0.3 (`./emsdk install 6.0.3 && ./emsdk activate 6.0.3`).

## 9. Behaviour differences to remember

- **No network.** Anything talking to a remote service must use its offline/replay transport.
  That makes a public demo cost-free and key-free — and means **no credential ever goes into the
  web build**, same rule as firmware.
- The web build is configured with `NUMOS_NEO_APP_SMOKE=1` (`wasm/CMakeLists.txt:112`), so it
  boots into the Neo smoke app rather than the launcher. A public demo wants the launcher;
  dropping that define is a one-line change and a full rebuild.
- Headless parity with the PC emulator is by construction: same sources, same `NATIVE_SIM`
  runtime, so a browser bug is usually reproducible as a `.numos` script on `emulator_pc`.
