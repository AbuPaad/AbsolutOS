# Spotify Remote + Now Playing + Synced Lyrics — NumOS App

**Date:** 2026-10-21  
**Status:** Design  
**Target:** NumOS v1 (ESP32-S3 N16R8, 320×156 canvas, 977 KB flash headroom, Wi-Fi on)

## What it is

A Spotify Connect remote control with album art, metadata, playback controls, and optional line-by-line synced lyrics. The calculator does not play audio — it sends commands to your phone, desktop, or speaker over the Spotify Web API.

## User flow

1. **Launch the app** → Playlist browser (your saved playlists)
2. **Tap a playlist** → Track list
3. **Tap a track** or **"Play All"** / **"Shuffle"** → Now Playing screen
4. **Now Playing shows:**
   - Album art (left, 156×156 or smaller)
   - Track name, artist, album (top right)
   - Progress bar synced to real playback position
   - Playback controls: prev / play-pause / next (soft keys or on-screen)
5. **Press AC** → Toggle lyrics overlay
   - Album art thumbnail (left, smaller)
   - Scrolling synced lyrics (right), current line highlighted
   - Lyrics advance in sync with `progress_ms` from Spotify

## Prerequisites (user requirements)

- **Spotify Premium** — required for playback control endpoints and dev mode
- **Active Spotify session** — must be playing on a device (phone, desktop, speaker)
- **5-user allowlist** — dev mode limit; add Crowd Supply buyers manually
- **Wi-Fi provisioned** — NumOS already has the hotspot setup flow

## Technical stack

### Spotify Web API (all endpoints work in dev mode, no extended quota needed)

**Authorization:** OAuth 2.0 Authorization Code Flow with PKCE  
**Scopes required:**
- `user-read-playback-state` — read currently playing track
- `user-modify-playback-state` — play/pause/skip/seek
- `playlist-read-private` — read user's playlists
- `playlist-read-collaborative` — read collaborative playlists

**Endpoints:**
1. `GET /v1/me/playlists` — fetch user's playlists
2. `GET /v1/playlists/{id}/tracks` — fetch playlist tracks
3. `GET /v1/me/player/currently-playing` — poll playback state (track, progress_ms, is_playing, device)
4. `PUT /v1/me/player/play` — start/resume, optionally with context (playlist URI + track offset)
5. `PUT /v1/me/player/pause` — pause
6. `POST /v1/me/player/next` — skip to next
7. `POST /v1/me/player/previous` — skip to previous
8. `PUT /v1/me/player/seek?position_ms=...` — seek (optional, for progress bar scrubbing)
9. `PUT /v1/me/player/shuffle?state=true` — enable shuffle before play

**Polling:** 3-5 second intervals for `currently-playing` while on the Now Playing screen. Interpolate `progress_ms` locally between polls: `progress_ms + (millis() - last_poll_time)`.

**Rate limits:** Dev mode quota is per-account, shared across all Client IDs. Poll conservatively; cache album art by `track.id`.

### LRCLIB API (free, no auth, no key)

**Base URL:** `https://lrclib.net/api`

**Endpoint:** `GET /api/get?track_name={name}&artist_name={artist}&duration={seconds}`

**Response:**
```json
{
  "id": 3396226,
  "trackName": "I Want to Live",
  "artistName": "Borislav Slavov",
  "duration": 233,
  "syncedLyrics": "[00:17.12] I feel your breath upon my neck\n[00:22.84] A soft caress as cold as death\n[03:25.72] ",
  "plainLyrics": "I feel your breath upon my neck\nA soft caress as cold as death\n..."
}
```

**LRC format:** `[mm:ss.xx] lyric line` — parse once per track, store as `std::vector<LyricLine>` in PSRAM.

**Fallback:** If LRCLIB returns no lyrics (instrumental, not in DB), show "No lyrics available" or fall back to `plainLyrics` if present.

### Image fetching and decoding

**Album art URL:** Spotify returns `track.album.images[]` — array of `{url, width, height}`. Pick the ~300×300 one (usually `images[1]`).

**Fetch:** `net::HttpStream` GET request (HTTPS, TLS already working, buffers in PSRAM).

**Decode:** LVGL's built-in `lv_tjpgd` (TJpgDec library, already linked). Decode to RGB565 in PSRAM, then blit tiles to the LVGL canvas draw buffer (internal DMA RAM) via `lv_canvas` or `lv_image`.

**Cache:** Store the last fetched `track.id` and album art buffer pointer. Only re-fetch when `track.id` changes.

### Memory budget

| Component | Size | Location |
|-----------|------|----------|
| TLS session (2×16 KB buffers) | 32 KB | PSRAM (already redirected) |
| JSON parse buffer (currently-playing) | 4-8 KB | PSRAM (ArduinoJson with PSRAM allocator) |
| LRC parsed lyrics (50 lines) | ~10 KB | PSRAM (`std::vector<LyricLine>`) |
| Album art JPEG decode work buffer | ~3 KB | PSRAM |
| Album art RGB565 output (300×300) | 180 KB | PSRAM (decode once, reuse) |
| App code (SpotifyApp.cpp + screens) | ~20-30 KB | Flash |
| LVGL draw buffer (existing) | ~32 KB | Internal DMA RAM (already allocated) |

**Flash:** 977 KB headroom — plenty for this app.

**RAM:** Everything except the LVGL draw buffer sits in PSRAM. The ESP32-S3 has 8 MB PSRAM; this app uses ~240 KB total.

## Code structure

### App files

**`src/apps/SpotifyApp.{h,cpp}`**

Contract:
```cpp
class SpotifyApp {
public:
    void begin();   // OAuth flow if no token, else load playlist screen
    void end();     // teardown screens, release resources
    void load();    // create LVGL screens (called from begin())
    void handleKey(const KeyEvent& ev);
    void update();  // poll Spotify every 3-5s, update UI, advance lyrics
};
```

**Screens:**
1. `_playlistScreen` — scrollable list of playlists
2. `_trackListScreen` — scrollable list of tracks in selected playlist
3. `_nowPlayingScreen` — album art + metadata + controls
4. `_lyricsScreen` — lyrics overlay (toggled by AC key)

**State:**
```cpp
enum class State { PLAYLISTS, TRACKS, NOW_PLAYING, LYRICS };
State _state = State::PLAYLISTS;

String _currentTrackId;        // cache for album art fetch
lv_img_dsc_t* _albumArt = nullptr;  // decoded RGB565, in PSRAM
std::vector<LyricLine> _lyrics;     // parsed LRC
uint32_t _lastPollMs = 0;
uint32_t _progressMs = 0;
bool _isPlaying = false;
```

### Helper classes

**`src/apps/SpotifyClient.{h,cpp}`** (or inline in SpotifyApp.cpp)

```cpp
class SpotifyClient {
public:
    bool fetchPlaylists(std::vector<Playlist>& out);
    bool fetchPlaylistTracks(const String& playlistId, std::vector<Track>& out);
    bool getCurrentlyPlaying(TrackState& out);
    bool play(const String& contextUri, int offset = 0);
    bool pause();
    bool next();
    bool previous();
    bool seek(uint32_t positionMs);
    bool shuffle(bool enable);
};

struct TrackState {
    String trackId;
    String trackName;
    String artistName;
    String albumName;
    String albumArtUrl;
    uint32_t durationMs;
    uint32_t progressMs;
    bool isPlaying;
};
```

**`src/apps/LRCParser.{h,cpp}`** (or inline)

```cpp
struct LyricLine {
    uint32_t timeMs;
    String text;
};

std::vector<LyricLine> parseLRC(const String& lrc);
```

Simple parser: find `[mm:ss.xx]`, convert to milliseconds, extract text until `\n`, repeat.

### OAuth flow (one-time setup)

**Authorization Code with PKCE** (no client secret on device):

1. Generate `code_verifier` (random 128-char string) and `code_challenge` (SHA256 base64url)
2. Open Spotify authorize URL in the Portal captive page or print a QR code on screen:
   ```
   https://accounts.spotify.com/authorize?
     client_id=YOUR_CLIENT_ID
     &response_type=code
     &redirect_uri=http://numos.local/callback
     &scope=user-read-playback-state user-modify-playback-state playlist-read-private playlist-read-collaborative
     &code_challenge_method=S256
     &code_challenge=BASE64URL_SHA256_OF_VERIFIER
   ```
3. User logs in on their phone, approves, gets redirected to `http://numos.local/callback?code=...`
4. NumOS's web server (Portal) captures the `code`, exchanges it for an access token:
   ```
   POST https://accounts.spotify.com/api/token
   Body: grant_type=authorization_code&code=...&redirect_uri=...&client_id=...&code_verifier=...
   ```
5. Store `access_token` and `refresh_token` in LittleFS (`/spotify_tokens.json`)
6. When `access_token` expires (1 hour), refresh it:
   ```
   POST https://accounts.spotify.com/api/token
   Body: grant_type=refresh_token&refresh_token=...&client_id=...
   ```

**Simplification:** Use the existing `net::Portal` web server for the redirect callback. Add a `/callback` route that captures the `code` query param, completes the token exchange, and redirects to a "Success, return to your calculator" page.

### Lyrics sync logic

```cpp
void SpotifyApp::update() {
    uint32_t now = millis();
    
    // Poll Spotify every 3-5s
    if (now - _lastPollMs > 3000) {
        TrackState state;
        if (_client.getCurrentlyPlaying(state)) {
            _progressMs = state.progressMs;
            _isPlaying = state.isPlaying;
            _lastPollMs = now;
            
            // New track? Fetch lyrics + album art
            if (state.trackId != _currentTrackId) {
                _currentTrackId = state.trackId;
                fetchLyrics(state.trackName, state.artistName, state.durationMs / 1000);
                fetchAlbumArt(state.albumArtUrl);
            }
            
            updateNowPlayingUI(state);
        }
    }
    
    // Interpolate progress between polls
    if (_isPlaying) {
        _progressMs += (now - _lastPollMs);
    }
    
    // Update lyrics highlight
    if (_state == State::LYRICS) {
        updateLyricsHighlight(_progressMs);
    }
}

void SpotifyApp::updateLyricsHighlight(uint32_t progressMs) {
    for (size_t i = 0; i < _lyrics.size(); ++i) {
        if (i + 1 < _lyrics.size()) {
            // Highlight if progressMs is between this line and the next
            if (progressMs >= _lyrics[i].timeMs && progressMs < _lyrics[i + 1].timeMs) {
                highlightLine(i);
                return;
            }
        } else {
            // Last line
            if (progressMs >= _lyrics[i].timeMs) {
                highlightLine(i);
                return;
            }
        }
    }
}
```

## App registration

**SystemApp.h:**
```cpp
enum class Mode {
    // ...existing modes...
    APP_SPOTIFY = 22,
};
```

**SystemApp.cpp:**
```cpp
// In initApps()
_spotifyApp = new SpotifyApp();

// In _apps table (around line 331)
_apps.emplace_back(22, "Spotify", icon_Settings);  // TODO: proper icon

// In switchApp()
case Mode::APP_SPOTIFY:
    if (_spotifyApp) _spotifyApp->begin();
    break;

// In teardownModeNow()
case Mode::APP_SPOTIFY:
    if (_spotifyApp) _spotifyApp->end();
    break;
```

**MainMenu.cpp:**  
Add `{"Spotify", 22}` to the `APPS[]` array.

**AppContext.h:**  
Add `Ctx::Spotify = 22` to the enum, update `ctxName()` and `ctxSlug()`.

**KeyContext.h:**  
Define key roles for Spotify context (e.g., AC = lyrics toggle, arrows = prev/next, EXE = play/pause).

## UI details (deferred)

You said you'll provide UI details later. For now, the plan assumes:
- **Playlists/Tracks:** Simple scrollable lists (LVGL `lv_list` or `lv_table`)
- **Now Playing:** Album art left, metadata + progress bar right, soft keys for prev/play/next
- **Lyrics:** Two-column layout (thumbnail left, scrolling lyrics right), highlighted line in accent color

Theme tokens apply (`ui::ThemeManager`, `ui::fontUi()`, etc.) — no literals.

## Testing & verification

1. **OAuth flow:** Verify token exchange works, tokens saved to LittleFS, refresh works after 1 hour
2. **Playlist fetch:** List appears, tapping a playlist loads tracks
3. **Playback control:** Play/pause/skip commands reach the active device (test with phone Spotify open)
4. **Now Playing sync:** Progress bar advances, `progress_ms` interpolation matches real playback
5. **Lyrics fetch:** LRCLIB returns synced lyrics for common tracks
6. **Lyrics highlight:** Current line highlights in sync with playback
7. **Album art:** Decodes without crash, displays correctly, cached per track
8. **Memory:** No heap exhaustion, TLS stays in PSRAM, LVGL draw buffer unaffected

## Open questions

1. **Icon for the app** — needs a custom icon (current placeholder: `icon_Settings`)
2. **Error states:** What to show if no active Spotify device, no internet, LRCLIB down, token expired?
3. **Seek interaction:** Should the progress bar be scrubbable (tap/drag to seek), or read-only?
4. **Playlist pagination:** Spotify returns 50 items by default; do we fetch all pages or cap at 50?
5. **Device picker:** If multiple devices available, let user choose, or auto-pick the active one?

## Related skills

- `numos-app-development` — app contract, LVGL rules, theme tokens, teardown, boot order
- `numos-theme-system` — colour/font tokens, generated files, no literals
- `sys-architecture` — layer graph, file map, build targets

## Files to create

- `src/apps/SpotifyApp.h`
- `src/apps/SpotifyApp.cpp`
- `src/apps/SpotifyClient.h` (or inline)
- `src/apps/SpotifyClient.cpp` (or inline)
- `src/apps/LRCParser.h` (or inline)
- `src/apps/LRCParser.cpp` (or inline)

## Dependencies

**Already in firmware:**
- `net::HttpStream` (HTTPS client, TLS in PSRAM)
- `net::Portal` (web server for OAuth callback)
- `ArduinoJson` (JSON parse, allocate in PSRAM)
- LVGL 9.2 + tjpgd (JPEG decode)
- `hal::FileSystem` (LittleFS for token storage)

**No new libraries needed.**

## Policy compliance

- ✅ **Personal use, dev mode** — you + 5 allowlist users
- ✅ **Spotify Connect remote** — not a streaming SDA, no audio playback
- ✅ **LRCLIB** — free, no auth, community-contributed lyrics (not Spotify's licensed data)
- ✅ **Attribution** — show "Powered by Spotify" on Now Playing screen, "Lyrics by LRCLIB" on lyrics screen
- ⚠️ **Syncing lyrics with playback** — Spotify policy §III.06 forbids syncing sound recordings with visual media; LRCLIB's own app does this, but policy is ambiguous. Risk: low for personal dev-mode use, higher if you ever apply for extended quota.

## Next steps

1. You provide UI layout mockups / detailed screen specs
2. I write `SpotifyApp.{h,cpp}` scaffolding + OAuth flow
3. Wire up Spotify API calls (`SpotifyClient`)
4. Add LRCLIB fetch + LRC parser
5. Implement Now Playing screen + progress sync
6. Implement Lyrics screen + highlight sync
7. Test with your Spotify Premium account
8. Add to SystemApp registry + MainMenu
9. Build, flash, verify on device

---

**Status:** Design complete. Ready for UI specs + implementation.

## Pending changes / scope clarification

**Decision (2026-10-21):** For v1, implement **Spotify only**. Rationale:

- Spotify Web API is fully standardised across platforms — one code path works on iOS, Android, Windows, macOS, Linux.
- No Bluetooth backend fragmentation (BLE AVRCP on Android, AMS on iOS = two separate code paths).
- LRCLIB lyrics lookup is also platform-agnostic — only needs `track.name`, `artist.name`, and `duration`.
- Premature to ship Bluetooth-only features (iOS AMS is closed/limited, Android AVRCP metadata support varies by app).
- Spotify Premium requirement is an acceptable gate for v1 (dev mode, 5-user allowlist matching existing NumOS pattern).

**Later phases (post-v1, not in this spec):**

1. **Apple BLE AMS** — implement after v1 ships. iOS-only backend; requires Apple Media Service GATT characteristic discovery (`89D3502B-0F36-433A-8EF4-C502AD55F8DC`). Media metadata and playback controls, no album art in protocol.

2. **Android shim APK** — implement after Apple BLE. Tiny (~50 KB) Android app that reads `MediaSessionCompat` API, translates BLE write commands → play/pause/skip. Works with any Android media app (Spotify free, YouTube Music, local players).

3. **Music lyric match / trending content** — separate content-creation workflow (not a firmware feature). Concept: calculator displays currently-playing track + LRCLIB synced lyrics, user records a short video on-calc, Instagram call-to-action at bottom. This is a content format, not a backend change — the firmware just needs to expose the now-playing + lyrics screen; the video recording / Instagram posting happens on the user's phone. Monetisation: higher-tier model / sponsor tier later.

**Current scope (v1 ONLY):**

- Spotify Web API remote (poll `currently-playing`, play/pause/skip/seek)
- Album art fetch + JPEG decode (tjpgd)
- LRCLIB synced lyrics fetch + LRC parse + line-by-line highlight sync to `progress_ms`
- Playlist browser + track select + play/shuffle
- Now Playing screen + AC-key lyrics toggle
- OAuth PKCE flow + token storage in LittleFS
- Dev mode 5-user allowlist

**Out of v1 scope:**

- Bluetooth Classic / AVRCP (ESP32-S3 has no BT Classic)
- BLE AMS (iOS only, v2)
- Android shim APK (v2)
- Playlist browsing over Bluetooth (v2)
- Any non-Spotify backend

---

**Status:** Design complete. v1 scope finalised as Spotify-only. Pending items (Apple BLE, Android shim, lyric-content workflow) tracked for post-v1.

Ready for UI specs + implementation.
