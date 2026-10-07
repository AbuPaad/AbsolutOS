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
 * SettingsApp.h — Settings configuration panel for NumOS.
 *
 * LVGL-native app with clean NumWorks-inspired UI:
 *   - Angle mode toggle (Radians/Degrees) — writes the runtime source of
 *     truth (AngleModeRuntime.h)
 *   - Complex roots toggle (ON/OFF)
 *   - Decimal precision selector (6/8/10/12)
 *   - Step-by-step educational mode toggle (ON/OFF)
 *   - Brightness (production WROOM-1U target only)
 *   - Web portal toggle — raises the provisioning AP + file/config portal
 *     (net/Portal.h). This is the ONLY way a blank unit can ever get Wi-Fi
 *     credentials or an API key, so the row is always last and always present.
 *
 * Part of: NumOS — System Settings
 */

#pragma once

#include <lvgl.h>
#include "../Config.h"
#include "BrightnessSettingPolicy.h"
#include "../net/Wifi.h"
#include "../net/OtaUpdater.h"
#include "../ui/StatusBar.h"
#include "../input/KeyCodes.h"
#include "../input/KeyboardManager.h"

class DisplayDriver;

class SettingsApp {
public:
    explicit SettingsApp(DisplayDriver* display = nullptr);
    ~SettingsApp();

    void begin();
    void end();
    /** Commit the final visible brightness before a screen change. */
    void prepareToLeave();
    void load();
    void handleKey(const KeyEvent& ev);
    /**
     * Called once per main-loop pass while this app is active. The settings
     * screen is otherwise pure LVGL; this exists because the portal's state
     * changes without any input — the AP is raised asynchronously and the STA
     * association completes seconds later, so the row has to catch up on its own.
     */
    void update();

    bool isActive() const { return _screen != nullptr; }
    bool navigateBack() { return false; }

#if defined(__EMSCRIPTEN__) || NUMOS_BOARD_PROD_WROOM1U_N16R8
    /** Persist the compact settings record (LittleFS on hardware/IDBFS on web). */
    static bool loadPersistentState();
    static bool savePersistentState();
#endif

private:
    // Brightness keeps its slot on the production target; the Theme row slots
    // in after it and the Wi-Fi row is appended last on every target so
    // existing row indexes before it never shift.
    static constexpr int THEME_ROW =
        NUMOS_BOARD_PROD_WROOM1U_N16R8 ? 5 : 4;
    // A "System Update" row is appended AFTER Wi-Fi so Wi-Fi keeps its existing
    // index: closeWifiView() and the keymap both name WIFI_ROW explicitly.
    static constexpr int NUM_ITEMS =
        NUMOS_BOARD_PROD_WROOM1U_N16R8 ? 8 : 7;
    static constexpr int WIFI_ROW = NUM_ITEMS - 2;
    static constexpr int UPDATE_ROW = NUM_ITEMS - 1;
    static constexpr int SCREEN_W  = 320;
    static constexpr int SCREEN_H  = SCREEN_HEIGHT;  // canvas (Config.h)
    static constexpr int PAD       = 12;
    static constexpr int ROW_H     =
        NUMOS_BOARD_PROD_WROOM1U_N16R8 ? 34 : 40;
    static constexpr int ROW_GAP   = 2;
    // The hint strip is pinned to the screen, NOT inside the scrolling row
    // container: it carries the portal's AP name/password and browser URL while
    // the portal is up, and must stay readable no matter where the list is
    // scrolled. Sized for two lines of the 12 px font.
    static constexpr int HINT_H    = 32;

    /// The Wi-Fi screen: row 0 is the portal toggle (the only way to ADD a
    /// network — a 20-character password is not being typed on this keypad),
    /// rows 1..n are the saved networks in priority order.
    static constexpr int WIFI_ROWS_MAX = 1 + static_cast<int>(net::Wifi::kMaxNetworks);
    static constexpr int WIFI_LIST_H   = 30;   ///< row height on that screen

    /// The System Update screen: version, status, and two actions.
    static constexpr int UPDATE_ROWS_MAX = 4;
    static constexpr int UPDATE_LIST_H   = 32;

    enum class View { Main, Wifi, Update };

    lv_obj_t*       _screen;
    ui::StatusBar   _statusBar;

    // UI elements
    lv_obj_t*       _container;
    lv_obj_t*       _rows[NUM_ITEMS];
    lv_obj_t*       _labels[NUM_ITEMS];
    lv_obj_t*       _values[NUM_ITEMS];
    lv_obj_t*       _hintLabel;
    lv_obj_t*       _brightnessSlider;

    // Wi-Fi screen widgets (created on entry, destroyed on exit)
    View            _view = View::Main;
    lv_obj_t*       _wifiRows[WIFI_ROWS_MAX];
    lv_obj_t*       _wifiLabels[WIFI_ROWS_MAX];
    lv_obj_t*       _wifiValues[WIFI_ROWS_MAX];
    int             _wifiRowCount = 0;
    uint32_t        _lastPollMs   = 0;

    // System Update screen widgets (created on entry, destroyed on exit)
    lv_obj_t*       _updateRows[UPDATE_ROWS_MAX];
    lv_obj_t*       _updateLabels[UPDATE_ROWS_MAX];
    lv_obj_t*       _updateValues[UPDATE_ROWS_MAX];
    int             _updateRowCount = 0;

    int             _focus;
    DisplayDriver*  _display;
    numos::settings::BrightnessSettingSession _brightnessSession;

    void createUI();
    void createRows();          ///< (re)build the Settings rows in _container
    void updateFocus();
    void updateValues();
    void toggleCurrent();
    void adjustBrightness(int delta);
    void refreshHint();         ///< nav hint, or the portal's AP/URL block

    // ── Wi-Fi screen ────────────────────────────────────────────────────────
    void buildWifiView();       ///< enter: tears the Settings rows down
    void closeWifiView();       ///< leave: rebuilds them
    void refreshWifiView();     ///< statuses + focus highlight
    void updateWifiFocus();
    void wifiActivate(int row); ///< connect / promote the selected network
    void wifiForget(int row);

    // ── System Update screen ────────────────────────────────────────────────
    void buildUpdateView();     ///< enter: tears the Settings rows down
    void closeUpdateView();     ///< leave: rebuilds them
    void refreshUpdateView();   ///< status + focus highlight
    void updateUpdateFocus();
};
