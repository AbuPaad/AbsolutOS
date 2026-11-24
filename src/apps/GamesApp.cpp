/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * GamesApp.cpp — Games front-end. See the header for the scope of this build.
 *
 * Every colour comes from ui::appSurface(ui::appid::kGames); every font from the
 * ui:: font ladder. No literals, no theme-id branch — the same rule the rest of
 * the tree follows, so a skin with no entry still renders.
 */

#include "apps/GamesApp.h"

#include "../ui/Theme.h"
#include "../ui/ThemeFonts.h"
#include "../ui/ThemeManager.h"
#include "../pico8/NumosHost.h"
#include "../games/BuiltinRoms.h"
#include "../ui/games/GamesIcons.h"

using numos::emulation::GameBoyCore;
using numos::emulation::GbButton;

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef ARDUINO
  #include <cstdio>
  #include "hal/FileSystem.h"
#else
  #include <LittleFS.h>
#endif

namespace {

constexpr int kCanvasW = 320;
constexpr int kCanvasH = 156;

// Play area: 156×156 at (82,0). The cart frame is 128×128, scaled 39/32.
constexpr int kPlayX = 82;
constexpr int kPlayY = 0;
constexpr int kPlayW = 156;
constexpr int kPlayH = 156;

constexpr int kHomeTileW = 94;
constexpr int kHomeTileH = 132;
constexpr int kHomeY     = 16;
constexpr int kHomeX[3]  = { 8, 113, 218 };

constexpr int kListRows   = 6;
constexpr int kListRowH   = 23;
constexpr int kListX      = 0;
constexpr int kListW      = 178;
constexpr int kListTop    = 16;
constexpr int kArtX       = 186;
constexpr int kArtSize    = 128;


ui::AppColours surf() { return ui::appSurface(ui::appid::kGames); }

void fmtBytes(uint32_t b, char* out, size_t n) {
    if (b < 1024u * 1024u) {
        std::snprintf(out, n, "%.1f KB", static_cast<double>(b) / 1024.0);
    } else {
        std::snprintf(out, n, "%.2f MB", static_cast<double>(b) / (1024.0 * 1024.0));
    }
}

bool hasExt(const std::string& name, const char* ext) {
    const size_t n = std::strlen(ext);
    if (name.size() < n) return false;
    std::string tail = name.substr(name.size() - n);
    for (size_t i = 0; i < tail.size(); ++i) {
        tail[i] = static_cast<char>(::tolower(static_cast<unsigned char>(tail[i])));
    }
    return tail == ext;
}

}  // namespace

GamesApp::GamesApp() {}
GamesApp::~GamesApp() { end(); }

// ═══════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::load() {
    _exit = false;
    _screenKind = Screen::Home;
    _tile = 1;
    _row = 0;
    _settingsIx = 0;
    _storageIx = 0;
    _storageArm = -1;
    _pauseIx = 0;
    _aboutOpen = false;
    _frames = 0;
    _fps = 0;
    _lastFpsMs = 0;
    _p8.end();
    _p8tried = false;
    _p8img = nullptr;

    // Celeste and Tetris travel inside the app image: write them to LittleFS
    // before the scan so a freshly flashed unit has something to play.
    numos::games::materializeBuiltinRoms();

    scan();
    buildFrame();
    rebuild();
    lv_screen_load(_scr);

    std::printf("[GAMES] load: %d item(s) in /roms\n", static_cast<int>(_items.size()));
}

void GamesApp::end() {
    stopCart();
    _p8buttons = 0;
    _p8img = nullptr;
    _fpsLabel = nullptr;
    _root = nullptr;
    if (_scr) {
        lv_obj_delete(_scr);
        _scr = nullptr;
    }
}

void GamesApp::update() {
    if (!_scr || _screenKind != Screen::Play) return;
    if (_gbReady) {
        _gb.stepFrame();
        if (_p8img) lv_obj_invalidate(_p8img);
    } else if (_p8.ready()) {
        _p8.step();
        if (_p8img) lv_obj_invalidate(_p8img);
    }
    ++_frames;
    const uint32_t now = static_cast<uint32_t>(lv_tick_get());
    if (now - _lastFpsMs >= 1000u) {
        _fps = _frames;          // one GameLoop pass per frame at this stage
        _frames = 0;
        _lastFpsMs = now;
        if (_fpsLabel) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%u fps", static_cast<unsigned>(_fps));
            lv_label_set_text(_fpsLabel, buf);
        }
    }
}

bool GamesApp::consumeExitRequest() {
    const bool e = _exit;
    _exit = false;
    return e;
}

// ═══════════════════════════════════════════════════════════════════════════
// /roms scan
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::scan() {
    _items.clear();
    scanDir("/roms/pico8", false);
    scanDir("/roms/gb", true);
    scanDir("/roms", false);   // legacy flat layout, kept until the migration lands

    std::sort(_items.begin(), _items.end(), [](const Item& a, const Item& b) {
        if (a.cat != b.cat) return a.cat < b.cat;
        return a.name < b.name;
    });
}

void GamesApp::scanDir(const char* dir, bool gb) {
    File d = LittleFS.open(dir, "r");
    if (!d || !d.isDirectory()) return;

    while (File e = d.openNextFile()) {
        if (e.isDirectory()) continue;
        const std::string name = e.name();
        // Legacy root: only the two game extensions belong in this list.
        if (!hasExt(name, ".p8.png") && !hasExt(name, ".gb") && !hasExt(name, ".gbc")) continue;

        Item it;
        it.name  = name;
        it.path  = std::string(dir) + "/" + name;
        it.bytes = static_cast<uint32_t>(e.size());
        it.gb    = hasExt(name, ".gb") || hasExt(name, ".gbc");
        it.cat   = it.gb ? "Game Boy" : "PICO-8";
        _items.push_back(it);
    }
}

uint32_t GamesApp::freeBytes() const {
#ifdef ARDUINO
    const size_t total = LittleFS.totalBytes();
    const size_t used  = LittleFS.usedBytes();
    return (total > used) ? static_cast<uint32_t>(total - used) : 0u;
#else
    // The emulated filesystem has no capacity model; report the device figure.
    return 0u;
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Widgets
// ═══════════════════════════════════════════════════════════════════════════

lv_obj_t* GamesApp::panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t fill,
                          uint32_t border, int radius, bool filled) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_hex(fill), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, filled ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, border ? 1 : 0, LV_PART_MAIN);
    if (border) lv_obj_set_style_border_color(o, lv_color_hex(border), LV_PART_MAIN);
    return o;
}

lv_obj_t* GamesApp::text(lv_obj_t* parent, int x, int y, int w, const char* s,
                         uint32_t colour, const lv_font_t* font) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    lv_obj_set_style_text_color(l, lv_color_hex(colour), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_label_set_text(l, s);
    return l;
}

lv_obj_t* GamesApp::icon(lv_obj_t* parent, const lv_image_dsc_t* src, int size, uint32_t colour) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, src);
    // Source art is 64x64 native (owner-supplied): 256 = 1:1.
    lv_image_set_scale(img, static_cast<uint32_t>(256 * size / 64));
    lv_obj_set_style_image_recolor(img, lv_color_hex(colour), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
    return img;
}

void GamesApp::header(const char* left, const char* right, uint32_t leftColour) {
    const ui::AppColours c = surf();
    panel(_root, 0, 0, kCanvasW, 14, c.pane, 0, 0, true);
    text(_root, 5, 1, 190, left, leftColour, ui::fontUiXSmall());
    text(_root, 190, 1, 125, right, c.textDim, ui::fontUiXSmall());
}


// ═══════════════════════════════════════════════════════════════════════════
// Frame + dispatch
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::buildFrame() {
    const ui::AppColours c = surf();
    _scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(c.bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_scr, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    _root = lv_obj_create(_scr);
    lv_obj_set_pos(_root, 0, 0);
    lv_obj_set_size(_root, kCanvasW, kCanvasH);
    lv_obj_set_style_bg_opa(_root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
}

void GamesApp::rebuild() {
    if (!_root) return;
#ifndef ARDUINO
    static const char* kNames[8] = { "Home", "List", "Play", "Pause", "Settings", "Wifi", "Storage", "Keymap" };
    std::printf("[GAMES] screen=%s row=%d tile=%d\n", kNames[static_cast<int>(_screenKind)], _row, _tile);
#endif
    _fpsLabel = nullptr;
    lv_obj_clean(_root);

    if (_screenKind == Screen::Play) startCartIfNeeded();

    switch (_screenKind) {
        case Screen::Home:     buildHome();     break;
        case Screen::List:     buildList();     break;
        case Screen::Play:     buildPlay();     break;
        case Screen::Pause:    buildPause();    break;
        case Screen::Settings: buildSettings(); break;
        case Screen::Wifi:     buildWifi();     break;
        case Screen::Storage:  buildStorage();  break;
        case Screen::Keymap:   buildKeymap();   break;
    }
}

void GamesApp::saveGbRam() {
    if (!_gb.ready() || !_gb.saveDirty() || _gb.saveSize() == 0) return;
    const uint8_t* data = _gb.cartRam();
    if (!data) return;
    const std::string sav = _gbPath + ".sav";
    File f = LittleFS.open(sav.c_str(), "w");
    if (!f) return;
    const size_t written = f.write(data, _gb.saveSize());
    f.close();
    if (written == _gb.saveSize()) _gb.clearSaveDirty();
}

void GamesApp::stopCart() {
    if (_gbReady) { saveGbRam(); _gb.unload(); }
    _gbReady = false;
    _p8.end();
    _p8tried = false;
    _p8row = -1;
    _gbRow = -1;
    _gbReady = false;
}

void GamesApp::startCartIfNeeded() {
    if (_items.empty()) return;
    if (_gbRow == _row && (_gbReady || _p8tried)) return;
    stopCart();
    _gbRow = _row;

    const Item& probe = _items[static_cast<size_t>(_row)];
    if (probe.gb) {
        // Same sequence GameBoyApp already runs on device: read the ROM, hand it
        // to the core, then restore the cart RAM from <rom>.sav if it exists.
        File f = LittleFS.open(probe.path.c_str(), "r");
        if (!f) { std::printf("[GB] open failed: %s\n", probe.path.c_str()); return; }
        const size_t size = f.size();
        std::vector<uint8_t> rom(size);
        const size_t got = f.read(rom.data(), size);
        f.close();
        if (got != size) { std::printf("[GB] short read\n"); return; }
        if (!_gb.loadRom(rom.data(), rom.size())) {
            std::printf("[GB] core refused %s (err=%d)\n", probe.path.c_str(),
                        static_cast<int>(_gb.lastInitError()));
            return;
        }
        _gbPath = probe.path;
        if (_gb.saveSize() > 0) {
            File s = LittleFS.open((_gbPath + ".sav").c_str(), "r");
            if (s) {
                std::vector<uint8_t> ram(static_cast<size_t>(s.size()));
                const size_t rgot = s.read(ram.data(), ram.size());
                s.close();
                if (rgot == ram.size()) _gb.loadCartRam(ram.data(), ram.size());
            }
        }
        _gbReady = true;
        std::printf("[GB] loaded %s (%u bytes)\n", probe.path.c_str(), static_cast<unsigned>(size));
        return;
    }
    _p8tried = true;
    const Item& it = _items[static_cast<size_t>(_row)];
    if (it.gb) return;                       // Game Boy is the Walnut-CGB path
    if (!_p8.load(it.path)) {
        std::printf("[P8] load failed: %s\n", it.path.c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Home
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::buildHome() {
    const ui::AppColours c = surf();

    // Icons only: no heading, no counts, no labels. The three marks carry it.
    const lv_image_dsc_t* kIcons[3] = { &ic_globe, &ic_folder, &ic_more };
    for (int i = 0; i < 3; ++i) {
        const bool on = (_tile == i);
        lv_obj_t* t = panel(_root, kHomeX[i], kHomeY, kHomeTileW, kHomeTileH,
                            on ? c.rowFocus : c.pane, on ? c.accent : c.row, 3);
        lv_obj_t* ic = icon(t, kIcons[i], 64, on ? c.title : c.textDim);
        lv_obj_set_pos(ic, (kHomeTileW - 64) / 2, (kHomeTileH - 64) / 2);
    }
}

void GamesApp::buildList() {
    const ui::AppColours c = surf();

    if (_items.empty()) {
        text(_root, 8, 66, 300, "No carts. Put .p8.png in /roms/pico8, .gb in /roms/gb.",
             c.textDim, ui::fontUiSmall());
        return;
    }

    const int count = static_cast<int>(_items.size());
    if (_row >= count) _row = count - 1;
    if (_row < 0) _row = 0;

    // Left column: the list and nothing else.
    const int listTop = 2, rowH = 22, rowsW = 186, visible = 7;
    int start = _row - 3;
    if (start > count - visible) start = count - visible;
    if (start < 0) start = 0;

    for (int i = 0; i < visible; ++i) {
        const int idx = start + i;
        if (idx >= count) break;
        const bool on = (idx == _row);
        const Item& it = _items[static_cast<size_t>(idx)];
        const int y = listTop + i * rowH;

        if (on) {
            panel(_root, 0, y, rowsW, rowH - 1, c.rowFocus, 0, 0, true);
            panel(_root, 0, y, 2, rowH - 1, c.accent, 0, 0, true);
        }
        text(_root, 7, y + 4, rowsW - 14, it.name.c_str(), on ? c.title : c.text, ui::fontUiSmall());
    }

    if (count > visible) {
        const int trackH = visible * rowH;
        int thumbH = trackH * visible / count;
        if (thumbH < 14) thumbH = 14;
        int thumbY = listTop;
        if (count > 1) thumbY += (trackH - thumbH) * _row / (count - 1);
        panel(_root, rowsW, listTop, 2, trackH, c.row, 0, 0, true);
        panel(_root, rowsW, thumbY, 2, thumbH, c.accent, 0, 0, true);
    }

    // Right column: artwork at the top, every detail stacked underneath it.
    const Item& cur = _items[static_cast<size_t>(_row)];
    const int artW = 108, artX = 198, artY = 4;
    panel(_root, artX, artY, artW, artW, c.bg, c.row, 2, true);
    char glyph[3] = { cur.gb ? 'G' : 'P', cur.gb ? 'B' : '8', 0 };
    text(_root, artX + artW / 2 - 10, artY + artW / 2 - 8, 30, glyph, c.accent, ui::fontDisplay());

    char sizeBuf[24];
    fmtBytes(cur.bytes, sizeBuf, sizeof(sizeBuf));
    int y = artY + artW + 4;
    text(_root, artX, y, artW, sizeBuf, c.text, ui::fontUiSmall()); y += 14;
    text(_root, artX, y, artW, cur.gb ? "Game Boy" : "PICO-8", c.accent, ui::fontUiXSmall()); y += 12;

    // Path of the file, still under the art: the only per-item detail the row
    // model carries today.
    text(_root, artX, y, artW, cur.gb ? "/roms/gb" : "/roms/pico8",
         c.textDim, ui::fontUiXSmall());

}

// ═══════════════════════════════════════════════════════════════════════════
// Play + pause
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::buildPlay() {
    const ui::AppColours c = surf();
    const Item* sel = _items.empty() ? nullptr : &_items[static_cast<size_t>(_row)];

    lv_obj_t* play = panel(_root, kPlayX, kPlayY, kPlayW, kPlayH, 0x000000, c.row, 0, true);

    if (_gbReady) {
        // 160x144 at 1:1, centred in the 156x156 area (GameBoyCore's own space).
        _gbDsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        _gbDsc.header.cf     = LV_COLOR_FORMAT_RGB565;
        _gbDsc.header.flags  = 0;
        _gbDsc.header.w      = GameBoyCore::kWidth;
        _gbDsc.header.h      = GameBoyCore::kHeight;
        _gbDsc.header.stride = GameBoyCore::kWidth * 2;
        _gbDsc.data_size     = static_cast<uint32_t>(GameBoyCore::kWidth) *
                               static_cast<uint32_t>(GameBoyCore::kHeight) * 2u;
        _gbDsc.data          = reinterpret_cast<const uint8_t*>(_gb.framebuffer());
        _p8img = lv_image_create(play);
        lv_image_set_src(_p8img, &_gbDsc);
        lv_image_set_scale(_p8img, 256);
        lv_obj_set_pos(_p8img, (kPlayW - GameBoyCore::kWidth) / 2,
                              (kPlayH - GameBoyCore::kHeight) / 2);
    } else if (_p8.ready()) {
        // 128x128 RGB565 scaled 312/256 = 1.21875 into 156x156.
        _p8dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        _p8dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
        _p8dsc.header.flags  = 0;
        _p8dsc.header.w      = numos_pico8::kFrameW;
        _p8dsc.header.h      = numos_pico8::kFrameH;
        _p8dsc.header.stride = numos_pico8::kFrameW * 2;
        _p8dsc.data_size     = static_cast<uint32_t>(numos_pico8::kFrameW) *
                               static_cast<uint32_t>(numos_pico8::kFrameH) * 2u;
        _p8dsc.data          = reinterpret_cast<const uint8_t*>(numos_pico8::frame565());
        _p8img = lv_image_create(play);
        lv_image_set_src(_p8img, &_p8dsc);
        lv_image_set_scale(_p8img, 312);
        lv_obj_set_pos(_p8img, 0, 0);
    } else {
        _p8img = nullptr;
        text(play, 8, 52, kPlayW - 16, sel ? sel->name.c_str() : "(no cart)",
             c.textDim, ui::fontUiXSmall());
        const char* msg = _p8.error();
        if (!msg) msg = sel && sel->gb ? "GB core refused this ROM" : "PICO-8 runtime not loaded";
        text(play, 8, 68, kPlayW - 16, msg, c.accent, ui::fontUiXSmall());
    }

    // Left gutter: badge and title.
    panel(_root, 0, 0, kPlayX, kCanvasH, c.bg, 0, 0, true);
    text(_root, 4, 4, kPlayX - 8, (sel && sel->gb) ? "GB" : "P8", c.accent, ui::fontUiSmall());
    if (sel) text(_root, 4, 20, kPlayX - 8, sel->name.c_str(), c.text, ui::fontUiXSmall());
    panel(_root, 4, 64, 34, 14, c.bg, c.row, 2, false);
    text(_root, 7, 64, 30, "SAVE", c.textDim, ui::fontUiXSmall());

    // Right gutter: frame meter and the panel numbers.
    panel(_root, kPlayX + kPlayW, 0, kCanvasW - (kPlayX + kPlayW), kCanvasH, c.bg, 0, 0, true);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u fps", static_cast<unsigned>(_fps));
    _fpsLabel = text(_root, kPlayX + kPlayW + 4, 4, 60, buf, c.accent, ui::fontUiSmall());
    text(_root, kPlayX + kPlayW + 4, 20, 60, _gbReady ? "160x144" : "156x156", c.textDim, ui::fontUiXSmall());
    text(_root, kPlayX + kPlayW + 4, 32, 60, _gbReady ? "46.1 KB" : "48.7 KB", c.textDim, ui::fontUiXSmall());
    text(_root, kPlayX + kPlayW + 4, 44, 60, _gbReady ? "9.2 ms spi" : "9.7 ms spi", c.textDim, ui::fontUiXSmall());
}

void GamesApp::buildPause() {
    const ui::AppColours c = surf();
    buildPlay();
    lv_obj_t* scrim = panel(_root, 0, 0, kCanvasW, kCanvasH, 0x000000, 0, 0, true);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, LV_PART_MAIN);

    // Only what the build can do. Save/load state, screenshot and palette were
    // placeholders with no implementation behind them.
    static const char* kItems[3] = { "Resume", "Restart cart", "Exit to list" };
    lv_obj_t* box = panel(_root, 65, 34, 190, 84, c.pane, c.row, 3, true);
    text(box, 7, 4, 176, "PAUSED", c.accent, ui::fontUiXSmall());
    for (int i = 0; i < 3; ++i) {
        const bool on = (_pauseIx == i);
        if (on) panel(box, 4, 18 + i * 17, 182, 15, c.rowFocus, 0, 2, true);
        text(box, 10, 18 + i * 17, 170, kItems[i], on ? c.title : c.textDim, ui::fontUiSmall());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Settings hub
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::buildSettings() {
    const ui::AppColours c = surf();
    char freeBuf[32];
    fmtBytes(freeBytes(), freeBuf, sizeof(freeBuf));
    header("SETTINGS - GAMES", freeBuf, c.accent);

    static const char* kLabels[4] = { "Wi-Fi", "Storage", "Keymap", "About" };
    char values[4][48];
    std::snprintf(values[0], sizeof(values[0]), "no connection");
#ifdef ARDUINO
    std::snprintf(values[1], sizeof(values[1]), "%s free", freeBuf);
#else
    std::snprintf(values[1], sizeof(values[1]), "host fs");
#endif
    std::snprintf(values[2], sizeof(values[2]), "50-key matrix");
    std::snprintf(values[3], sizeof(values[3]), "v0.1");

    for (int i = 0; i < 4; ++i) {
        const bool on = (_settingsIx == i);
        const int y = 16 + i * 24;
        if (on) panel(_root, 4, y, 312, 22, c.rowFocus, c.accent, 3, true);
        text(_root, 10, y + 3, 120, kLabels[i], on ? c.title : c.text, ui::fontUiSmall());
        text(_root, 150, y + 3, 160, values[i], c.textDim, ui::fontUiXSmall());
    }

    if (_aboutOpen) {
        panel(_root, 4, 112, 312, 30, c.pane, c.row, 3, true);
        text(_root, 9, 114, 302, "runtime: none (fake-08 not vendored)", c.textDim, ui::fontUiXSmall());
        char l2[64];
        std::snprintf(l2, sizeof(l2), "%d items - audio: none", static_cast<int>(_items.size()));
        text(_root, 9, 126, 302, l2, c.textDim, ui::fontUiXSmall());
    }

}

// ═══════════════════════════════════════════════════════════════════════════
// Wi-Fi (layout only in this build)
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::buildWifi() {
    const ui::AppColours c = surf();
    header("WI-FI - NO CONNECTION", "0 saved", c.textDim);

    panel(_root, 4, 16, 312, 30, c.pane, c.row, 3, true);
    text(_root, 12, 24, 150, "Web portal", c.text, ui::fontUiSmall());
    text(_root, 240, 24, 70, "OFF", c.textDim, ui::fontUiSmall());

    panel(_root, 4, 48, 312, 30, c.pane, c.row, 3, true);
    text(_root, 12, 56, 200, "no networks saved", c.textDim, ui::fontUiSmall());
    text(_root, 240, 56, 70, "use portal", c.textDim, ui::fontUiXSmall());


}

// ═══════════════════════════════════════════════════════════════════════════
// Manage storage
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::buildStorage() {
    const ui::AppColours c = surf();
    char freeBuf[32];
    fmtBytes(freeBytes(), freeBuf, sizeof(freeBuf));

    char right[48];
#ifdef ARDUINO
    std::snprintf(right, sizeof(right), "%s free of 2.94 MB", freeBuf);
#else
    std::snprintf(right, sizeof(right), "host fs - %d items", static_cast<int>(_items.size()));
#endif
    header("STORAGE", right, c.accent);

    std::vector<const Item*> sorted;
    sorted.reserve(_items.size());
    for (size_t i = 0; i < _items.size(); ++i) sorted.push_back(&_items[i]);
    std::sort(sorted.begin(), sorted.end(), [](const Item* a, const Item* b) {
        return a->bytes > b->bytes;
    });

    if (sorted.empty()) {
        text(_root, 8, 60, 300, "Nothing stored.", c.textDim, ui::fontUiSmall());
        return;
    }

    const int count = static_cast<int>(sorted.size());
    if (_storageIx >= count) _storageIx = count - 1;
    if (_storageIx < 0) _storageIx = 0;

    const int visible = 4;
    int start = _storageIx - 1;
    if (start > count - visible) start = count - visible;
    if (start < 0) start = 0;

    for (int i = 0; i < visible; ++i) {
        const int idx = start + i;
        if (idx >= count) break;
        const bool on = (idx == _storageIx);
        const bool armed = (idx == _storageArm);
        const Item* it = sorted[static_cast<size_t>(idx)];
        const int y = 18 + i * 26;

        if (on) panel(_root, 3, y, 314, 24, c.rowFocus, armed ? c.accent : 0, 2, true);
        text(_root, 8, y + 1, 200, it->name.c_str(), on ? c.title : c.text, ui::fontUiSmall());

        if (armed) {
            text(_root, 8, y + 12, 220, "ENTER again to delete", c.accent, ui::fontUiXSmall());
        } else {
            char meta[64];
            char sizeBuf[24];
            fmtBytes(it->bytes, sizeBuf, sizeof(sizeBuf));
            std::snprintf(meta, sizeof(meta), "%s - %s", sizeBuf, it->cat.c_str());
            text(_root, 8, y + 12, 220, meta, c.textDim, ui::fontUiXSmall());
        }
    }

}

// ═══════════════════════════════════════════════════════════════════════════
// Keymap
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::buildKeymap() {
    const ui::AppColours c = surf();
    header("PICO-8 KEYMAP - 50-KEY MATRIX", "", c.accent);

    static const char* kRows[6][8] = {
        { "SHIFT", "ALPHA", "MODE", "SETUP", "F1", "F2", "F3", "F4" },
        { "ON", "AC", "DEL", "EQ", "LEFT", "UP", "DOWN", "RIGHT" },
        { "X", "Y", "TABLE", "GRAPH", "ZOOM", "TRACE", "STEPS", "SOLVE" },
        { "7", "8", "9", "(", ")", "/", "^", "SQRT" },
        { "4", "5", "6", "*", "-", "sin", "cos", "tan" },
        { "1", "2", "3", "+", "+/-", "0", ".", "ENTER" },
    };
    static const char* kMapped[6][8] = {
        { "", "", "pause", "", "", "", "", "" },
        { "", "X", "O", "", "left", "up", "down", "right" },
        { "", "", "", "", "", "", "", "" },
        { "", "", "", "", "", "", "", "" },
        { "", "", "", "", "", "", "", "" },
        { "", "", "", "", "", "", "", "", },
    };

    for (int r = 0; r < 6; ++r) {
        for (int col = 0; col < 8; ++col) {
            const bool mapped = kMapped[r][col][0] != 0;
            const int x = 5 + col * 38;
            const int y = 18 + r * 20;
            panel(_root, x, y, 36, 18, mapped ? c.rowFocus : c.pane,
                  mapped ? c.accent : c.row, 2, true);
            text(_root, x + 2, y + 2, 32, kRows[r][col],
                 mapped ? c.title : c.textDim, ui::fontUiXSmall());
        }
    }

}

// ═══════════════════════════════════════════════════════════════════════════
// Input
// ═══════════════════════════════════════════════════════════════════════════

void GamesApp::handleKey(const KeyEvent& ev) {
    // While a cart runs, the pad drives the VM: PRESS/REPEAT set a bit, RELEASE
    // clears it, so a held d-pad stays held in the core.
    if (_screenKind == Screen::Play && _gbReady) {
        if (ev.code == KeyCode::EXE) {                 // back out, keep the save
            if (ev.action == KeyAction::PRESS) {
                saveGbRam(); _gb.unload(); _gbReady = false; _gbRow = -1;
                _screenKind = Screen::List; rebuild();
            }
            return;
        }
        GbButton b;
        bool mapped = true;
        switch (ev.code) {
            case KeyCode::LEFT:  b = GbButton::Left;   break;
            case KeyCode::RIGHT: b = GbButton::Right;  break;
            case KeyCode::UP:    b = GbButton::Up;     break;
            case KeyCode::DOWN:  b = GbButton::Down;   break;
            case KeyCode::DEL:   b = GbButton::A;      break;
            case KeyCode::AC:    b = GbButton::B;      break;
            case KeyCode::SHIFT: b = GbButton::Select; break;
            case KeyCode::ALPHA: b = GbButton::Start;  break;
            default: mapped = false; break;
        }
        if (mapped) {
            if (ev.action == KeyAction::RELEASE) _gb.setButton(b, false);
            else                                 _gb.setButton(b, true);
            return;
        }
    }
    if (_screenKind == Screen::Play && _p8.ready()) {
        uint8_t bit = 0;
        switch (ev.code) {
            case KeyCode::LEFT:  bit = 0x01; break;
            case KeyCode::RIGHT: bit = 0x02; break;
            case KeyCode::UP:    bit = 0x04; break;
            case KeyCode::DOWN:  bit = 0x08; break;
            case KeyCode::DEL:   bit = 0x10; break;   // O
            case KeyCode::AC:    bit = 0x20; break;   // X
            default: break;
        }
        if (bit) {
            if (ev.action == KeyAction::RELEASE) _p8buttons &= static_cast<uint8_t>(~bit);
            else                                  _p8buttons |= bit;
            _p8.setButtons(_p8buttons);
            return;
        }
    }
    if (ev.code == KeyCode::MODE) { _exit = true; return; }
    if (ev.action != KeyAction::PRESS && ev.action != KeyAction::REPEAT) return;

    const int count = static_cast<int>(_items.size());

    switch (_screenKind) {
        case Screen::Home:
            if (ev.code == KeyCode::LEFT)  { _tile = (_tile + 2) % 3; rebuild(); return; }
            if (ev.code == KeyCode::RIGHT) { _tile = (_tile + 1) % 3; rebuild(); return; }
            if (ev.code == KeyCode::ENTER || ev.code == KeyCode::EXE) {
                if (_tile == 1)      _screenKind = Screen::List;
                else if (_tile == 2) _screenKind = Screen::Settings;
                else                 _screenKind = Screen::Wifi;   // online gate
                rebuild();
                return;
            }
            if (ev.code == KeyCode::AC) { _exit = true; return; }
            return;

        case Screen::List:
            if (ev.code == KeyCode::UP && _row > 0) { --_row; rebuild(); return; }
            if (ev.code == KeyCode::DOWN && _row + 1 < count) { ++_row; rebuild(); return; }
            if (ev.code == KeyCode::F1) { _screenKind = Screen::Storage; _storageIx = _row; _storageArm = -1; rebuild(); return; }
            if ((ev.code == KeyCode::ENTER || ev.code == KeyCode::EXE) && count > 0) {
                _screenKind = Screen::Play; _frames = 0; rebuild(); return;
            }
            if (ev.code == KeyCode::AC || ev.code == KeyCode::BACK) { _screenKind = Screen::Home; rebuild(); return; }
            return;

        case Screen::Play:
            if (ev.code == KeyCode::ENTER || ev.code == KeyCode::EXE || ev.code == KeyCode::MODE) {
                _pauseIx = 0;
                _screenKind = Screen::Pause;
                rebuild();
                return;
            }
            if (ev.code == KeyCode::AC || ev.code == KeyCode::BACK) {
                stopCart(); _p8buttons = 0;
                _screenKind = Screen::List; rebuild(); return;
            }
            return;

        case Screen::Pause:
            if (ev.code == KeyCode::UP)   { _pauseIx = (_pauseIx + 2) % 3; rebuild(); return; }
            if (ev.code == KeyCode::DOWN) { _pauseIx = (_pauseIx + 1) % 3; rebuild(); return; }
            if (ev.code == KeyCode::ENTER || ev.code == KeyCode::EXE) {
                if (_pauseIx == 0)      { _screenKind = Screen::Play; rebuild(); }
                else if (_pauseIx == 1) { _p8.end(); _p8tried = false; _p8row = -1;
                                          _screenKind = Screen::Play; rebuild(); }
                else if (_pauseIx == 2) { _p8.end(); _p8tried = false; _p8row = -1;
                                          _screenKind = Screen::List; rebuild(); }
                return;
            }
            if (ev.code == KeyCode::AC || ev.code == KeyCode::BACK) { _screenKind = Screen::Play; rebuild(); return; }
            return;

        case Screen::Settings:
            if (ev.code == KeyCode::UP && _settingsIx > 0) { --_settingsIx; rebuild(); return; }
            if (ev.code == KeyCode::DOWN && _settingsIx < 3) { ++_settingsIx; rebuild(); return; }
            if (ev.code == KeyCode::ENTER || ev.code == KeyCode::EXE) {
                if (_settingsIx == 0)      { _screenKind = Screen::Wifi; rebuild(); }
                else if (_settingsIx == 1) { _screenKind = Screen::Storage; _storageIx = 0; _storageArm = -1; rebuild(); }
                else if (_settingsIx == 2) { _screenKind = Screen::Keymap; rebuild(); }
                else                       { _aboutOpen = !_aboutOpen; rebuild(); }
                return;
            }
            if (ev.code == KeyCode::AC || ev.code == KeyCode::BACK) { _screenKind = Screen::Home; rebuild(); return; }
            return;

        case Screen::Wifi:
            if (ev.code == KeyCode::AC || ev.code == KeyCode::EXE || ev.code == KeyCode::BACK) { _screenKind = Screen::Home; rebuild(); return; }
            return;

        case Screen::Storage: {
            const int n = count;
            if (ev.code == KeyCode::UP && _storageIx > 0) { --_storageIx; _storageArm = -1; rebuild(); return; }
            if (ev.code == KeyCode::DOWN && _storageIx + 1 < n) { ++_storageIx; _storageArm = -1; rebuild(); return; }
            if (ev.code == KeyCode::AC || ev.code == KeyCode::BACK) { _screenKind = Screen::Settings; _storageArm = -1; rebuild(); return; }
            if ((ev.code == KeyCode::ENTER || ev.code == KeyCode::EXE) && n > 0) {
                // The rows are sorted by size, the model is not: resolve the same way
                // buildStorage() does, or the delete hits the wrong file.
                std::vector<size_t> order(_items.size());
                for (size_t i = 0; i < order.size(); ++i) order[i] = i;
                std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
                    return _items[a].bytes > _items[b].bytes;
                });
                if (_storageIx < static_cast<int>(order.size())) {
                    const size_t target = order[static_cast<size_t>(_storageIx)];
                    if (_storageArm != _storageIx) {
                        _storageArm = _storageIx;
                        std::printf("[GAMES] arm delete: %s\n", _items[target].path.c_str());
                        rebuild();
                    } else {
                        const std::string path = _items[target].path;
                        LittleFS.remove(path.c_str());
                        std::printf("[GAMES] deleted %s\n", path.c_str());
                        _items.erase(_items.begin() + static_cast<long>(target));
                        _storageArm = -1;
                        if (_row >= static_cast<int>(_items.size()) && _row > 0) --_row;
                        rebuild();
                    }
                }
                return;
            }
            return;
        }

        case Screen::Keymap:
            _screenKind = Screen::Settings;
            rebuild();
            return;
    }
}
