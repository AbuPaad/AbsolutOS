# NumOS — Games Frontend + PICO-8 Plan

> **Status:** research complete, plan for approval. Nothing implemented.
> **Date:** 2026-10-25. **Owner:** Paadshi.
> **Scope:** one games frontend serving the existing Game Boy app and a new PICO-8 runtime,
> plus the content (cart/ROM) pipeline into the device.
> **Depends on:** v2 hardware for the SD path (v2 restores the SD slot, which is DOA on v1).

---

## 1. The decision in one paragraph

Build a **games subsystem**, not two game apps. One frontend (library carousel + in-game bezel +
pause menu) in `src/ui/games/`, and one thin *runtime host* interface that the Game Boy core
(`GameBoyCore`) and a PICO-8 VM (fake-08 + z8lua) both implement. The Game Boy looks rubbish today
because it draws a 160x144 picture at 1x on a 320x156 canvas with nothing around it — the fix is
chrome and a library, not a new emulator. PICO-8 is feasible on ESP32-S3, but it is **framerate-marginal,
not 60fps** (§6.2), and its carts arrive as `.p8.png` files that fake-08 (MIT) can already decode.

Nothing here requires copying a GPL-2.0-only or unlicensed codebase (§8).

---

## 2. Current state (evidence)

| Thing | Where | State |
|---|---|---|
| GB app | `src/apps/GameBoyApp.cpp` (610 lines), `src/emulation/GameBoyCore.cpp` (331) | working; own ROM picker (`kRomDir`, `rescanRoms`, `buildPicker`); publishes an RGB565 `lv_image_dsc_t`, `kImageScale` (1x) — `GameBoyApp.cpp:19-23,331-341` |
| Chrome | `ui::appSurface(ui::appid::kGameBoy)` | themed, but black surround + a text list; no cover art, no bezel, no pause overlay |
| Storage | `src/hal/FileSystem.{h,cpp}` (231) | LittleFS internal only; no SD path yet |
| Launcher | `src/ui/MainMenu.cpp:115` `APPS[]`, 20 cards | flat, page-capped, no per-app art |
| Geometry | `Config.h` 320x156, `offsetY +54` | settled — this plan never changes it |

The dead space is the problem: at 1x the GB picture is 160x144, i.e. **80 px of empty column each
side and 12 dead rows**. PICO-8 at 1x is 128x128: **96 px each side**. That space is where the
frontend lives.

### 2.1 Platform constraints (owner-stated 2026-10-25 — these bind every wave)

- **No audio.** The board has no audio path, and recon found none in `src/` (the only `audio`
  strings in the tree are circuit-simulator hits). Consequences, recorded once so no wave
  re-litigates it: Game Boy runs **silent**; PICO-8 runs **silent**, so `sfx()` and `music()` are
  no-ops and the synth is compiled out (it also buys flash). Nothing in this plan waits on sound,
  and no marketing claim mentions it. Visually, this raises the bar on §4 — the chrome is carrying
  the whole experience.
- **Wi-Fi is ours.** NumOS already owns a mature stack and it is the only one we may use:
  `src/net/Wifi.{h,cpp}` (723), `Portal` (1515), `HttpStream` (431), `OtaUpdater` (501),
  `DeviceTransport` (360), `TlsSession.h` — **4413 lines total**. Upstream Wi-Fi code is
  **non-transferable**, not merely unnecessary: pico-e32's `components/wifi`, retro-go's
  `rg_network.c` and `p3a`'s networking are all irrelevant to us. Any networked feature in this
  plan is a call into `net::`, never a vendored driver.
  - Lifecycle rule that must be honoured: the radio is a **system service owned by `main.cpp`**
    (`Wifi.h:14-26`) — `begin()` once at the end of `setup()`, `tick()` from `loop()`, apps must
    never own it. A game runtime must not touch the radio.
- **Cart/ROM transport already exists.** `net::Portal` raises a WPA2 AP at `192.168.4.1`, browses
  the whole LittleFS root and **accepts multipart uploads** per directory (`Portal.cpp:123-126,
  476-477,504-507`; `handleUploadWrite`). With `/roms` already the GB convention
  (`GameBoyApp.cpp:170-230`), the v1 path is: raise the portal from Settings, upload the file,
  reopen the app. **No new transport code.** This de-risks §7 considerably.
- **Doc location (corrected 2026-10-25):** the networking authority is **not** in the repo's `docs/`
  tree — it lives in `~/musings/sserialprintthing/`:
  - `device-networking.md` (392 lines, 2026-09-28) — the stub spec: seam (`AiTransport`), the nine
    stubs, memory budget, bringup ladder, traps. Its opening line ("nothing here exists yet") is
    **stale**.
  - `networkingimplement.md` (260 lines, 2026-09-29 + a 2026-10-01 correction) — what actually
    landed: `src/net/{Wifi,Clock,HttpStream,DeviceTransport}.{h,cpp}`, 1054 lines, verified on
    `emulator_pc` and compiled for the S3 target.
  - `Portal.h:14-16` and `Wifi.h:17` cite `device-networking.md` §1.1 as the radio-lifetime
    authority, so the citation is correct but **the file is not committed**. Wave G0 should move
    both docs into `docs/` rather than leave the headers pointing outside the repo.
- **Internal SRAM is the scarce resource, not PSRAM.** From `networkingimplement.md`:
  Wi-Fi buffers + mbedTLS live in **internal SRAM (~328 KB total)** and must *never* be pushed to
  PSRAM; mbedTLS defaults to ~16 KB in + 16 KB out per session. Two hard rules follow for games:
  - `net::Wifi::begin()` must stay the **last** step of `setup()`, after `initLvgl()`, the DMA
    draw-buffer allocation and `g_splash.destroy()` — `esp_wifi_init`'s internal-SRAM grab can
    otherwise starve the draw buffer (`main.cpp`).
  - A PICO-8 runtime's internal-RAM footprint (VM + framebuffer + Lua stack) is spent out of the
    *same* ~328 KB that Wi-Fi needs. **P-GATE-1 must report internal-SRAM headroom, not just flash.**
- **Threading:** Wi-Fi event callbacks and any blocking HTTP call run on their own FreeRTOS tasks,
  while `agents.md` §5.4 forbids touching LVGL off the LVGL task — every byte crosses a queue. A
  game runtime must never touch `net::` directly and never block the LVGL task on a fetch.
- **Serial input already exists, and its semantics matter for games.**
  `src/input/SerialBridge.{h,cpp}` is the **PC → calculator** bridge: line-based, one key per line
  terminated by Enter, plus named words for codes without a single char. Its header states the rule
  this subsystem must follow: *"Momentary keys emit a PRESS **and** a RELEASE: a lone PRESS latches
  inside every consumer that reads edges — the Game Boy core holds the button down until it sees
  `KeyAction::RELEASE`."* `RuntimeHost::input()` inherits exactly that contract.
  - The *other* direction (calculator → PC, `Serial.println("ROW[r] COL[c] pressed")` → xdotool)
    is a host-side bench tool (`serialbridge/serialbridge.sh`, `keytap.py`, `KEYMAP.md`), not
    firmware. `KEYMAP.md` warns the two are opposite in direction and protocol — always check which
    one is meant.
  - Practical value: SerialBridge is how the games frontend gets driven on hardware without a
    keyboard, so GAMES-03's goldens should have a serial-driven variant.

---

## 3. What the ESP32 game-system scene actually gives us

Researched 2026-10-25 against repos, licences and READMEs.

| Project | Licence | Verdict |
|---|---|---|
| **`jtothebell/fake-08`** — PICO-8 player, 884★, active 2026-06 | **MIT** (stated in `LICENSE.MD`; chain: z8lua MIT, Zepto8 WTFPL-2, tac08 MIT, PicoLove zlib, LodePNG zlib, SDL2 zlib) | **Adopt the runtime.** Already has `source/cart.cpp` + `libs/lodepng` and a legacy `.p8.png` test cart (`test/carts/test_legacypng_cart.p8.png`). No ESP32 target — that work is ours. |
| **`ducalex/retro-go`** — ODROID-GO launcher + emulators, 1404★ | **GPL-2.0** (CHANGELOG: "License is now GPL2 for all components"); **not** "or later" | **Copy nothing; steal the architecture.** 2.0-only is incompatible with NumOS GPL-3.0. Structure to mirror: `rg_gui`/`rg_display`/`rg_storage`/`rg_settings`/`rg_surface` + per-device `targets/` (16 targets incl. `esp32-s3-devkit`, `t-deck-plus`). `rg_gui.c` is 2324 lines of proven 320-wide launcher UI. |
| **`pebri86/esplay-retro-emulation`** | **MIT**, 528★ | Usable if we want a second opinion on launcher code; less complete than retro-go. |
| **`retro-esp32/RetroESP32`** | **CC-BY-SA-4.0**, 740★ | Not a software licence — do not copy code. Layout ideas only, with attribution. |
| **`teapotlaboratories/pico-e32`** — PICO-8 handheld on ESP32-S3, Aug 2026 | **TBD** (README says "License: TBD") | **Closest project to ours.** Its `carousel_launcher.cpp` is the frontend we want (cover-art carousel, folders, breadcrumbs, PICO-8 3x5 font, per-board layout). Its measurements are the real yield (§6.2). Ask the author for terms. |
| **`fabkury/p3a`** | **Apache-2.0**, 100★ | Wrong architecture: PICO-8 here is a **WebAssembly emulator streaming over Wi-Fi** to an ESP32-P4 art frame — not on-device. |
| **`emir173/esp32-console`** | **MIT**, 36★ | S3 handheld, own OS, **carousel main menu, no LVGL** — good read for a framework-free menu. |
| **`ESPboy-edu/ESPboy_GameBoy`** | **MIT**, 60★ | ESP32 GB emulator; possible reference for GB perf, not for UI. |

**Licence rule for this work:** ideas, layout arithmetic and UX patterns are not copyrightable —
copy those freely from anything. Source code goes only from MIT/zlib/WTFPL/Apache trees
(fake-08, z8lua, lodepng, esplay, ESPboy). GPL-2.0-only (retro-go) and CC-BY-SA (RetroESP32) and
unlicensed (pico-e32) code does **not** go into a GPL-3.0 firmware, and "we'll reimplement it later"
is not a licence.

---

## 4. Frontend design at 320x156 (the actual answer to "looks rubbish")

### 4.0 Toolkit: LVGL native — and the either/or is false (verified 2026-10-25)

The question "LVGL or whatever esplay/fake-08 use" dissolves once you look:

- **esplay's launcher is already LVGL.** `launcher_main.c` holds `lv_group_t`, `lv_obj_t` buttons,
  `lv_indev_t`, `lv_display_t` and a `lvgl_flush_cb` that does `lv_draw_sw_rgb565_swap`; its CMake
  `REQUIRES … lvgl`; it pins `lvgl/lvgl: ^9.4.0`. It is 1,487 lines total
  (`launcher_main.c` 776, `games.c` 163, `apps.c` 163, `settings.c` 163, `fnmatch.c` 222). So
  "use what's in esplay" and "LVGL native" are the **same answer**, not alternatives.
- **fake-08 has no library/browser UI to borrow.** Its only menu is the in-VM PICO-8 settings screen
  (`FakoSettings.h`, `MenuStyleOption`), drawn inside the 128x128 frame by the host. There is no
  system list, no cart browser, no cover art.
- **NumOS is LVGL 9 too** (`lvgl/lvgl@^9.2.0`, `LV_COLOR_DEPTH 16`), and the GB app already publishes
  frames the right way: a raw RGB565 `lv_image_dsc_t` inside a `_player` container whose background is
  the theme surface (`GameBoyApp.cpp:327-345`). No `lv_canvas`, no per-pixel LVGL drawing.

**Decision:** every menu surface (library, system picker, pause overlay, bezel) is LVGL, adapted from
esplay's screens. The game frame stays LVGL too, using the existing image-dsc publish pattern — so
there is exactly **one** display path, and P-GATE-4 is satisfied without a second graphics stack.

Why not an immediate-mode GUI: a second toolkit would fork four NumOS invariants at once — theme
tokens/widget chrome (`ThemeManager`), the keypad indev (`LvglKeypad` + `KeyContext`), the deferred
teardown rule, and emulator parity (`NativeHal` duplicates the LVGL path). That is the "two frontends"
cost this plan exists to avoid.

Import caveats when taking esplay's launcher:
1. It calls `lv_palette_main/lighten/darken` directly — a literal-colour violation. Every one becomes a
   `Theme` token on import (`agents.md` §5.6).
2. Version: esplay pins LVGL **^9.4.0**, NumOS **^9.2.0**. Either bump NumOS or verify each 9.4 call;
   do not run two LVGL versions.
3. Take the screen builders, not the firmware: drop its `main()`/boot ownership, its `file_server`
   (we have `net::Portal`) and its `appfs` OTA app slot unless we adopt that model.
4. Its input is `lv_keypad_read` over GPIO buttons; replace with `LvglKeypad`/TCA9555, never merge.


Three surfaces, all in `src/ui/games/`, all theme-token driven (no literals — `agents.md` §5.6).

### 4.1 Library (cart/ROM browser)
- **2-up cover-art carousel**, selected tile centred at **121x155** (cover art is 160x205 portrait;
  0.7609x nearest-neighbour = 121x156, one row trimmed). Two tiles = 242 px, leaving 78 px of gutter
  for the index counter and console badge. 3-up does not fit (3x108 = 324 > 320).
- Side peeks dimmed (retro-go and pico-e32 both dim non-selected tiles — pattern, not code).
- Text column: title, author, licence badge (PICO-8), or filename + save state age (GB).
- Empty state is a designed screen, not a blank list.
- **Cover art sources:** PICO-8 = the cart's own visible pixels (`.p8.png` *is* the cover art);
  GB = **auto-screenshot on first launch**, cached next to the ROM. Both are free.

### 4.2 In-game bezel (why GB stops looking rubbish)
- The runtime blits into a framed window: 1x integer, centred, with the 80 (GB) / 96 (PICO-8) side
  gutters used for: console badge, save-state chip, FPS, and the key legend.
- Frame/border comes from `Theme` tokens; Casio theme gets a light LCD surround, numos stays black.
- Optional integer 1.5x for GB stays parked (exactly as today) — bezel first, scale later.

### 4.3 Pause overlay (in-game menu)
Save state / load state / screenshot / palette / exit — the retro-go `rg_gui_dialog` *pattern*.
Built as a shared LVGL overlay so both runtimes get it once. Exit routes through the existing
deferred-teardown rule (`SystemApp::returnToMenu()`), never `end()` from a key handler.

---

## 5. Architecture

```
src/ui/games/            GameLibrary (carousel), GameBezel, PauseMenu, CoverArt
src/games/RuntimeHost.h  interface: attach(surface), frame(blit), input(map), save/load, fps  (no audio)
src/games/GbRuntime.*    wraps GameBoyCore  (existing core, thin adapter)
src/games/Pico8Runtime.* wraps fake-08 VM   (new)
src/hal/FileSystem.*     + SD mount (v2), content scanning, sidecar metadata
```

Rules:
- The library and bezel **never** include a core header; they talk to `RuntimeHost`.
- Runtimes never draw chrome; they hand over a framebuffer.
- One content-scan path for both (extension + sidecar), so Wave G3 adds a runtime, not a screen.

---

## 6. PICO-8 runtime

### 6.1 Stack
- **fake-08** (MIT) VM + `cart.cpp` cart loader + lodepng — already handles `.p8.png`, including the
  legacy `:c:` encoding (test cart in-tree). **We do not port the decompressors**; the Python decoder
  in skill `pico8-cart-acquisition` becomes the independent oracle for a golden test.
- **z8lua** (MIT via Lua file headers) compiled as C++ with `-fjump-tables` (pico-e32's finding).
- `Pico8Runtime` implements `RuntimeHost`; adds one ESP32 host layer (display/input/timing) — no audio,
  so `sfx`/`music` are no-ops (§2.1).

### 6.2 Measured risk — set expectations now
- z8lua on ESP32-S3 measured **0.29 M VM-inst/s** (pico-e32 Gate #2, 2026-07-14); `-fjump-tables`
  gives **~2.8–3.1x**, so ~0.8–0.9 M inst/s.
- PICO-8's own VM budget is **~4.19 M inst/s** (139,810 cycles/frame at 60 fps).
- Therefore: **light carts at 30 fps is the target; Celeste-class carts will be below that.** Do not
  promise 60 fps anywhere in marketing. The honest claim is "PICO-8 carts run, heavy ones at
  reduced framerate".
- **Lua heap must live in PSRAM.** pico-e32 OOM'd on internal SRAM (369 KB free) in the alloc-heavy
  benchmark. Typical cart footprint ~130 KB.
- Flash: PicoPico's comparable stack measured **95.8 KiB** (83.1 z8lua + 12.7 engine) at `-Os`;
  fake-08 is more complete so budget 110–150 KiB against ~977 KiB headroom in app0. **Measure, don't assume.**

### 6.3 Gates (fail-fast, before UI work)
- **P-GATE-1** fake-08+z8lua builds for `esp32s3_n16r8` and reports flash/RAM; ≤150 KiB code.
- **P-GATE-2** three light carts sustain ≥30 fps on the device path (emulator timing is not evidence).
- **P-GATE-3** Celeste launches and is playable at whatever rate it achieves — recorded, not gated.
- **P-GATE-4** ROM/cart blit does not route every frame through LVGL invalidation (§7, SPI bytes).

---

## 7. Cart / ROM acquisition

**Posture (settled):** carts and ROMs are **user-supplied**. We never bundle, mirror or sell content.
Carts on the BBS are author-owned (CC-BY-NC-SA or stricter) — NC terms and a commercial product do
not mix, so a per-request fetch is defensible where an archive is not.

1. **Primary — SD card (v2).** User drops files on the card; the library scans on mount.
   Media on SD, settings/saves in LittleFS. This is the whole v1.1 story and needs no network.
2. **Secondary — fetch by cart id** via the existing Wi-Fi setup portal, one extra field:
   `get_cart.php?cat=7&lid=<id>` → bytes → SD. Must check **body length and content type**: a wrong id
   (tid instead of lid) returns **HTTP 200 with 0 bytes**. Show the cart's licence before launching.
3. **Never** ship a resolver inside sold firmware — the same reasoning that kept hosted AI out of v1.
   The community resolver worker stays an optional, self-deployed artifact (skill
   `pico8-cart-acquisition`, `references/resolver-worker.md`).
4. Offline test content: fake-08's MIT test carts (`.p8` + two `.p8.png`) are legal fixtures.

### 7.1 An on-device PICO-8 browser is cheap — verified 2026-10-25

The BBS cart index is JS-rendered, but it embeds a complete `pdat` array; one `net::HttpStream` GET
returns 30–40 carts with **both ids**, title, author, thumbnail path, tags and timestamps. No scrape of
thread pages, no tid→lid guessing. Full field map: skill `pico8-cart-acquisition`.

    ?cat=7&sub=2&mode=carts                   newest    (30/page)
    ?cat=7&sub=2&mode=carts&orderby=featured  curated   (30/page, zero overlap with newest)
    ?cat=7&sub=2&mode=carts&orderby=lucky     random    (40/page)
    ?cat=7&sub=2&mode=carts&cc4=1             CC4-licensed only
    ?cat=7&sub=2&mode=carts&page=N            paging

Verified: `&popular=1` is a **no-op** (same set as newest) — do not build a ranking on it.
`orderby=featured` is the real "trending" surface.

Proposed browse UI (one screen, four verbs):
| Key | Row |
|---|---|
| up/down | Newest · Featured · Random · Search by id · My library (on SD/LittleFS) |
| left/right | page through the current feed |
| OK | fetch → decode → launch; the licence badge is shown before the first launch |

`cc4=1` is the browse mode to default to for anything we ship commercially, since it is the only
ordering that is licence-filtered rather than merely licence-labelled.

---

## 8. Vendoring manifest (verified 2026-10-25)

Everything below is MIT or zlib. **Pins are commit SHAs, not branches** — vendored code must be
reproducible, and `fake-08`'s submodule pin is load-bearing (its API expects that exact z8lua fork).

| # | Repo | Licence | Pin | Action |
|---|---|---|---|---|
| 1 | `github.com/jtothebell/fake-08` | **MIT** (`LICENSE.MD`) | `814991a2571a` (master) | **Fork/vendor.** PICO-8 VM, `source/cart.cpp` (`.p8.png` loader), `libs/lodepng`. Drop `platform/{3ds,vita,switch,gcw0,ps4}`, SDL glue, PC build. Keep `test/carts/*` as fixtures. |
| 2 | `github.com/jtothebell/z8lua` | **MIT** (Lua file headers; GitHub reports none) | **`e6928578d46b`** — the commit fake-08 pins | **Vendor as component.** This fork, not `samhocevar/z8lua`: fake-08's API targets it. Build as C++ with `-fjump-tables`. |
| 3 | `libs/lodepng` (inside fake-08) | **zlib** (`lvandeve/lodepng`) | ships with #1 | **Take as-is.** No separate fork; needed for `.p8.png` + cover art. |
| 4 | `github.com/pebri86/esplay-retro-emulation` | **MIT** | `aa1e92fbf2bb` (master) | **Fork the launcher only:** `launcher/` + `launcher/components/appfs`. It is an ESP32 multi-system launcher, i.e. the shell this plan describes. **Do not take its emulator submodules** (gnuboy/nofrendo/SMS++ are GPL — that would re-open the licence problem). |
| 5 | `github.com/Mr-PauI/Walnut-CGB` | **MIT** | `a42c18691751` | **Already vendored** at `lib/WalnutCGB`. No action; GB needs nothing new. |
| 6 | `github.com/ducalex/retro-go` | **GPL-2.0 only** | — | **Reference only. Do not fork, do not copy.** Structure and UX decisions are free; source is not. |
| 7 | `github.com/teapotlaboratories/pico-e32` | **TBD** | — | **Reference only.** Best carousel + gate numbers; ask the author for terms before any code. |
| 8 | `github.com/DavidVentura/PicoPico` | **none** | — | **Reject.** Dead since 2023, unlicensed, carts baked at build time. |

```sh
git clone https://github.com/jtothebell/fake-08.git     && git -C fake-08     checkout 814991a2571a
git clone https://github.com/jtothebell/z8lua.git       && git -C z8lua       checkout e6928578d46b
git clone https://github.com/pebri86/esplay-retro-emulation.git && git -C esplay-retro-emulation checkout aa1e92fbf2bb
```

Attribution obligations: MIT/zlib require the copyright notice + permission text in the distribution.
Add each to `THIRD_PARTY_NOTICES.md` (the repo already keeps one) in the same commit that vendors it,
not afterwards — the same rule the font provenance work already follows.

## 9. Wave plan

### Wave G0 — frontend foundation (start now, no v2 hardware, emulator-testable)
| ID | Ticket |
|---|---|
| GAMES-00 | `src/games/RuntimeHost.h` + `GbRuntime` adapter over `GameBoyCore`; GB app becomes the first consumer. No behaviour change. |
| GAMES-01 | `GameBezel` in `src/ui/games/`: framed 1x window, gutters, console badge, FPS/save chips, theme-token only. **This is the "GB looks rubbish" fix.** |
| GAMES-02 | `PauseMenu` overlay (save/load/screenshot/palette/exit) wired to GAMES-00; respects deferred teardown. |
| GAMES-03 | Golden: `emulator_pc` PPM capture of GB bezel before/after; numos launcher stays `AE=0`. |
| GAMES-04 | Commit the two networking docs from `~/musings/sserialprintthing/` into `docs/` and fix the stale "nothing here exists yet" line, so `Wifi.h`/`Portal.h` stop citing a file outside the repo. Doc-only. |
| GAMES-05 | Serial-driven frontend harness: drive the library + bezel over `src/input/SerialBridge` (line protocol) so GAMES-03 has a hardware variant that needs no keyboard. |

### Wave G1 — library
| ID | Ticket |
|---|---|
| LIB-01 | `GameLibrary` carousel: 2-up, 121x155 tiles, dimmed peeks, empty state, keymap via `KeyContext`. |
| LIB-02 | `CoverArt`: PICO-8 = `.p8.png` visible pixels (lodepng, already vendored by fake-08); GB = first-launch screenshot cached beside the ROM. |
| LIB-03 | Replace the GB app's text picker with `GameLibrary` (keep the ROM scan). |

### Wave G2 — content store (needs v2 hardware)
| ID | Ticket |
|---|---|
| STORE-10 | SD mount in `hal/FileSystem` + `content/` layout (`/sd/games/gb/`, `/sd/games/p8/`). |
| STORE-11 | Scan + metadata sidecars (`<name>.json`: title, author, licence, last played); 500 files < 2 s. |
| STORE-12 | Migration: existing LittleFS ROM dir keeps working; SD is additive. |

### Wave G3 — PICO-8 runtime
| ID | Ticket |
|---|---|
| PICO-01 | Vendored `fake-08` + `z8lua` components; `-fjump-tables`; build for `esp32s3_n16r8`; report flash/RAM (P-GATE-1). |
| PICO-02 | `Pico8Runtime` + ESP32 host (blit, input map, timing); light carts at 30 fps (P-GATE-2). |
| PICO-03 | **Silent by decision** (§2.1): compile the synth out, make `sfx()`/`music()` no-ops, and say so on the app's About screen so it is never filed as a bug. |
| PICO-04 | Save/load + `cartdata` persistence to LittleFS. |
| PICO-05 | Licence badge from cart metadata; refuse to launch a cart whose NC terms the user hasn't seen. |

### Wave G4 — optional acquisition
| ID | Ticket |
|---|---|
| FETCH-01 | *Outbound* fetch only — the transport already exists (§2.1): `net::HttpStream` GET of `get_cart.php?cat=7&lid=<id>` → `/pico8`, with the tid/lid trap handled (HTTP 200 + 0 bytes), the licence shown before launch, and the radio driven only from the portal path, never mid-game. |

---

## 10. Open questions

1. ~~**Audio:**~~ **Resolved 2026-10-25 — there is no audio.** Both runtimes are silent (§2.1). If a
   future board adds a codec, PICO-8's synth is the natural first consumer, not a rewrite.
2. **pico-e32 licence:** worth one message to the author — its carousel and gate numbers are the best
   match to our problem.
3. **retro-go design lift:** confirm we only mirror structure, never code (GPL-2.0-only).
4. **Menu capacity:** the flat `APPS[]` launcher at 156 px height is the real bottleneck — does Games
   become its own top-level entry (recommended) or two more cards among 20?

---

## 11. Non-goals

- No PLATFORM re-scaling of the canvas; geometry stays 320x156 / `offsetY +54`.
- No second LVGL display path for game frames unless P-GATE-4 fails.
- No bundling of carts or ROMs; no hosted resolver in firmware.
- No promise of 60 fps for PICO-8 on S3.
- No new emulators in this plan (NES/SMS/etc.) — the subsystem makes them cheap later, which is the point.
