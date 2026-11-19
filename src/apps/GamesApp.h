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
 * GamesApp.h — Games front-end (LVGL-native).
 *
 * Scope of this build (G0 frontend foundation):
 *   Home (Online | Local | Settings) → List (/roms) → Play → Pause, plus the
 *   Settings hub, the Wi-Fi view and Manage storage.
 *
 * What is real here: the /roms scan, the row model, the layout, the key map,
 * the storage accounting and the delete path. What is not: cart execution
 * (fake-08 is not vendored yet) and the online feed (no TLS path in this app).
 * Both are visible in the UI as explicit states, never as fake data.
 *
 * Geometry: 320×156 canvas. Play area 156×156 at (82,0), which is a 128×128
 * cart frame scaled 39/32 (LVGL scale 312/256).
 *
 * Part of: NumOS — Games
 */

#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../input/KeyCodes.h"
#include "../pico8/Pico8Runtime.h"
#include "../emulation/GameBoyCore.h"

class GamesApp {
public:
    GamesApp();
    ~GamesApp();

    void load();                      ///< build the screen tree, scan /roms
    void end();                       ///< destroy the tree
    void update();                    ///< per-loop tick: frame meter while playing
    void handleKey(const KeyEvent& ev);
    bool consumeExitRequest();
    bool isActive() const { return _scr != nullptr; }

private:
    enum class Screen : uint8_t {
        Home, List, Play, Pause, Settings, Wifi, Storage, Keymap
    };

    struct Item {
        std::string name;
        std::string cat;        ///< "PICO-8 · arcade" | "Game Boy"
        std::string path;       ///< "/roms/pico8/x.p8.png"
        uint32_t    bytes = 0;
        bool        gb    = false;
    };

    // ── construction ──
    void buildFrame();                 // screen + root container
    void rebuild();                    // clear root, then the current screen
    void buildHome();
    void buildList();
    void buildPlay();
    void buildPause();
    void buildSettings();
    void buildWifi();
    void buildStorage();
    void buildKeymap();

    // ── helpers ──
    lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t fill,
                    uint32_t border, int radius = 0, bool filled = true);
    lv_obj_t* text(lv_obj_t* parent, int x, int y, int w, const char* s,
                   uint32_t colour, const lv_font_t* font);
    lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* src, int size, uint32_t colour);
    void      header(const char* left, const char* right, uint32_t leftColour);

    // ── data ──
    void scan();
    void scanDir(const char* dir, bool gb);
    uint32_t freeBytes() const;

    // ── state ──
    Screen  _screenKind = Screen::Home;
    int     _tile       = 1;           // 0 online, 1 local, 2 settings
    int     _row        = 0;
    int     _settingsIx = 0;
    int     _storageIx  = 0;
    int     _storageArm = -1;
    int     _pauseIx    = 0;
    bool    _exit       = false;
    bool    _aboutOpen  = false;
    bool    _onlineConnected = false;
    uint32_t _frames    = 0;
    uint32_t _fps       = 0;
    uint32_t _lastFpsMs = 0;

    std::vector<Item> _items;

    lv_obj_t* _scr  = nullptr;
    lv_obj_t* _root = nullptr;
    lv_obj_t* _fpsLabel = nullptr;

    // PICO-8 runtime for the Play screen (fake-08). One cart at a time.
    Pico8Runtime _p8;
    lv_obj_t*    _p8img = nullptr;
    lv_image_dsc_t _p8dsc{};
    bool         _p8tried = false;
    numos::emulation::GameBoyCore _gb;
    lv_image_dsc_t _gbDsc{};
    bool         _gbReady = false;
    int          _gbRow = -1;
    std::string  _gbPath;
    void         stopCart();
    void         saveGbRam();
    int          _p8row = -1;
    uint8_t      _p8buttons = 0;
    void         startCartIfNeeded();
    void         publishFrame();
};
