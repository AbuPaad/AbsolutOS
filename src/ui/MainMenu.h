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
 * MainMenu.h
 * Launcher NumOS — Pixel-Perfect NumWorks aesthetic (High-Fidelity).
 *
 * LVGL 9.x implementation:
 *  - 24 px orange status bar (#FF9900) with rad/deg, title, battery
 *  - 3-column grid of white cards with geometric vector-style icons
 *  - Focus: blue border (#0074D9) + 1.05× scale + overshoot animation
 *  - Shadows: diffuse 4 px normal, intensified on focus
 *  - Dot-grid background pattern (#D1D1D1 on #F5F5F5)
 *  - Anti-aliased UI default font (Montserrat)
 */

#pragma once

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include "hal/ArduinoCompat.h"
#endif
#include <lvgl.h>
#include <functional>

class DisplayDriver;

class MainMenu {
public:
    explicit MainMenu(DisplayDriver& display);
    ~MainMenu();

    // ── Lifecycle ────────────────────────────────────────────────────────
    void create();
    void load();

    // ── Input ────────────────────────────────────────────────────────────
    void setLaunchCallback(std::function<void(int)> cb);
    bool moveFocusByDelta(int dCol, int dRow);

    /// True when the active interaction profile renders the launcher as the
    /// casio MenuList (4×2 numbered pages) instead of the numos card grid.
    bool isListMode() const { return _listMode; }

    /// List mode: launch the focused slot's app (positional N:LABEL → APPS[] id).
    bool launchFocusedSlot();

    /// List mode: launch a POSITIONAL slot (0-based) on the current page. This is
    /// the Casio primary path: digit keys launch directly, no focus walk (SPEC-stageC
    /// §C2/C6). Resolves the id through generated kCasioSlots, never a name list.
    bool launchSlot(int slot);

    // ── Accessors ────────────────────────────────────────────────────────
    lv_group_t* group()  const { return _group; }
    lv_obj_t*   screen() const { return _screen; }

#ifdef NATIVE_SIM
    // ── Native-only read-only debug accessors (Phase 9B menu-nav parity) ──
    // Compiled ONLY into the SDL2 emulator: NATIVE_SIM is defined exclusively by
    // [env:emulator_pc] (platformio.ini), so the firmware preprocessor strips this
    // whole block — the device build, its symbols, and its binary are unchanged.
    // They expose just enough of the focus model for a `.numos` `assert_menu_focus`
    // check WITHOUT reading pixels or leaking internal LVGL pointers. Strictly
    // read-only: there is deliberately no mutation surface here.
    //
    //   * debugFocusedCardId()    — id of the currently focused card, or -1 if none
    //                               (thin wrapper over the private focusedCardId()).
    //   * debugResolveCardToken() — map a script token (a card NAME, case- and
    //                               space-insensitive, OR a non-negative decimal id)
    //                               to a card id in [0, APP_COUNT); -1 if unknown or
    //                               out of range. The real APPS[] table is the single
    //                               source of truth, so names never drift.
    //   * debugCardNameById()     — canonical card name for an id, or nullptr if the
    //                               id is out of range (for friendly diagnostics).
    //   * debugLauncherPage()     — casio list launcher page (1-based), or 1 when the
    //                               grid is active (assert_launcher_page reads this).
    int                debugFocusedCardId() const { return focusedCardId(); }
    bool               debugFocusedCardCenter(int& x, int& y) const;
    static int         debugResolveCardToken(const char* token);
    static const char* debugCardNameById(int id);
    int                debugLauncherPage() const { return _listPage + 1; }
    int                debugFocusedListSlot() const { return _listMode ? _listSlot : -1; }
#endif

private:
    // ── App descriptor ───────────────────────────────────────────────────
    struct AppEntry {
        int         id;
        const char* name;
        uint32_t    color;       // Primary icon colour
        uint32_t    colorLight;  // Lighter accent for geometric decoration
    };

    static const AppEntry APPS[];
    static const int      APP_COUNT;

    // ── UI construction ──────────────────────────────────────────────────
    void buildStatusBar();
    void buildGrid();
    lv_obj_t* buildCard(lv_obj_t* parent, const AppEntry& app);
    lv_obj_t* cardById(int appId) const;
    int focusedCardId() const;

    // ── Casio MenuList (4×2 numbered pages; ThemeManager interaction) ────
    void buildMenuList();       ///< full-bleed 4×2 N:NAME list (no status bar)
    void rebuildListPage();     ///< (re)fill slot labels + edge arrows for _listPage
    void updateListFocus();     ///< move the focus box to the focused slot
    void setListPage(int page); ///< clamp + rebuild + keep slot valid
    void moveListRow(int dRow); ///< UP/DOWN: adjacent row, no wrap
    static int menuListSlotsOnPage(int page);  ///< p1/p2=8, p3=6
    static int menuListPageCount();            ///< ceil(APP_COUNT/8) = 3

    /// Creates a geometric vector icon inside `parent` based on app id.
    void createAppIcon(lv_obj_t* parent, const AppEntry& app);

    // Flex layout: col/row params no longer required (dynamic wrapping)

    // ── Styles ───────────────────────────────────────────────────────────
    void initStyles();

    lv_style_t _styleScreen;
    lv_style_t _styleCard;
    lv_style_t _styleCardFocused;
    lv_style_t _styleCardPressed;
    lv_style_t _styleIconBox;
    lv_style_t _styleAppName;

    // Focus transition (overshoot, 250 ms)
    lv_style_transition_dsc_t _transCard;

    // ── Event handlers ───────────────────────────────────────────────────
    static void onCardEvent(lv_event_t* e);
    static void onCardFocused(lv_event_t* e);   ///< Logs focus changes to Serial
    static void onGridDraw(lv_event_t* e);
    static void onIconDraw(lv_event_t* e);
    /** Disables shadow rendering on all cards+icons during scroll for FPS. */
    static void onScrollBegin(lv_event_t* e);
    /** Restores shadow rendering after scroll ends. */
    static void onScrollEnd(lv_event_t* e);

    // ── Members ──────────────────────────────────────────────────────────
    DisplayDriver& _display;

    // Casio MenuList state (page-turn launcher; interaction-driven, never the
    // theme id — doc 12). _listPage is 0-based; _listSlot is the focused slot
    // in [0 .. slotsOnPage), positional label = slot+1.
    bool        _listMode   = false;
    int         _listPage   = 0;
    int         _listSlot   = 0;
    lv_obj_t*   _listSlots[8] = {};      ///< N:NAME labels, slot 0..7
    lv_obj_t*   _listArrowLeft  = nullptr;
    lv_obj_t*   _listArrowRight = nullptr;

    lv_obj_t*   _screen    = nullptr;
    lv_obj_t*   _grid      = nullptr;
    lv_obj_t*   _firstCard = nullptr;   ///< First card — focused on create()
    lv_group_t* _group     = nullptr;

    /// ThemeId the current widgets were built with (0xFF = nothing built yet).
    /// create() latches the launcher's topology and every card's colours, so
    /// load() rebuilds when the active theme has moved on — otherwise a theme
    /// change made while an app was on screen leaves a stale launcher behind.
    uint8_t     _builtThemeId = 0xFF;

    std::function<void(int)> _launchCb;
};
