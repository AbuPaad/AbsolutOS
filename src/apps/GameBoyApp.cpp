/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * GameBoyApp.cpp — Game Boy / Game Boy Color front-end. See GameBoyApp.h.
 *
 * Rendering: the core hands us one RGB565 line at a time (its lcd callback
 * fills GameBoyCore's framebuffer); we publish that buffer to LVGL as a raw
 * RGB565 lv_image_dsc_t and invalidate the image once per emulated frame. This
 * is the same mechanism GrapherApp/FractalApp use for their canvases — no
 * lv_canvas, no per-pixel LVGL drawing, no PSRAM staging buffer of our own.
 */

#include "apps/GameBoyApp.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifndef ARDUINO
  #include <cstdio>
  #include "hal/FileSystem.h"
#else
  #include <LittleFS.h>
#endif

#ifdef ARDUINO
  #define GBLOG(...) Serial.printf(__VA_ARGS__)
#else
  #define GBLOG(...) std::printf(__VA_ARGS__)
#endif

using numos::emulation::GameBoyCore;
using numos::emulation::GbButton;

namespace {

// ── Palette for the app chrome (the emulated picture has its own palettes) ──
constexpr uint32_t COL_BG       = 0x000000;   // letterbox around the image
constexpr uint32_t COL_TEXT     = 0xFFFFFF;
constexpr uint32_t COL_DIM      = 0x9E9E9E;
constexpr uint32_t COL_ROW      = 0x1C1C1C;
constexpr uint32_t COL_ROW_SEL  = 0x1565C0;   // same blue as launcher focus
constexpr int      ROW_H        = 26;
constexpr int      PAD          = 6;

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string baseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

bool hasRomExtension(const std::string& name) {
    const std::string lc = toLower(name);
    return (lc.size() > 4 && lc.compare(lc.size() - 4, 4, ".gbc") == 0) ||
           (lc.size() > 3 && lc.compare(lc.size() - 3, 3, ".gb") == 0);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

GameBoyApp::GameBoyApp() {}

GameBoyApp::~GameBoyApp() {
    end();
}

void GameBoyApp::begin() {
    _screen = lv_obj_create(nullptr);           // own screen, no parent
    lv_obj_set_style_bg_color(_screen, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
}

void GameBoyApp::load() {
    if (!_screen) begin();

    _exitRequested = false;
    _state = State::Picker;

    rescanRoms();
    buildPicker();
    lv_screen_load(_screen);

    GBLOG("[GB] load: %d rom(s) in %s\n", static_cast<int>(_roms.size()), kRomDir);
}

void GameBoyApp::end() {
    saveCartRam();
    unloadRom();

    destroyPicker();
    destroyPlayer();

    if (_screen) {
        lv_obj_delete(_screen);
        _screen = nullptr;
    }
    _state = State::Idle;
    _roms.clear();
    _selected = 0;
    _exitRequested = false;
}

bool GameBoyApp::isActive() const { return _state == State::Playing; }

uint32_t GameBoyApp::framesRun() const { return _core.framesRun(); }

bool GameBoyApp::consumeExitRequest() {
    const bool requested = _exitRequested;
    _exitRequested = false;
    return requested;
}

// ═══════════════════════════════════════════════════════════════════════════
// ROM discovery — the scan the emulated-filesystem listing API exists for
// ═══════════════════════════════════════════════════════════════════════════

void GameBoyApp::rescanRoms() {
    _roms.clear();

#ifndef ARDUINO
    // PC: make sure the drop folder exists so the docs' instruction works and
    // the app can say something useful instead of failing silently.
    if (!LittleFS.isDirectory(kRomDir)) LittleFS.mkdir(kRomDir);
#endif

    File dir = LittleFS.open(kRomDir, "r");
    if (!dir || !dir.isDirectory()) {
        GBLOG("[GB] cannot open %s\n", kRomDir);
        _state = State::Error;
        return;
    }

    while (File entry = dir.openNextFile()) {
        if (entry.isDirectory()) continue;
        const std::string name = entry.name();
        if (!hasRomExtension(name)) continue;
        _roms.push_back(name);
    }

    // Deterministic order: scripts and goldens must not depend on readdir order.
    std::sort(_roms.begin(), _roms.end());

    _selected = 0;
    _state = _roms.empty() ? State::Error : State::Picker;
    GBLOG("[GB] scan: %d rom(s)\n", static_cast<int>(_roms.size()));
}

// ═══════════════════════════════════════════════════════════════════════════
// Picker UI
// ═══════════════════════════════════════════════════════════════════════════

void GameBoyApp::buildPicker() {
    destroyPicker();
    if (!_screen) return;

    _title = lv_label_create(_screen);
    lv_label_set_text(_title, "Game Boy");
    lv_obj_set_style_text_color(_title, lv_color_hex(COL_TEXT), LV_PART_MAIN);
    lv_obj_align(_title, LV_ALIGN_TOP_LEFT, PAD, PAD);

    _message = lv_label_create(_screen);
    lv_obj_set_style_text_color(_message, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_align(_message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(_message, lv_pct(100));
    lv_obj_align(_message, LV_ALIGN_BOTTOM_MID, 0, -PAD);

    _picker = lv_obj_create(_screen);
    lv_obj_set_size(_picker, lv_pct(100), kScreenH - 2 * PAD - 24);
    lv_obj_align(_picker, LV_ALIGN_TOP_LEFT, 0, PAD + 22);
    lv_obj_set_style_bg_opa(_picker, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_picker, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_picker, PAD, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_picker, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(_picker, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(_picker, LV_SCROLLBAR_MODE_AUTO);

    if (_roms.empty()) {
        setMessage("No ROMs.\nDrop .gb / .gbc files in /roms, then reopen.");
        return;
    }

    _rows.clear();
    _rowLabels.clear();
    for (size_t i = 0; i < _roms.size(); ++i) {
        lv_obj_t* row = lv_obj_create(_picker);
        lv_obj_set_size(row, lv_pct(100), ROW_H);
        lv_obj_set_style_radius(row, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_left(row, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);   // keyboard-driven only

        lv_obj_t* label = lv_label_create(row);
        lv_label_set_text(label, _roms[i].c_str());
        lv_obj_set_style_text_color(label, lv_color_hex(COL_TEXT), LV_PART_MAIN);
        lv_obj_center(label);

        _rows.push_back(row);
        _rowLabels.push_back(label);
    }

    setMessage("ENTER play · MODE exit");
    refreshSelectionUi();
}

void GameBoyApp::destroyPicker() {
    if (_picker) {
        lv_obj_delete(_picker);
        _picker = nullptr;
    }
    if (_title) {
        lv_obj_delete(_title);
        _title = nullptr;
    }
    if (_message) {
        lv_obj_delete(_message);
        _message = nullptr;
    }
    _rows.clear();
    _rowLabels.clear();
}

void GameBoyApp::refreshSelectionUi() {
    for (size_t i = 0; i < _rows.size(); ++i) {
        const bool selected = (static_cast<int>(i) == _selected);
        lv_obj_set_style_bg_opa(_rows[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN);
        lv_obj_set_style_bg_color(_rows[i], lv_color_hex(COL_ROW_SEL), LV_PART_MAIN);
        if (!selected) {
            lv_obj_set_style_bg_color(_rows[i], lv_color_hex(COL_ROW), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(_rows[i], LV_OPA_50, LV_PART_MAIN);
        }
    }
    if (_selected >= 0 && _selected < static_cast<int>(_rows.size())) {
        // Keep the highlighted row visible when the list is longer than the box.
        lv_obj_scroll_to_view(_rows[_selected], LV_ANIM_OFF);
    }
}

void GameBoyApp::setMessage(const char* text) {
    if (_message) lv_label_set_text(_message, text ? text : "");
}

// ═══════════════════════════════════════════════════════════════════════════
// Player UI + emulation
// ═══════════════════════════════════════════════════════════════════════════

void GameBoyApp::buildPlayer() {
    destroyPlayer();
    if (!_screen) return;

    _player = lv_obj_create(_screen);
    lv_obj_set_size(_player, lv_pct(100), lv_pct(100));
    lv_obj_center(_player);
    lv_obj_set_style_bg_color(_player, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_player, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_player, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_player, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(_player, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(_player, LV_OBJ_FLAG_SCROLLABLE);

    _image = lv_image_create(_player);
    lv_image_set_scale(_image, kImageScale);
    lv_obj_center(_image);

    publishFrame();
}

void GameBoyApp::destroyPlayer() {
    if (_player) {
        lv_obj_delete(_player);
        _player = nullptr;
    }
    _image = nullptr;
    _imgDsc.data = nullptr;
}

/** Point the LVGL image descriptor at the core's framebuffer. */
void GameBoyApp::publishFrame() {
    const uint16_t* fb = _core.framebuffer();
    if (!fb) return;

    _imgDsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    _imgDsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    _imgDsc.header.flags  = 0;
    _imgDsc.header.w      = GameBoyCore::kWidth;
    _imgDsc.header.h      = GameBoyCore::kHeight;
    _imgDsc.header.stride = GameBoyCore::kWidth * 2;
    _imgDsc.data_size     = static_cast<uint32_t>(GameBoyCore::kWidth) *
                            static_cast<uint32_t>(GameBoyCore::kHeight) * 2u;
    _imgDsc.data          = reinterpret_cast<const uint8_t*>(fb);

    if (_image) {
        lv_image_set_src(_image, &_imgDsc);
        lv_obj_invalidate(_image);
    }
}

void GameBoyApp::update() {
    if (_state != State::Playing || !_core.ready()) return;

#ifdef ARDUINO
    // Device pacing: the main loop is not VSYNC-pinned, so step as many frames
    // as real elapsed time allows. A capped catch-up stops a stall (SD read,
    // Wi-Fi task) from turning into an unbounded emulation burst.
    const uint32_t now = micros();
    if (_lastTickUs == 0) _lastTickUs = now;
    _frameAccumUs += now - _lastTickUs;
    _lastTickUs = now;

    uint32_t frames = _frameAccumUs / kFrameUs;
    if (frames > kMaxCatchUpFrames) {
        frames = kMaxCatchUpFrames;
        _frameAccumUs = 0;   // drop the backlog rather than play catch-up forever
    } else {
        _frameAccumUs -= frames * kFrameUs;
    }
    for (uint32_t i = 0; i < frames; ++i) _core.stepFrame();
#else
    // One emulated frame per main-loop iteration (PC pacing — see the header).
    _core.stepFrame();
#endif

    publishFrame();
}

bool GameBoyApp::loadSelectedRom() {
    if (_selected < 0 || _selected >= static_cast<int>(_roms.size())) return false;

    const std::string path = std::string(kRomDir) + "/" + _roms[_selected];
    File f = LittleFS.open(path.c_str(), "r");
    if (!f) {
        setMessage("Cannot open ROM");
        _state = State::Error;
        return false;
    }

    const size_t size = f.size();
    if (size < kMinRomBytes || size > kMaxRomBytes) {
        GBLOG("[GB] rejected %s: %u bytes\n", path.c_str(), static_cast<unsigned>(size));
        f.close();
        setMessage("ROM size not supported");
        _state = State::Error;
        return false;
    }

    std::vector<uint8_t> rom(size);
    const size_t got = f.read(rom.data(), size);
    f.close();
    if (got != size) {
        setMessage("ROM read failed");
        _state = State::Error;
        return false;
    }

    if (!_core.loadRom(rom.data(), rom.size())) {
        GBLOG("[GB] core refused %s (err=%d)\n", path.c_str(), _core.lastInitError());
        setMessage(GameBoyCore::initErrorText(_core.lastInitError()));
        _state = State::Error;
        return false;
    }

    _romBaseName = _roms[_selected];
    _romPath = path;
    loadCartRam();

    _frameAccumUs = 0;
    _lastTickUs   = 0;
    _state = State::Playing;
    buildPlayer();

    GBLOG("[GB] playing '%s' (%u bytes, cgb=%d, save=%u B)\n",
          _core.title(), static_cast<unsigned>(size), _core.isCgb() ? 1 : 0,
          static_cast<unsigned>(_core.saveSize()));
    return true;
}

void GameBoyApp::unloadRom() {
    if (_core.ready()) _core.unload();
    _romBaseName.clear();
    _romPath.clear();
}

void GameBoyApp::leaveGame() {
    if (_state != State::Playing) return;
    saveCartRam();
    unloadRom();
    destroyPlayer();
    _state = State::Picker;
    buildPicker();
    lv_screen_load(_screen);
}

// ═══════════════════════════════════════════════════════════════════════════
// Save data — /roms/<rom>.sav, the same convention as desktop emulators
// ═══════════════════════════════════════════════════════════════════════════

void GameBoyApp::saveCartRam() {
    if (!_core.ready() || !_core.saveDirty() || _core.saveSize() == 0) return;
    const uint8_t* data = _core.cartRam();
    if (!data) return;

    const std::string sav = _romPath + ".sav";
    File f = LittleFS.open(sav.c_str(), "w");
    if (!f) {
        GBLOG("[GB] could not write %s\n", sav.c_str());
        return;
    }
    const size_t written = f.write(data, _core.saveSize());
    f.close();   // the native wrapper notifies the WASM persistence layer here
    if (written == _core.saveSize()) {
        _core.clearSaveDirty();
        GBLOG("[GB] saved %u B to %s\n", static_cast<unsigned>(written), sav.c_str());
    } else {
        GBLOG("[GB] short save write (%u/%u)\n", static_cast<unsigned>(written),
              static_cast<unsigned>(_core.saveSize()));
    }
}

void GameBoyApp::loadCartRam() {
    if (!_core.ready() || _core.saveSize() == 0) return;

    const std::string sav = _romPath + ".sav";
    File f = LittleFS.open(sav.c_str(), "r");
    if (!f) return;   // no save yet — the cartridge starts fresh

    const size_t size = f.size();
    if (size != _core.saveSize()) {
        GBLOG("[GB] ignoring %s: %u B, expected %u B\n", sav.c_str(),
              static_cast<unsigned>(size), static_cast<unsigned>(_core.saveSize()));
        f.close();
        return;
    }

    std::vector<uint8_t> buf(size);
    const size_t got = f.read(buf.data(), size);
    f.close();
    if (got == size && _core.loadCartRam(buf.data(), buf.size())) {
        GBLOG("[GB] loaded %u B from %s\n", static_cast<unsigned>(size), sav.c_str());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Input
// ═══════════════════════════════════════════════════════════════════════════

void GameBoyApp::handleKey(const KeyEvent& ev) {
    // MODE is normally swallowed by SystemApp; treat it as an exit here too so
    // the app is safe when driven directly (e.g. by a test harness).
    if (ev.code == KeyCode::MODE) {
        _exitRequested = true;
        return;
    }

    if (_state == State::Picker) {
        if (ev.action != KeyAction::PRESS && ev.action != KeyAction::REPEAT) return;
        switch (ev.code) {
            case KeyCode::UP:
                if (_selected > 0) { --_selected; refreshSelectionUi(); }
                return;
            case KeyCode::DOWN:
                if (_selected + 1 < static_cast<int>(_roms.size())) {
                    ++_selected;
                    refreshSelectionUi();
                }
                return;
            case KeyCode::ENTER:
                loadSelectedRom();
                return;
            case KeyCode::AC:
                _exitRequested = true;
                return;
            default:
                return;
        }
    }

    if (_state == State::Error) {
        // A refused/broken ROM must not trap the user: AC leaves, ENTER rescans.
        if (ev.action != KeyAction::PRESS) return;
        if (ev.code == KeyCode::AC)     _exitRequested = true;
        if (ev.code == KeyCode::ENTER)  { _state = State::Picker; rescanRoms();
                                          buildPicker(); lv_screen_load(_screen); }
        return;
    }

    if (_state != State::Playing) return;

    // AC backs out of the running game to the ROM list (the game keeps its save).
    if (ev.code == KeyCode::AC) {
        if (ev.action == KeyAction::PRESS) leaveGame();
        return;
    }

    GbButton button;
    switch (ev.code) {
        case KeyCode::LEFT:  button = GbButton::Left;   break;
        case KeyCode::RIGHT: button = GbButton::Right;  break;
        case KeyCode::UP:    button = GbButton::Up;     break;
        case KeyCode::DOWN:  button = GbButton::Down;   break;
        case KeyCode::ENTER: button = GbButton::A;      break;
        case KeyCode::DEL:   button = GbButton::B;      break;
        case KeyCode::SHIFT: button = GbButton::Start;  break;
        case KeyCode::ALPHA: button = GbButton::Select; break;
        default: return;   // every other key is not a Game Boy button
    }

    // Releases matter: a held D-pad/button must stay held in the core.
    switch (ev.action) {
        case KeyAction::PRESS:
        case KeyAction::REPEAT:  _core.setButton(button, true);  break;
        case KeyAction::RELEASE: _core.setButton(button, false); break;
        default: break;
    }
}
