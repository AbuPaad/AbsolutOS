# NumOS Games + PICO-8 — implementation plan v3

Status: revised for approval. Nothing implemented. Supersedes `NUMOS_GAMES_FRONTEND_AND_PICO8_PLAN.md`
(Oct 2026) and `NUMOS_GAMES_PICO8_PLAN_V2.md`. Mockup: `~/musings/numos-pico8-mockup.html`
(the mockup lags v3 at §5.3 and §5.5).

Recon evidence for every code claim lives in `NUMOS_GAMES_RECON.md`.

The changes since v2 come from a review against the live feed and the tree. Each one replaces an assumption
that the code does not support.

## 1. Decisions

| # | Decision | Value |
|---|---|---|
| D1 | Entry screen | Two tiles only: Online (globe), Local (folder) |
| D2 | Offline behaviour | The globe opens the Wi-Fi screen first. The list needs a link |
| D3 | After a connect | Go to the online list at page 1 |
| D4 | Local root | `/roms` |
| D5 | Local storage | Internal LittleFS only. No SD card in this scope |
| D6 | Local layout | `/roms/pico8/` for carts, `/roms/gb/` for Game Boy ROMs. One list contains both |
| D7 | Row content | Name. Size. Category. Online sizes come from a header probe (D13) |
| D8 | List geometry | Rows at x 0–178. Art panel 128×128 at (186,14) |
| D9 | Download feedback | Byte-driven progress bar, then a tick, then `ENTER play` |
| D10 | Wi-Fi screen | The Settings view, reused. One implementation |
| D11 | Audio | None. `sfx()` and `music()` are no-ops |
| D12 | Carts | User-supplied or user-fetched. Never bundled, never mirrored |
| D13 | Online size | `HEAD` on the sharded `cposts` URL. Focused row first, then the visible page at ≤1 req/s. Cache to disk permanently |
| D14 | Free space | Reserve 256 KB for app data. Refuse a download that would cross the reserve |
| D15 | Discovery | Featured and cc4 feeds, a lucky random pick, and a numeric-id jump. No title search |
| D16 | Removal | A Manage storage screen, reached from the list header |
| D17 | Frame geometry | The runtime declares its frame size. The bezel centres it. PICO-8 draws 156×156 at (82,0). Game Boy draws 160×144 at (80,6) |
| D18 | Save data | Game Boy: `<rom>.sav` beside the ROM. PICO-8: `/roms/pico8/<cart>.dat`, 1 KB cap |
| D19 | Third tile | Settings. A Games hub for Wi-Fi, Storage, Keymap and About. Not the system Settings app |

## 2. Flow

```
Home ─┬─ Local ─────────────────────────────── List (/roms) ── Play ── Pause
      └─ Online ─┬─ link up ────────────────── List (BBS)
                 └─ no link ── Wi-Fi ── connect ── List (BBS) page 1
```

The online branch has a gate. With no link, the globe opens the Wi-Fi screen. The list opens only after the
link is up. There is no cached-feed state, because the code never fetched a feed.

## 3. Module layout

```
src/games/RuntimeHost.h      interface only. No core headers in any UI file
src/games/GbRuntime.*        wraps the existing GameBoyCore
src/games/Pico8Runtime.*     wraps the vendored fake-08 VM + z8lua
src/games/Pico8Host.*        the one ESP32 host layer: frame, input, timing
src/ui/games/GameHome.*      the two-tile entry
src/ui/games/GameList.*      one list component. Two providers
src/ui/games/GameArt.*       128×128 art: cart pixels, or a generated placeholder
src/ui/games/GameBezel.*     framed 1× window. Centres the runtime's declared frame
src/ui/games/ManageStorage.* free space, per-item delete
src/ui/games/PauseMenu.*     shared overlay for both runtimes
src/ui/wifi/WifiView.*       extracted from SettingsApp. Used by Settings and Games
src/hal/RomStore.*           /roms scan, subfolders, free space, download target, caches
```

```cpp
struct GameItem {
    char      name[64];       // "black_blizzard.p8.png" or "Mole Mole"
    char      category[32];   // "PICO-8 · action" or "Game Boy"
    uint32_t  bytes;          // 0 = unknown. Online rows start at 0 (D13)
    GameSource source;
    bool      installed;      // tick. True only after a verified write
    char      resolvedId[40]; // cached lid or slug
    char      path[64];       // /roms/pico8/... (local)
    uint16_t  tid;            // BBS thread id (online)
};

struct RuntimeHost {
    virtual uint16_t frameW() const = 0;   // 128, or 160 (D17)
    virtual uint16_t frameH() const = 0;   // 128, or 144
    virtual void attach(lv_obj_t* surface) = 0;
    virtual void frame(const uint16_t* rgb565) = 0;
    virtual void input(const KeyEvent&) = 0;
    virtual bool save() = 0;
    virtual bool load() = 0;
    virtual uint32_t fps() const = 0;
};
```

Rules for review.

1. The list and the bezel do not include a core header. They call `RuntimeHost`.
2. One list component and one row type. Online and local differ in the provider only.
3. A runtime does not draw chrome. It publishes a framebuffer.
4. UI code uses `ThemeManager` tokens. UI code does not use colour literals.
5. The Wi-Fi view exists once. Two entry points, one screen.

## 4. `/roms` on internal LittleFS

| Item | Value |
|---|---|
| Partition | LittleFS, 2.9375 MiB, shared with `/settings.dat`, `/notes/`, `/circuits/`, `/fluid/`, `/py/`, `/ai/` |
| Root | `/roms` |
| Carts | `/roms/pico8/*.p8.png`, saves at `<cart>.dat` |
| ROMs | `/roms/gb/*.gb`, `/roms/gb/*.gbc`, saves at `<rom>.sav` |
| Caches | `/roms/.cache/: sizes.json, online.json, art/` |
| Scan | One `opendir` per subfolder on entry. One `stat` per file, kept in the row |
| Sort | Category, then name. Online follows feed order |
| Category source | The subfolder |

### 4.1 Game Boy migration

`GameBoyApp` already owns `/roms` flat and writes `<rom>.sav` beside the ROM (`GameBoyApp.cpp:481`). D6
moves the ROMs. The migration is a ticket, not a side effect.

1. Change `kRomDir` from `/roms` to `/roms/gb` (`GameBoyApp.h:114`).
2. Retire `GameBoyApp::buildPicker` and `rescanRoms`. `GameList` becomes the only ROM picker.
3. On the first boot after the update, move `/roms/*.gb`, `/roms/*.gbc` and their `.sav` files into
   `/roms/gb/`. Do not overwrite an existing file. Log the move count.
4. Move the host test fixtures `/roms/x.gb` and `/roms/rom.gb` (`FileSystem.cpp:148`, `FileSystem.h:199`).
5. Keep the save path relative to the ROM, so saves follow the move.

### 4.2 Capacity and reserve

| Quantity | Value |
|---|---|
| Partition | 2.9375 MiB |
| Reserve for app data (D14) | 256 KiB |
| Usable for `/roms` | about 2.68 MiB |
| Carts at 35 KB | about 74 |
| 512 KB ROMs | 5 |
| 1 MB ROMs | 2 |

Read free space from `LittleFS.totalBytes()` and `LittleFS.usedBytes()`. Do not use a fixed baseline.
Refuse a download when `free - bytes - 4096 < 262144`. Report the refusal in the footer, not a modal.

## 5. Screens

### 5.1 Home

| Element | Geometry | Content |
|---|---|---|
| Header | 320×12 | `GAMES` left. Free space right |
| Tile Online | 94×132 at (8,16) | Globe 64×64. `ONLINE`. Subline: `connected · <ssid>` or `no connection` |
| Tile Local | 94×132 at (113,16) | Folder 64×64. `LOCAL`. Subline: `<n> files · /roms` |
| Tile Settings | 94×132 at (218,16) | Ellipsis 64×64. `SETTINGS`. Subline: `wi-fi · storage` |
| Focus | 1 px accent border | The unfocused tile dims to 55 % |

Three tiles divide the 320 px into 94 px columns with 11 px gutters. A 64 px icon still fits with 15 px of
padding, so the two PNGs keep an exact 2× scale. The ellipsis is an SVG with `currentColor`, so it needs no
invert. Order is Online, Local, Settings: the least-used tile is last.

The icons are 32×32, black foreground, alpha. Draw at 64×64 with `image-rendering: pixelated`. Invert on a
dark theme. Do not smooth and do not recolour.

### 5.2 Wi-Fi (gate)

Reuse `SettingsApp::buildWifiView` behaviour exactly.

| Row | Label | Value |
|---|---|---|
| 0 | `Web portal` | `OFF` / `scanning` / `joining` / `ON` / `FAIL` |
| 1..n | `<i>. <ssid>` | `connected -55 dBm` / `-71 dBm` / `scanning` / `not in range` |
| empty | `no networks saved` | `use portal` |

Keys. `ENTER` toggles the portal on row 0, or connects on a network row. `RIGHT` rescans. `EXE` returns home.
Do not scan while the portal is up: a scan takes the radio from a client during association.

A successful join sets the link and opens the online list at page 1.

Never call `Wifi::disconnect` with `eraseCreds` from this screen. That flag clears the whole saved list, not
one entry (`Wifi.h`). Games has no reason to forget a network.

### 5.3 List

| Element | Geometry | Content |
|---|---|---|
| Header | 320×14 | `LOCAL · /ROMS` or `ONLINE · BBS · FEATURED`. Right: `<free> free` and `▸ storage` |
| Rows | x 0–178. 22 px plus 1 px gap. 6 visible | Line 1: name, then `✓` when installed. Line 2: `<size> · <category>` |
| Size unknown | Line 2 | `sizing…` while a probe is in flight (online only) |
| Progress | Line 2 during a fetch | 118 px track, 3 px fill, percentage |
| Scroll thumb | 2 px at x=178 | Only when the list is longer than 6 rows |
| Art panel | 128×128 at (186,14) | Cart pixels, cover-cropped. Category tag bottom-left. Author below |
| Footer | 320×13 | The action hint for the current state |

Keys.

| Key | Action |
|---|---|
| `UP` `DOWN` | Move the row |
| `LEFT` `RIGHT` | Page the feed |
| `ENTER` | Play a local row. Play an installed online row. Start a download otherwise |
| `L` | Lucky: jump to a random feed page and row |
| `ALPHA` then digits | Jump to a cart by numeric id. The row resolves it through the same `resolve()` path |
| `S` | Open Manage storage |
| `EXE` | Return home |

### 5.4 Play and Pause

The bezel asks the runtime for `frameW()` and `frameH()`. PICO-8 presents at 156×156 at (82,0): the cart frame
is 128×128, scaled by 39/32. LVGL takes scale in 1/256 units, so 312 is exact, but the mapping is not integral.
Within each 32 source columns, 7 render 2 px wide and 25 render 1 px wide. Nearest-neighbour, so there is no
blur, only an uneven rhythm. Game Boy presents at 160×144 at (80,6), unscaled. The gutters carry the badge, the save chip, the frame rate and the legend. The badge reads `P8` or
`GB` from the runtime.

Pause is a 190 px overlay with six rows. Exit uses deferred teardown.

### 5.5 Manage storage

Reached with `S` from the list header. Not a Settings screen: it exists to recover from a full filesystem.

| Element | Content |
|---|---|
| Header | `STORAGE · <free> free of 2.94 MB` |
| Rows | Every item in `/roms`, sorted by size, largest first: name, size, category |
| Footer | `ENTER delete · EXE back` |
| Second `ENTER` | A second `ENTER` on the same row. The row text changes to `ENTER again to delete` for 3 s |

Deleting a cart also deletes its `.dat`. Deleting a ROM also deletes its `.sav`. Deleting updates the list row
behind it and the header counter. This screen is the only exit from a full filesystem.

### 5.6 Settings hub (D19)

| Row | Geometry | Value | Opens |
|---|---|---|---|
| `Wi-Fi` | 312×22 at (4,16) | `connected · HomeNet` or `off` | The Wi-Fi view |
| `Storage` | 312×22 at (4,40) | `<free> free` | Manage storage |
| `Keymap` | 312×22 at (4,64) | `50-key matrix` | The key map |
| `About` | 312×22 at (4,88) | `v0.1` | An inline panel: runtime licences, file count, free space |

Four rows at 24 px, a 14 px header and a 13 px footer fill 123 px of 156. The hub needs no scrolling. It answers "what is my link, what is my space" in one place. It also lets a user
reach Wi-Fi without waiting for a fetch to fail. The list header keeps its `▸ storage` shortcut. Two doors lead to one screen. A full filesystem announces
itself in the list. A user looks for it in Settings.

Storage returns to the screen that opened it, so the back key is never a surprise.

## 6. Online size probe (D13)

The feed carries no byte size. Field 7 is not a size: cart 15133 reports 75969 there and 35484 from the
server. A size probe needs one request per cart.

| Probe | Result |
|---|---|
| `HEAD /bbs/cposts/<shard>/<lid>.p8.png` | `200`, `content-length: 35484`, `accept-ranges: bytes` |
| `HEAD /bbs/get_cart.php?cat=7&lid=<lid>` | `200`, `image/png`, **no** `content-length` |

So the probe uses the `cposts` URL, sharded by `floor(lid/10000)` or by the first two characters of the slug.

```
size(cart):
  if cache.has(cart.tid): return cache.get(cart.tid)
  r = HEAD cposts_url(cart)
  if r.status == 200 and r.contentLength > 0:
      cache.put(cart.tid, r.contentLength)   // /roms/.cache/sizes.json, survives reboot
      return r.contentLength
  return unknown
```

Order of work, per row focus.

1. Probe the focused row immediately. It shows a size in about 0.2 s.
2. Probe the other visible rows one at a time at 1 request per second, in the background.
3. Stop background probes when the user pages, downloads, or leaves the list.
4. Never re-probe a cached cart. The cache is permanent.
5. A failed probe leaves `sizing…` in the row and does not retry until the next visit.

Cost. 30 rows per page is 30 requests, about 30 s of background probing, once per cart forever. A 429 stops
the queue and retries after 30 s.

## 7. Download state machine

```
idle ──ENTER──▶ space ──enough──▶ fetching ──▶ verifying ──▶ installed
                   │                  │             │
                   └─below floor──▶ refused        └─fail──▶ failed ──▶ idle
```

| State | Row | Footer | Condition to leave |
|---|---|---|---|
| idle | name / `24.0 KB · PICO-8 · action` | `ENTER download` | `ENTER` |
| space | unchanged | `verifying space` | free − bytes − 4096 ≥ 262144 |
| fetching | progress bar and percentage | `downloading… 46%` | last byte written |
| verifying | bar at 100 % | `verifying` | size matches, PNG magic matches |
| installed | name `✓` | `✓ saved /roms/pico8 · ENTER play` | — |
| refused | unchanged | `no space: needs 24.0 KB` | 2 s |
| failed | no tick | `fetch failed · EXE back` | user action |

Rules.

1. The bar follows the byte count from the stream. Do not animate it from a timer.
2. Set the tick only after `close()` succeeds and the written size equals the declared size.
3. Reject `200` with 0 bytes. This is the usual failure on this site.
4. Verify the PNG signature before the write.
5. Write to `/roms/pico8/<resolvedId>.p8.png.tmp`, then rename. A partial file must not appear in the list.
6. One request per second. Back off on 429.
7. Leaving the list aborts the fetch and deletes the temporary file. Downloads run only from the list, so a
   runtime never overlaps a transfer.
8. Record the result in `/roms/.cache/online.json`: tid, lid, slug, title, bytes. The tick rebuilds from that
   file after a reboot. Slug field 22 is sometimes a raw title such as `Mole Mole`, so the filename uses the
   numeric id when the slug is not URL-safe.

```
resolve(cart):
  for id in [cart.slug (url-safe only), cart.lid]:
      r = GET cart_url(id)
      if r.status == 200 and r.bytes > 0 and r.contentType == image/png and r.magic == PNG:
          cart.resolvedId = id
          return r
  return failure
```

## 8. Catalogue

```
GET https://www.lexaloffle.com/bbs/?cat=7&sub=2&mode=carts[&orderby=featured|lucky][&cc4=1][&page=N]
```

| Field | Value |
|---|---|
| Response | About 86 KB of HTML |
| Data | `pdat=[ ... ]`. Not JSON: single quotes, backtick strings, trailing commas |
| Rows per page | 30 |
| Last page | 401. About 12 060 carts |
| 0 | tid |
| 1 | numeric id |
| 2 | title |
| 3 | thumbnail |
| 6 | posted date |
| 7 | a count, not a size |
| 8 | author |
| 18 | tags |
| 19 | flags. Bit 2 means a renderable cart |
| 22 | slug, or the title again |

Limits. About 15 rapid requests return 429. Thread pages no longer contain the cart link or the licence
string, because JavaScript builds both. No licence badge until a source exists.

## 9. Frame path

| Step | Cost |
|---|---|
| PICO-8 cart frame, 128×128 indices | 16 384 B, PSRAM |
| PICO-8 presented, 156×156 RGB565 | 48 672 B |
| Game Boy presented, 160×144 RGB565 | 46 080 B |
| SPI at 40 MHz, 128×128 | 6.55 ms |
| SPI at 40 MHz, 156×156 | 9.73 ms |
| SPI at 40 MHz, 160×144 | 9.22 ms |
| LVGL compositing | about 3 ms |
| VM step, light cart | 1.7–2.6 ms |
| VM step, Celeste average | 15.8 ms |

`DisplayDriver::lvglFlushCb` does one `setAddrWindow` and then a blocking `pushColors`. All DMA branches are
disabled with `#if 0` at lines 651–720.

Order of work.

1. Add the strip blit. Convert 8 strips of 16 rows with a 256-entry palette-to-RGB565 table. Push each strip.
   LVGL then draws only the chrome. Expected draw cost: 4–5 ms.
2. Do not allocate a full-frame PSRAM buffer. The reference port lost 4.6× on that path.

## 10. Memory

| Allocation | Location | Size |
|---|---|---|
| Lua heap, typical cart | PSRAM | about 130 KB |
| Strip staging, 16 rows × 156 px × RGB565 | **internal, DMA. PSRAM cannot feed TFT_eSPI** | 4 992 B |
| PICO-8 source frame, 8-bit palette + palette map | PSRAM | 16 KiB + 144 colours |
| `PicoRam`, one 64 KiB union | PSRAM | 65 536 B |
| Art, one item | PSRAM | 32 KiB |
| Feed page | PSRAM, then freed | 86 KB |
| Size cache and online map | PSRAM, then filed | about 40 KB per 1000 carts |
| mbedTLS record buffers | Internal. Cannot move | 2 × 16 KiB |

Do not let the Lua allocator fall back to internal SRAM. Fail at allocation.

## 11. Work breakdown

### Wave 1 — plumbing (3.5 days)

| # | Ticket | Est. |
|---|---|---|
| 1 | Add app id 22, `Mode::APP_GAMES`, the `APP_GAMEBOY` call sites, the launcher entry | 0.5 d |
| 2 | Add `RuntimeHost` with frame geometry. Make GameBoyApp the first consumer | 2 d |
| 3 | Extract `WifiView` from `SettingsApp`. Keep Settings behaviour identical | 0.5 d |
| 4 | Game Boy migration: `kRomDir`, retire the picker, move files and `.sav`, move fixtures | 0.5 d |

### Wave 2 — frontend (10 days)

| # | Ticket | Est. |
|---|---|---|
| 5 | Home: two tiles, icons, focus | 1 d |
| 6 | `RomStore`: `/roms` scan, subfolders, free space, reserve floor | 1.5 d |
| 7 | List: rows, scroll, art panel, unknown-size state | 2 d |
| 8 | Online provider: feed fetch, `pdat` parse, paging, lucky, numeric-id jump, 429 backoff | 2.5 d |
| 9 | Size probe with disk cache and background queue | 1 d |
| 10 | Download state machine, progress, tick, atomic rename, sidecar map | 1.5 d |
| 11 | Manage storage: list, two-step delete, cascade to `.dat` and `.sav` | 0.5 d |
| 11b | Settings hub: four rows, About panel, return-to-origin | 0.5 d |

### Wave 3 — runtime (6–9 days)

| # | Ticket | Est. |
|---|---|---|
| 12 | Vendor fake-08 and z8lua. Add `Pico8Host`. Pass P-GATE-1 | 5–8 d |
| 13 | Bezel with runtime-declared geometry, pause overlay, PICO-8 save path | 1 d |

### Wave 4 — frame path (1–2 days)

| # | Ticket | Est. |
|---|---|---|
| 14 | Strip blit. Measure before and after | 1–2 d |

### Wave 5 — test harness (1 day)

| # | Ticket | Est. |
|---|---|---|
| 15 | Serial harness and golden frames | 1 d |

Total: 22 to 25 days. Wave 2 does not depend on Wave 3.

## 12. Gates

**P-GATE-1 — build.** Build fake-08 and z8lua for `esp32s3_n16r8` with `-fjump-tables`. Report the flash
delta and the internal-SRAM delta. Pass: flash delta is 300 KiB or less, internal-SRAM delta is 24 KiB or less.

**P-GATE-2 — memory.** Start the radio. Report the internal-SRAM headroom. Prove that the Lua heap is in
PSRAM. Prove that allocation fails instead of falling back.

**P-GATE-3 — frame time.** Time `Step()` and the flush separately for Celeste and for one light cart.

**P-GATE-4 — pacing.** Verify that the host resumes at 60 Hz. A cart that divides `_update` to 30 Hz must run
at normal speed.

**P-GATE-5 — rollover.** Hold X and press each direction. Verify that the driver drops no key and ghosts no
key.

**P-GATE-6 — download integrity.** Fetch a cart that answers 200 with 0 bytes. Verify that no tick appears
and that no file remains in `/roms/pico8`.

**P-GATE-7 — reserve.** Fill the filesystem until 300 KB remains. Start a download of a 35 KB cart. Verify
that the request is refused before the socket opens.

**P-GATE-8 — size probe.** Open one feed page. Verify that the first size arrives within 1 s and that the page
takes no more than 35 requests. Reopen the page. Verify that a cached page makes zero requests.

**P-GATE-9 — storage recovery.** Fill the filesystem to the reserve. Delete one 35 KB cart from Manage
storage. Verify that the row leaves the list and that the header count increases.

**P-GATE-10 — migration.** Put two `.gb` files and one `.sav` in `/roms`. Boot the new build. Verify that the
files move to `/roms/gb`, that saves still load, that the picker no longer exists, and that the golden frames
match.

## 13. Risks

| Risk | Effect | Response |
|---|---|---|
| Internal flash limits ROM size | Game Boy support is a demo | Show free space. Refuse large downloads early |
| Size probe costs one request per cart | 30 requests per page, 429 exposure | Cache forever. Queue behind the focused row. Stop on 429 |
| Licence data has no source | No licence badge | Do not show a badge. Say so in About |
| Rollover is not measured | Input loss under load | P-GATE-5 before the runtime work |
| Blocking flush | 20 fps on dense rooms | Strip blit in Wave 4 |
| Non-integer 39/32 scale | Uneven pixel widths at 156 | Judge on the panel. Fall back to 128×128 at 1× if it reads badly |
| Migration moves user files | Data loss on update | Move without overwrite. Log the count. P-GATE-10 |
| Portal upload cap is 8 MB | Upload fails on a 2.9 MiB volume | Report the free space on the portal page |

## 14. Non-goals

1. No SD card support in this revision.
2. No bundled carts and no mirror.
3. No audio.
4. No second Wi-Fi implementation.
5. No title search.
6. No frame rate claim above "light carts reach 60 fps".
