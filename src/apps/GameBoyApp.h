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
 * GameBoyApp.h — Game Boy / Game Boy Color front-end (NumOS app).
 *
 * Two states:
 *   Picker  — lists the .gb/.gbc files found in /roms (scanned on load()).
 *   Playing — one emulated frame per update() tick, blitted as one RGB565
 *             lv_image centred on the screen.
 *
 * Input (PC keyboard via the SDL2 emulator, mapped in NativeHal.cpp):
 *   arrows = D-pad · ENTER = A · DEL = B · SHIFT = Start · ALPHA = Select
 *   AC     = back to the picker while playing; leaves the app from the picker
 *   MODE   = leaves the app (SystemApp intercepts it before the app sees it)
 *
 * Cartridge RAM is persisted to "/roms/<name>.sav" on leaving a game, and read
 * back on load, so battery-backed saves survive across runs.
 *
 * v1 scope (deliberate): no audio, no ROM browser chrome beyond the picker, no
 * save states, no fast-forward, no FPS counter on screen. Frame pacing is one
 * emulated frame per main-loop iteration — correct on the desktop emulator,
 * whose loop runs well above 59.7 Hz; the device build will need a real
 * accumulator (see the plan reference in the skill).
 */

#pragma once

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include "hal/ArduinoCompat.h"
#endif
#include <lvgl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "emulation/GameBoyCore.h"
#include "input/KeyCodes.h"

class GameBoyApp {
public:
    GameBoyApp();
    ~GameBoyApp();

    // Non-copyable: owns an LVGL screen tree and an emulator core.
    GameBoyApp(const GameBoyApp&) = delete;
    GameBoyApp& operator=(const GameBoyApp&) = delete;

    // ── Lifecycle (called by SystemApp / NativeHal) ──────────────────────
    void load();     ///< create the screen, rescan /roms, show the picker
    void end();      ///< persist the save, destroy the screen tree, free the ROM
    void update();   ///< per-frame tick; only steps the core while playing

    // ── Input ────────────────────────────────────────────────────────────
    void handleKey(const KeyEvent& ev);

    /// True when the app wants to go back to the launcher (AC in the picker).
    bool consumeExitRequest();

    /// True only while a ROM is actually running — gates the per-frame tick.
    bool isActive() const;

    /// Number of emulated frames run since the current ROM was loaded (tests).
    uint32_t framesRun() const;

private:
    enum class State : uint8_t { Idle, Picker, Playing, Error };

    /** Largest ROM image accepted (MBC5 tops out at 8 MB). */
    static constexpr size_t kMaxRomBytes = 8u * 1024u * 1024u;
    /** Minimum: a cartridge header must fit. */
    static constexpr size_t kMinRomBytes = 0x150u;

    /**
     * Image zoom in LVGL's 1/256 units. 256 = 1×: the Game Boy's 160×144 sits
     * centred and pixel-exact inside the 320×240 logical surface. The documented
     * alternative is 384 (1.5× = 240×216) — a display-level toggle, NOT a
     * different surface size.
     */
    static constexpr int kImageScale = 256;

    static constexpr const char* kRomDir = "/roms";

    void begin();                 ///< build the root screen (once)
    void buildPicker();           ///< build the ROM list UI
    void destroyPicker();
    void buildPlayer();           ///< build the game surface UI
    void destroyPlayer();
    void rescanRoms();            ///< fill _roms from kRomDir
    void refreshSelectionUi();
    void setMessage(const char* text);

    /** Publish the core's framebuffer to the LVGL image descriptor. */
    void publishFrame();
    bool loadSelectedRom();
    void unloadRom();
    void leaveGame();             ///< persist + back to the picker
    void saveCartRam();
    void loadCartRam();

    // ── LVGL objects ─────────────────────────────────────────────────────
    lv_obj_t* _screen  = nullptr;   ///< root screen, owned by this app
    lv_obj_t* _picker  = nullptr;   ///< picker container
    lv_obj_t* _title   = nullptr;
    lv_obj_t* _message = nullptr;
    lv_obj_t* _player  = nullptr;   ///< game container
    lv_obj_t* _image   = nullptr;   ///< the 160×144 surface
    std::vector<lv_obj_t*> _rows;   ///< picker rows (index == _roms index)
    std::vector<lv_obj_t*> _rowLabels;

    // ── State ────────────────────────────────────────────────────────────
    State                    _state = State::Idle;
    std::vector<std::string> _roms;      ///< base names, sorted, from kRomDir
    int                      _selected = 0;
    bool                     _exitRequested = false;
    lv_image_dsc_t           _imgDsc{};
    std::string              _romBaseName;  ///< base name of the running ROM
    std::string              _romPath;      ///< "/roms/<name>" (the .sav prefix)
    numos::emulation::GameBoyCore _core;
};
