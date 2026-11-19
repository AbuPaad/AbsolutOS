# NumOS Games + PICO-8 — implementation plan v2

Status: plan for approval. Nothing implemented. Supersedes the flash and frontend numbers in
`NUMOS_GAMES_FRONTEND_AND_PICO8_PLAN.md` (Oct 2026). Mockup: `~/musings/numos-pico8-mockup.html`.

## 1. Decisions locked by this revision

| # | Decision | Value |
|---|---|---|
| D1 | Entry screen | Two tiles only: Online (globe), Local (folder) |
| D2 | Offline behaviour | The globe opens the Wi-Fi screen first. The list is unreachable without a link |
| D3 | After a connect | Go directly to the online list, page 1 |
| D4 | Local root | `/roms` |
| D5 | Local storage | Internal LittleFS only. No SD card in this scope |
| D6 | Local layout | `/roms/pico8/` and `/roms/gb/`. One list contains both |
| D7 | Row content | Name. File size. Category |
| D8 | List geometry | Rows at x 0–178. Art panel 128×128 at (186,14) |
| D9 | Download feedback | Byte-driven progress bar, then a tick, then `EXE play` |
| D10 | Wi-Fi screen | The existing Settings view, reused. Not a second implementation |
| D11 | Audio | None. `sfx()` and `music()` are no-ops |
| D12 | Carts | User-supplied or user-fetched. Never bundled, never mirrored |

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
src/games/GbRuntime.*        wraps the existing GameBoyCore. No behaviour change
src/games/Pico8Runtime.*     wraps the vendored fake-08 VM + z8lua
src/games/Pico8Host.*        the one ESP32 host layer: frame, input, timing
src/ui/games/GameHome.*      the two-tile entry
src/ui/games/GameList.*      one list component. Two providers
src/ui/games/GameArt.*       128×128 art: cart pixels, or a generated placeholder
src/ui/games/GameBezel.*     framed 1× window, gutters, badges, FPS
src/ui/games/PauseMenu.*     shared overlay for both runtimes
src/ui/wifi/WifiView.*       extracted from SettingsApp. Owned here, used by Settings and Games
src/hal/RomStore.*           /roms scan, subfolders, free space, download target
```

Row type. One type for both providers.

```cpp
struct GameItem {
    char      name[64];      // "black_blizzard.p8.png" or "Black Blizzard"
    char      category[32];  // "PICO-8 · action" or "Game Boy"
    uint32_t  bytes;         // rendered as "24.0 KB"
    GameSource source;       // Local | Online
    bool      installed;     // tick. True only after a verified write
    char      resolvedId[40];// cached lid or slug (online)
    char      path[64];      // /roms/pico8/... (local)
};
```

Rules. Keep these rules in review.

1. The list and the bezel do not include a core header. They call `RuntimeHost`.
2. One list component and one row type. Online and local differ in the provider only.
3. A runtime does not draw chrome. It publishes a framebuffer.
4. UI code uses `ThemeManager` tokens. UI code does not use colour literals.
5. The Wi-Fi view exists once.

## 4. `/roms` on internal LittleFS

| Item | Value |
|---|---|
| Partition | LittleFS, 2.9375 MiB |
| Root | `/roms` |
| Subfolders | `pico8/` (`.p8.png`), `gb/` (`.gb`, `.gbc`) |
| Scan | One `opendir` per subfolder at entry. Read each `stat` once and keep the result in the row |
| Sort | Category, then name. Online sorts by feed order |
| Category source | The subfolder. Not the extension |

Capacity. A 35 KB cart uses 1.2 % of the volume. The volume holds about 80 carts, or 5 512 KB ROMs, or
2 1 MB ROMs. The list header shows free space. The download path verifies free space before it opens a socket.

### 4.1 Capacity limits to accept

Internal-only storage makes Game Boy support a demo, not a library. One 512 KB ROM costs more than 14 carts.
Record the limit in About. Do not promise a ROM library.

## 5. Screens

### 5.1 Home

| Element | Geometry | Content |
|---|---|---|
| Header | 320×12 | `GAMES` left. Free space right |
| Tile Online | 148×130 at (8,18) | Globe icon 64×64. `ONLINE`. Subline: `connected · <ssid>` or `no connection` |
| Tile Local | 148×130 at (164,18) | Folder icon 64×64. `LOCAL`. Subline: `<n> files · /roms` |
| Focus | 1 px accent border | Unfocused tile dims to 55 % |

The icons are 32×32 pixel art with a black foreground and alpha. Draw them at 64×64 with
`image-rendering: pixelated`. Invert them on a dark theme. Do not smooth and do not recolour.

### 5.2 Wi-Fi (gate)

Reuse `SettingsApp::buildWifiView` behaviour exactly.

| Row | Label | Value |
|---|---|---|
| 0 | `Web portal` | `OFF` / `scanning` / `joining` / `ON` / `FAIL` |
| 1..n | `<i>. <ssid>` | `connected -55 dBm` / `-71 dBm` / `scanning` / `not in range` |
| empty | `no networks saved` | `use portal` |

Keys. `ENTER` toggles the portal on row 0, or connects on a network row. `RIGHT` rescans. `EXE` returns to Home.
Do not scan while the portal is up. A scan takes the radio from a client during association.

A successful join sets the link, then opens the online list at page 1.

### 5.3 List

| Element | Geometry | Content |
|---|---|---|
| Header | 320×14 | `LOCAL · /ROMS` or `ONLINE · BBS · CC4`. Right: counts or free space |
| Rows | x 0–178. 22 px plus 1 px gap. 6 visible | Line 1: name, then `✓` when installed. Line 2: `<size> · <category>` |
| Progress | Line 2, during a fetch | 118 px track, 3 px fill, percentage |
| Scroll thumb | 2 px at x=178 | Only when the list is longer than 6 rows |
| Art panel | 128×128 at (186,14) | Cart pixels, cover-cropped. Category tag bottom-left. Author below |
| Footer | 320×13 | Action hint for the current state |

Keys. `UP` `DOWN` move the row. `LEFT` `RIGHT` page the feed. `ENTER` plays a local row, plays an installed
online row, or starts a download. `EXE` plays the focused row when it is installed. `EXE` returns to Home
otherwise.

This last rule is a conflict to settle at implementation. `EXE` is the back key on this device. The mockup
uses it for play after a download because the mockup shows the prompt `EXE play`. Recommended resolution:
show `ENTER play` in the footer, and keep `EXE` as back. Replace the prompt string with `ENTER play`.

### 5.4 Play and Pause

No change from v2. The play area is 128×128 at (96,14). The left gutter shows the system badge and the save
chip. The right gutter shows the live frame rate and the key legend. Pause is a 190 px overlay with six rows.
Exit uses deferred teardown.

## 6. Download state machine

```
idle ──ENTER──▶ space ──enough──▶ fetching ──▶ verifying ──▶ installed
                   │                       │             │
                   └─no space──▶ refused   └─error──▶ failed ──▶ idle (row keeps no tick)
```

| State | Row | Footer | Condition to leave |
|---|---|---|---|
| idle | name / `24.0 KB · PICO-8 · action` | `ENTER download` | `ENTER` |
| space | unchanged | `verifying space` | free space ≥ bytes + 4 KiB |
| fetching | progress bar and percentage | `downloading… 46%` | last byte written |
| verifying | progress bar at 100 % | `verifying` | size matches the feed value and the PNG magic matches |
| installed | name `✓` | `✓ saved /roms/pico8 · ENTER play` | — |
| refused | unchanged | `no space: needs 24.0 KB` | 2 s |
| failed | no tick | `fetch failed · EXE back` | user action |

Rules.

1. The progress bar follows the byte count from the stream. Do not animate it from a timer. A stalled fetch
   must stall the bar.
2. Set the tick only after `close()` succeeds and the written size equals the declared size.
3. Reject a response with status 200 and 0 bytes. This is the usual failure on this site. It is not a
   success.
4. Verify the PNG signature before the write. Do not write a file that will not load.
5. Write to `/roms/pico8/<resolvedId>.p8.png`. Write to a temporary name and rename it when verification passes.
   A partial file must not appear in the list.
6. Rate limit: one request per second, and back off on 429.

Resolution order for the id.

```
resolve(item):
  for id in [item.slug (url-safe only), item.lid]:
      r = GET cart_url(id)
      if r.status == 200 and r.bytes > 0 and r.contentType == image/png and r.magic == PNG:
          item.resolvedId = id
          return r
  return failure
```

Numeric ids shard by `floor(id/10000)`. Slugs shard by their first two characters. New carts do not answer
on their numeric id. Cart 160588 returned 200 with 0 bytes, while slug `fbibohate` returned 3278 bytes.

## 7. Catalogue

```
GET https://www.lexaloffle.com/bbs/?cat=7&sub=2&mode=carts[&orderby=featured|lucky][&cc4=1][&page=N]
```

| Field | Value |
|---|---|
| Response | About 86 KB of HTML |
| Data | `pdat=[ ... ]`. Not JSON: single quotes, backtick strings, trailing commas |
| Rows per page | 30 |
| Last page | 401. About 12 060 carts |
| Field 0 | tid |
| Field 1 | numeric id |
| Field 2 | title |
| Field 3 | thumbnail |
| Field 6 | posted date |
| Field 8 | author |
| Field 18 | tags |
| Field 19 | flags. Bit 2 means a renderable cart |
| Field 22 | slug, or the title again |

Limits. About 15 rapid requests return 429. Thread pages no longer contain the cart link or the licence
string, because JavaScript builds both. No licence badge until a source exists.

## 8. Frame path

| Step | Cost |
|---|---|
| PICO-8 frame, 128×128 RGB565 | 32 768 B |
| SPI at 40 MHz | 6.55 ms |
| LVGL compositing | about 3 ms |
| Total draw | about 9.5 ms |
| VM step, light cart | 1.7–2.6 ms |
| VM step, Celeste average | 15.8 ms |

`DisplayDriver::lvglFlushCb` does one `setAddrWindow` and then a blocking `pushColors`. All DMA branches are
disabled with `#if 0` at lines 651–720.

Order of work.

1. Add the strip blit. Convert 8 strips of 16 rows with a 256-entry palette-to-RGB565 table. Push each strip
   to the panel. LVGL then draws only the chrome. Expected draw cost: 4–5 ms.
2. Do not allocate a full-frame PSRAM buffer. The reference port lost 4.6× on that path.

## 9. Memory

| Allocation | Location | Size |
|---|---|---|
| Lua heap, typical cart | PSRAM | about 130 KB |
| Frame buffer, 128×128 RGB565 | PSRAM, or internal for strip blit | 32 KiB |
| Art, one item | PSRAM | 32 KiB |
| Feed page | PSRAM, then freed | 86 KB |
| mbedTLS record buffers | Internal. Cannot move | 2 × 16 KiB |

Do not let the Lua allocator fall back to internal SRAM. A silent fallback causes an out of memory error
two screens later. Fail at allocation.

## 10. Work breakdown

### Wave 1 — plumbing (3 days)

| # | Ticket | Est. |
|---|---|---|
| 1 | Add `appid` 24, `Mode::APP_PICO8`, the `APP_GAMEBOY` call sites, the launcher entry | 0.5 d |
| 2 | Add `RuntimeHost` and `GbRuntime`. Make GameBoyApp the first consumer | 2 d |
| 3 | Extract `WifiView` from `SettingsApp`. Keep Settings behaviour identical | 0.5 d |

### Wave 2 — frontend (7.5 days)

| # | Ticket | Est. |
|---|---|---|
| 4 | Home: two tiles, icons, focus | 1 d |
| 5 | `RomStore`: `/roms` scan, subfolders, free space | 1 d |
| 6 | List: rows, scroll, art panel | 2 d |
| 7 | Online provider: feed fetch, `pdat` parse, paging, 429 backoff | 2 d |
| 8 | Download state machine, progress, tick, atomic rename | 1.5 d |

### Wave 3 — runtime (6–9 days)

| # | Ticket | Est. |
|---|---|---|
| 9 | Vendor fake-08 and z8lua. Add `Pico8Host`. Pass P-GATE-1 | 5–8 d |
| 10 | Bezel and pause overlay | 1 d |

### Wave 4 — frame path (1–2 days)

| # | Ticket | Est. |
|---|---|---|
| 11 | Strip blit. Measure before and after | 1–2 d |

### Wave 5 — test harness (1 day)

| # | Ticket | Est. |
|---|---|---|
| 12 | Serial harness and golden frames | 1 d |

Total: 18.5 to 21.5 days of work. Wave 2 does not depend on Wave 3.

## 11. Gates

**P-GATE-1 — build.** Build fake-08 and z8lua for `esp32s3_n16r8` with `-fjump-tables`. Report the flash
delta and the internal-SRAM delta. Pass: flash delta is 300 KiB or less, internal-SRAM delta is 24 KiB or
less.

**P-GATE-2 — memory.** Start the radio. Report the internal-SRAM headroom. Prove that the Lua heap is in
PSRAM. Prove that allocation fails instead of falling back.

**P-GATE-3 — frame time.** Time `Step()` and the flush separately for Celeste and for one light cart. Report
both numbers in milliseconds.

**P-GATE-4 — pacing.** Verify that the host resumes at 60 Hz. A cart that divides `_update` to 30 Hz must
run at normal speed.

**P-GATE-5 — rollover.** Hold X and press each direction. Verify that the driver drops no key and ghosts
no key.

**P-GATE-6 — download integrity.** Fetch a cart that answers 200 with 0 bytes. Verify that no tick appears
and that no file remains in `/roms/pico8`.

**P-GATE-7 — free space.** Fill the filesystem until 10 KB remains. Start a download of a 24 KB cart.
Verify that the request is refused before the socket opens.

## 12. Risks

| Risk | Effect | Response |
|---|---|---|
| Internal flash limits ROM size | Game Boy support is a demo | Show free space. Refuse large downloads early |
| Licence data has no source | No licence badge | Do not show a badge. Say so in About |
| Rollover is not measured | Input loss under load | P-GATE-5 before the runtime work |
| Blocking flush | 20 fps on dense rooms | Strip blit in Wave 4 |
| `EXE` is the back key | Prompt conflict | Use `ENTER play` in the footer |

## 13. Non-goals

1. No SD card support in this revision.
2. No bundled carts and no mirror.
3. No audio.
4. No second Wi-Fi implementation.
5. No frame rate claim above "light carts reach 60 fps".
