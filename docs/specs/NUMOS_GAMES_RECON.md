# NumOS Games — technical recon for implementation

Read-only recon against the tree at `~/musings/Absolut-CAS/firmware/AbsolutOS` plus the upstream
fake-08 sources at pin `814991a2571a`. Every claim below cites a file and line. This document changes
five things in `NUMOS_GAMES_PICO8_PLAN_V3.md`. They are listed in §10.hey are listed in §10.

## 1. App registration and lifecycle

| Fact | Evidence |
|---|---|
| Apps are registered by integer id in one table | `src/ui/MainMenu.cpp:115` `MainMenu::AppEntry MainMenu::APPS[]` |
| Used ids: 0–21, 23 | 21 = Game Boy (`:137`), 23 = AI (`:138`), 20 = Math Visual (`:140`) |
| **Id 22 is free** | the table skips 22 |
| Launch is an if/else chain, one branch per id | `SystemApp::launchApp(int)` in `src/SystemApp.cpp` |
| Mode enum carries the app | `src/SystemApp.h:70` `enum class Mode`, `APP_GAMEBOY` at `:83` |
| Per-app teardown switch | `SystemApp.cpp:321-335` |
| App interface to copy | `GameBoyApp.h:40-57`: `load()`, `end()`, `update()`, `handleKey(const KeyEvent&)`, `consumeExitRequest()`, `isActive()` |

So a Games app costs one id, one `Mode`, one `launchApp` branch, one teardown case, and one entry in
`APPS[]`. Use **id 22**, not 24: the table is dense and 22 is the only hole below the AI entry.

`g_lvglActive` is global and switched per app (`launchApp` sets it true for LVGL-native apps). A Games
frontend is LVGL-native, so it follows the Calculation path, not the Game Boy one.

## 2. Display path — the constraint that decides the design

`src/main.cpp:225-247` documents and enforces all four limits:

1. **Single buffer.** `lvBuf2 = nullptr` (`:235`). Double buffering deadlocks LVGL 9 in blocking mode.
2. **Internal SRAM only.** Allocated `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA` (`:234`).
3. **PSRAM is unusable for the panel path.** "TFT_eSPI SPI-DMA on ESP32-S3 cannot source from PSRAM →
   StoreProhibited crash" (`:226`). OPI-PSRAM sits on a separate SPI bus.
4. **32 KiB = 51.2 lines per strip** (`:231`), about half the flush calls of a 16 KiB buffer.

Other facts:

| Fact | Evidence |
|---|---|
| Colour depth RGB565 | `src/lv_conf.h:51` |
| Software transforms are enabled, so image scaling works | `src/lv_conf.h:185` `LV_DRAW_COMPLEX 1` |
| The panel is offset at flush time by `SCREEN_OFFSET_Y` (54) | `DisplayDriver.cpp:196`, flush at `:548`, pushes at `:591/658/687/713` |
| Canvas can be moved live | `DisplayDriver.h:79` `applyCanvasPlacement(x, y, rotation)` |
| An image is published as an `lv_image_dsc_t` and invalidated once per frame | `GameBoyApp.cpp:355-375` (`publishFrame`) |
| LVGL scaling is already in production use | `GameBoyApp.h:100` `kImageScale = 256` (2×), `GameBoyApp.cpp:341` `lv_image_set_scale` |

### 2.1 Consequences for the 156×156 play area

`156/128 = 1.21875 = 39/32`. LVGL takes scale in 1/256 units, so **312 is exact**. The scale factor carries no
rounding error. The mapping is not integral, however. Within every 32 source columns, **7 columns are
2 px wide and 25 are 1 px wide**. Rows behave the same. Nearest-neighbour, so no blur, but a visible uneven
rhythm at 1.22×. 128 at 1× is pixel-perfect and smaller. 156 is larger and slightly uneven. The user56 is larger and slightly uneven. The user
asked for 156.

Panel bytes per frame, at 40 MHz SPI:

| Frame | Pixels | Bytes | SPI time |
|---|---|---|---|
| 128×128 | 16 384 | 32 768 | 6.55 ms |
| **156×156** | **24 336** | **48 672** | **9.73 ms** |
| 160×144 (GB) | 23 040 | 46 080 | 9.22 ms |

Going 128 → 156 costs **+48 % SPI time**. The pixel area suggests +49 %. The strip buffer holds 51 lines
in both cases, so the cost follows destination pixels, not source pixels.

### 2.2 Where the palette conversion must happen

fake-08 hands out **8-bit palette indices** plus a palette map, not RGB565
(`vm.h`: `GetPicoInteralFb()`, `GetScreenPaletteMap()`). The conversion is ours:

1. Build a 256-entry `uint16_t` RGB565 LUT once, from `Host::GetPaletteColors()` (144 colours,
   `hostVmShared.h` `COLOR_00`..`COLOR_143`).
2. Per frame, expand 8-bit → RGB565 through the LUT into an **internal** staging buffer.
3. Push each strip with `pushColors(..., true)`.

Two staging sizes satisfy the DMA rule. A full 156×156 frame is 48 672 B. That works, but it takes a
third of the internal headroom. A 16-row strip is `156 × 16 × 2 = 4 992 B`, which is 10 strips per
frame. Recommend the strip form, and settle the number at P-GATE-3.

**Correction:** plan V3 says "frame buffer in PSRAM, or internal for strip blit". PSRAM is not an option
for anything the panel reads. PSRAM may hold the *source* (8-bit frame), never the *destination*.

## 3. Input

| Fact | Evidence |
|---|---|
| Event type | `src/input/KeyCodes.h:163` `struct KeyEvent {code, action, row, col, semanticId, text}` |
| Edge semantics | `KeyAction` includes PRESS, RELEASE, REPEAT |
| `MODE` is swallowed per app | `SystemApp.cpp:765-828`, one `if (ev.code == KeyCode::MODE)` per app |
| Game Boy has the exemption, and handles MODE itself | `SystemApp.cpp:845-850`. `GameBoyApp.cpp` `handleKey` first line |
| The picker maps keys narrowly | `GameBoyApp.cpp`: `UP`/`DOWN` move, `EXE` loads, `AC` exits. on Casio a digit launches directly |

fake-08 wants a **bitmask**, not events (`hostVmShared.h`): `P8_KEY_LEFT|RIGHT|UP|DOWN|O|X|PAUSE|7`,
with separate `KDown` (this frame) and `KHeld` (still down) fields in `InputState_t`. So the host keeps a
held bitset, updates it from `KeyEvent` PRESS/RELEASE, and computes `KDown` as `held & ~previousHeld` once
per frame.

Conflict to settle in code: **`AC` is X in PICO-8 and "exit" in the Game Boy picker**. In the Games list
navigation is `UP`/`DOWN`/`ENTER`, so `AC` is free to be X during play, and exit stays `MODE`/`EXE`.

Rollover is still unmeasured (hardware contract lists it as an omission). P-GATE-5 stands.

## 4. Storage

| Fact | Evidence |
|---|---|
| Wrapper exposes the calls the plan needs | `src/hal/FileSystem.h:256-278`: `exists`, `open`, `remove`, `rename`, `mkdir`, `isDirectory`. `:164` `size()` |
| Directory walk pattern | `FileSystem.h:39-45` documented, `GameBoyApp.cpp:180` implemented (`openNextFile`, `sort`) |
| Game Boy save convention | `GameBoyApp.cpp:473-508`, `sav = _romPath + ".sav"`, written after `saveDirty()` |
| No total/used wrapper | none in `FileSystem.h`. use `LittleFS.totalBytes()` / `usedBytes()` on device and stub them on host |

`/roms` is still owned by `GameBoyApp.h:114` and must be migrated (§4.1 of the plan).

## 5. Network

| Fact | Evidence |
|---|---|
| Method is a string, so `HEAD` is available | `src/net/HttpStream.h:49` `std::string method = "POST"` |
| **Response `Content-Length` is already exposed** | `HttpStream.h:93` `long contentLength() const` (returns −1 when chunked) |
| Header buffer is 2048 B, sized for real response headers | `HttpStream.h:50` |
| Streaming read, explicit status, explicit close | `:96` `read`, `:86` `status`, `:99` `close` |
| Saved networks carry their password | `src/net/Wifi.h:46` `struct WifiNetwork {ssid; pass;}` |
| Saved list API | `Wifi.h:129-130` `networkCount`/`networkAt`, `:73` `networkIndex`, `:80` `forgetNetwork` |
| Non-blocking join exists | `Wifi.h:79` `connectAsync(ssid, pass)`. `:81` blocking `connect(ssid, pass, timeoutMs)` |
| Scan is polling, not blocking | `Wifi.h:146` `startScan` (false if running), `:157` `signalFor` |
| **`disconnect(true)` erases the entire saved list** | `Wifi.h:87` comment: "eraseCreds clears the WHOLE saved list" |
| Portal owns the radio when up | `src/net/Portal.h:92-102`. portal pre-join sorts candidates by RSSI and calls `connectAsync` (`Portal.cpp:1336`) |

Consequences:

1. The size probe is one call: `HttpReq{url, method="HEAD"}` → `open()` → `contentLength()` → `close()`.
   **No new HTTP code is needed** — plan V3's §6 is implementable as written, and the earlier worry about
   header access was wrong.
2. The Games Wi-Fi screen can join a saved network by `networkAt(i, net)` then
   `connectAsync(net.ssid, net.pass)`. No new credential API.
3. **Never call `Wifi::disconnect(true)` from Games.** It wipes every saved network, including the user's
   others. Use `disconnect(false)` if a teardown is ever needed.
4. `networkCount()` caps at `kMaxNetworks = 6` (`Wifi.h:62`), which bounds the Wi-Fi screen to 7 rows and
   therefore needs a 2-row scroll at 30 px rows on 156 px of height. Settle the geometry in implementation.

## 6. Memory

| Item | Size | Where | Evidence |
|---|---|---|---|
| `PicoRam` | **64 KiB**, one `uint8_t data[0x10000]` union | PSRAM | `PicoRam.h` tail |
| Lua state heap | ~130 KB typical cart | PSRAM | z8lua `realloc` path. must be redirected |
| Panel staging | 4 992 B per 16-row strip | **internal, DMA** | §2 |
| LVGL draw buffer | 32 KiB | internal, DMA | `main.cpp:231` |
| Art tile | 32 KiB | PSRAM | our choice, no DMA |
| Feed page | 86 KB transient | PSRAM | ours |

`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` is the established pattern in this tree
(`CircuitCoreApp.cpp:127`, `MnaMatrix.cpp:58`, `NeoAST.h:154`, `NeoLanguageApp.cpp:759`). Note
`NeoAST.h:156` falls back to non-PSRAM — the Games path must **not** do that.

## 7. fake-08 port surface (pin `814991a2571a`)

The VM is host-driven, so no `main()` and no `GameLoop()` are needed:

```cpp
PicoRam* mem = new PicoRam(). Mem->Reset().          // 64 KiB, place in PSRAMem->Reset(). // 64 KiB, place in PSRAM/ 64 KiB, place in PSRAM
Pico8Host host. // subclass of Host/ subclass of Host
Audio audio(mem). // constructed, never filled/ constructed, never filled
Vm vm(&host, mem, nullptr, nullptr, &audio). 
host.setUpPaletteColors(). Host.setTargetFps(60). ost.setTargetFps(60). 
vm.LoadCart(bytes, size). // or vm.LoadCart(path)/ or vm.LoadCart(path)
// per frame:
host.updateInput(). // held bitset -> InputState_t/ held bitset -> InputState_t
vm.Step(). // the timing boundary for P-GATE-3/ the timing boundary for P-GATE-3
uint8_t* fb  = vm.GetPicoInteralFb(). 
uint8_t* pal = vm.GetScreenPaletteMap(). 
```

`Vm` public API used: `Step`, `LoadCart(data,size)`, `GetPicoInteralFb`, `GetScreenPaletteMap`,
`update_buttons`, `CloseCart`, `setTargetFps`, `getFps`, `IsPaused`, `togglePauseMenu`,
`getSerializedCartData`, `deserializeCartDataToMemory`, `vm_cartdata`.

`Host` methods the ESP32 subclass must implement (from `source/host.h`):

```
oneTimeSetup(Audio*)                 shouldRunMainLoop()      scanInput() -> InputState_t
shouldQuit()                         drawFrame(fb, paletteMap, drawMode)
waitForTargetFps()                   setTargetFps(int)        deltaTMs()
shouldFillAudioBuff()                getAudioBufferPointer()  getAudioBufferSize()
playFilledAudioBuffer()              oneTimeCleanup()
getCartDataFile(key)                 getCartDataFileContents(key)   saveCartData(key, contents)
getFileContents(name, buf)           writeBufferToFile(key, buf, len)
getCartDirectory() / setCartDirectory  listcarts() / listdirs()
getSetting(name) / setSetting(name,val)  loadSettingsIni() / saveSettingsIni()
GetPaletteColors()                   setUpPaletteColors()      forceStretch() / changeStretch()
```

Notes that change the estimate:

- `Host`'s constructor takes `(windowWidth, windowHeight)` and `setPlatformParams` takes SDL-style flags.
  Both are ignorable, but they mean the header drags in `Audio.h` → `synth.h`. Stub it. Do not link SDL.
- Audio is a class with a filled-buffer protocol. The cheapest honest path is to instantiate it and never
  call `FillAudioBuffer`. Then `sfx()` and `music()` run their state machine, and the port produces no samples.
  `FakoSettings.h` also exists upstream — decide once whether its in-VM settings screen is suppressed
  (recommended: suppress, and keep our pause menu).
- `Logger_*` is used by upstream `main.cpp` only. A host-driven port need not initialise it. host-driven port need not initialise it.

## 8. Build and test surface

| Fact | Evidence |
|---|---|
| 15 build envs. device is `esp32s3_n16r8`, host is `emulator_pc` | `platformio.ini:9`, `:359` |
| Source selection is per env via `build_src_filter` | `platformio.ini:111`, `:228` |
| A new `src/games` directory must be added to the host env too, or the emulator build will not see it | consequence of the filters |
| Host test harness exists | `tests/host`, `tests/emulator`. `tests/wasm` holds the playwright UI harness used for the mockups |
| No `pio test` runner | known repo limitation. tests run through the emulator and validation envs |

## 9. What this recon confirms

- The plan's module boundaries hold. Nothing in the tree blocks a Games app.othing in the tree blocks a Games app.
- The port is host-driven (`Vm::Step`). Pacing, pause and the frame hook are ours. That is what makes
  156×156 and the strip blit possible without touching the VM.
- `HttpStream` already has `contentLength()`, so the size probe needs no new networking code.
- WiFi credentials for saved networks are reachable, so the Games Wi-Fi screen is a UI job.

## 10. Corrections to plan V3

| # | Plan said | Recon says | Action |
|---|---|---|---|
| C1 | Frame buffer "PSRAM, or internal for strip blit" | PSRAM cannot feed TFT_eSPI DMA at all (`main.cpp:226`) | Destination is internal-only. PSRAM holds the 8-bit source only |
| C2 | Play area 128×128 at (96,14) | Requested 156×156 at (82,0) | Update D17 and §5.4. note 39/32 scaling and 9.73 ms SPI |
| C3 | New app id 24 | 24 is past the AI entry. 22 is the free hole | Use id 22 |
| C4 | Size probe may need new HTTP code | `contentLength()` exists (`HttpStream.h:93`) | Drop that risk from §13 |
| C5 | Wi-Fi teardown unspecified | `disconnect(true)` wipes all saved networks | Add an explicit "never erase credentials" rule |
| C6 | Memory table puts the frame buffer in PSRAM | As C1 | Add a 4 992 B internal strip staging row |

## 11. Open items that need hardware

1. Internal SRAM actually free at runtime, with radio up, after the 32 KiB draw buffer and the 4 992 B
   strip buffer. (P-GATE-2)
2. Measured SPI throughput, not the 5 MB/s nominal. (P-GATE-3)
3. Whether 156×156 at 1.21875 looks acceptable on the panel. The 39/32 rhythm is the risk, and only thehe 39/32 rhythm is the risk, and only the
   panel can settle it.
4. Rollover under X held plus a direction. (P-GATE-5)
