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
 * SettingsApp.cpp — Settings configuration panel for NumOS.
 *
 * Clean NumWorks-inspired settings UI with toggle rows.
 *
 * Part of: NumOS — System Settings
 */

#include "SettingsApp.h"
#include "../Config.h"
#include "../display/DisplayDriver.h"
#include "../math/AngleModeRuntime.h"
#include "../net/Portal.h"

#include <cstdio>

#if NUMOS_BOARD_PROD_WROOM1U_N16R8
#if NUMOS_PRODUCTION_DEMO_PROFILE
#include "../demo/DemoBootHealth.h"
#endif
#include "../demo/DemoSettingsRecord.h"
#include <FS.h>
#include <LittleFS.h>
#include <cstdint>
#include <cstring>
#elif defined(__EMSCRIPTEN__)
#include "../hal/FileSystem.h"
#include <cstdint>
#include <cstring>
#endif

using namespace vpam;

// ══ Color palette (matches system theme) ═════════════════════════════
static constexpr uint32_t COL_BG         = 0xFFFFFF;
static constexpr uint32_t COL_ROW_BG     = 0xF5F5F5;
static constexpr uint32_t COL_ROW_FOCUS  = 0xE3F2FD;
static constexpr uint32_t COL_BORDER     = 0xD0D0D0;
static constexpr uint32_t COL_FOCUS_BD   = 0x4A90D9;
static constexpr uint32_t COL_TEXT       = 0x1A1A1A;
static constexpr uint32_t COL_VALUE_ON   = 0x2E7D32;
static constexpr uint32_t COL_VALUE_OFF  = 0xB71C1C;
static constexpr uint32_t COL_VALUE      = 0x1565C0;
static constexpr uint32_t COL_HINT       = 0x808080;

// ══ Precision options ════════════════════════════════════════════════
static const int PRECISIONS[] = {6, 8, 10, 12};
static constexpr int NUM_PREC  = 4;

#if NUMOS_BOARD_PROD_WROOM1U_N16R8
namespace {
constexpr const char* SETTINGS_PATH = "/settings.dat";
constexpr const char* SETTINGS_TEMP_PATH = "/settings.tmp";
uint8_t g_persistedBrightness =
    numos::display::kSafeDisplayProfile.initialBacklight;
}

bool SettingsApp::savePersistentState() {
#if NUMOS_PRODUCTION_DEMO_PROFILE
    if (numos::demo::safeModeActive()) return false;
#endif
    const auto record = numos::demo::encodeSettingsRecord(
        numos::angleModeIsDeg(), setting_complex_enabled,
        setting_edu_steps, static_cast<uint8_t>(setting_decimal_precision),
        g_persistedBrightness);

    LittleFS.remove(SETTINGS_TEMP_PATH);
    fs::File file = LittleFS.open(SETTINGS_TEMP_PATH, "w");
    if (!file) return false;
    const bool complete =
        file.write(record.data(), record.size()) == record.size();
    file.close();
    if (!complete) {
        LittleFS.remove(SETTINGS_TEMP_PATH);
        return false;
    }
    LittleFS.remove(SETTINGS_PATH);
    const bool saved = LittleFS.rename(SETTINGS_TEMP_PATH, SETTINGS_PATH);
    if (saved) {
        Serial.printf("[SETTINGS] persist brightness=%u\n",
                      static_cast<unsigned>(g_persistedBrightness));
    }
    return saved;
}

bool SettingsApp::loadPersistentState() {
    fs::File file = LittleFS.open(SETTINGS_PATH, "r");
    if (!file) return false;  // normal first run

    std::array<uint8_t, numos::demo::kSettingsRecordSize> record{};
    if (file.size() != record.size()) {
        file.close();
        return false;
    }
    const bool complete =
        file.read(record.data(), record.size()) == record.size();
    file.close();
    if (!complete) return false;

    numos::demo::DecodedSettings decoded{};
    if (!numos::demo::decodeSettingsRecord(
            record.data(), record.size(), decoded)) return false;

    if (decoded.angleValid) {
        numos::setAngleMode(decoded.angleDeg ? vpam::AngleMode::DEG
                                             : vpam::AngleMode::RAD);
    }
    if (decoded.complexValid)
        setting_complex_enabled = decoded.complexEnabled;
    if (decoded.educationValid)
        setting_edu_steps = decoded.educationEnabled;
    if (decoded.precisionValid)
        setting_decimal_precision = decoded.precision;
    if (decoded.brightnessValid) {
        setting_brightness = decoded.brightness;
        g_persistedBrightness = decoded.brightness;
    }
    if (decoded.brightnessMigrated) {
        Serial.printf("[SETTINGS] brightness migration raw=%u visible=%u\n",
                      static_cast<unsigned>(record[9]),
                      static_cast<unsigned>(g_persistedBrightness));
        (void)savePersistentState();
    }
    return true;
}
#elif defined(__EMSCRIPTEN__)
namespace {
constexpr const char* SETTINGS_PATH = "/settings.dat";
constexpr uint32_t SETTINGS_MAGIC = 0x53543031;  // "ST01"
constexpr uint8_t SETTINGS_FORMAT_VERSION = 1;
constexpr size_t SETTINGS_RECORD_SIZE = 10;
}

bool SettingsApp::savePersistentState() {
    uint8_t record[SETTINGS_RECORD_SIZE] = {};
    std::memcpy(record, &SETTINGS_MAGIC, sizeof(SETTINGS_MAGIC));
    record[4] = SETTINGS_FORMAT_VERSION;
    record[5] = numos::angleModeIsDeg() ? 1 : 0;
    record[6] = setting_complex_enabled ? 1 : 0;
    record[7] = setting_edu_steps ? 1 : 0;
    record[8] = static_cast<uint8_t>(setting_decimal_precision);
    record[9] = 0;

    File file = LittleFS.open(SETTINGS_PATH, "w");
    if (!file) return false;
    const bool complete = file.write(record, sizeof(record)) == sizeof(record);
    file.close();
    return complete;
}

bool SettingsApp::loadPersistentState() {
    File file = LittleFS.open(SETTINGS_PATH, "r");
    if (!file) return false;

    uint8_t record[SETTINGS_RECORD_SIZE] = {};
    const bool complete = file.read(record, sizeof(record)) == sizeof(record);
    file.close();
    if (!complete) return false;

    uint32_t magic = 0;
    std::memcpy(&magic, record, sizeof(magic));
    if (magic != SETTINGS_MAGIC || record[4] != SETTINGS_FORMAT_VERSION)
        return false;

    const int precision = static_cast<int>(record[8]);
    if (precision != 6 && precision != 8 &&
        precision != 10 && precision != 12) {
        return false;
    }
    numos::setAngleMode(record[5] ? vpam::AngleMode::DEG
                                  : vpam::AngleMode::RAD);
    setting_complex_enabled = record[6] != 0;
    setting_edu_steps = record[7] != 0;
    setting_decimal_precision = precision;
    return true;
}
#endif

// ════════════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ════════════════════════════════════════════════════════════════════════════

SettingsApp::SettingsApp(DisplayDriver* display)
    : _screen(nullptr)
    , _container(nullptr)
    , _hintLabel(nullptr)
    , _brightnessSlider(nullptr)
    , _focus(0)
    , _display(display)
    , _brightnessSession()
{
    for (int i = 0; i < NUM_ITEMS; ++i) {
        _rows[i]   = nullptr;
        _labels[i] = nullptr;
        _values[i] = nullptr;
    }
}

SettingsApp::~SettingsApp() {
    end();
}

// ════════════════════════════════════════════════════════════════════════════
// begin — Create LVGL screen and widgets (called once at startup)
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::begin() {
    if (_screen) return;

    _screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    _statusBar.create(_screen);
    _statusBar.setTitle("Settings");

    createUI();
}

// ════════════════════════════════════════════════════════════════════════════
// end — Destroy the LVGL screen
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::end() {
    prepareToLeave();
    _view = View::Main;
    if (_screen) {
        _statusBar.destroy();   // nullify dangling pointers before parent screen is freed
        lv_obj_delete(_screen);
        _screen    = nullptr;
        _container = nullptr;
        _hintLabel = nullptr;
        _brightnessSlider = nullptr;
        for (int i = 0; i < NUM_ITEMS; ++i) {
            _rows[i] = nullptr;
            _labels[i] = nullptr;
            _values[i] = nullptr;
        }
        for (int i = 0; i < WIFI_ROWS_MAX; ++i) {
            _wifiRows[i] = nullptr;
            _wifiLabels[i] = nullptr;
            _wifiValues[i] = nullptr;
        }
        _wifiRowCount = 0;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// load — Activate the settings screen
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::load() {
    if (!_screen) begin();
    // Always open on the Settings rows, never mid-way through the Wi-Fi screen:
    // the container is torn down on teardown, so _view has to agree with it.
    if (_view == View::Wifi) closeWifiView();
#if NUMOS_BOARD_PROD_WROOM1U_N16R8
    _brightnessSession.begin(setting_brightness);
    setting_brightness = _brightnessSession.runtimeBrightness();
    if (_display) _display->setBacklightLevel(setting_brightness);
#endif
    _statusBar.setTitle("Settings");
    _statusBar.update();
    _focus = 0;
    updateValues();
    updateFocus();
    refreshHint();
    lv_screen_load_anim(_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void SettingsApp::prepareToLeave() {
    // The portal is transient by design: leaving Settings drops the AP and the
    // server. Portal::stop() is idempotent and, if credentials are stored, it is
    // also what hands the radio back to the STA so a freshly provisioned unit
    // joins the network without a power cycle.
    net::Portal::stop();
#if NUMOS_BOARD_PROD_WROOM1U_N16R8
    if (!_brightnessSession.active()) return;

    const uint8_t before = setting_brightness;
    const auto decision = _brightnessSession.prepareToLeave();
    setting_brightness = decision.runtimeBrightness;
    if (_display && before != setting_brightness) {
        _display->setBacklightLevel(setting_brightness);
    }

    bool saved = false;
    if (decision.persist) {
        g_persistedBrightness =
            numos::settings::normalizePersistedBrightness(
                setting_brightness);
        saved = savePersistentState();
    }
    Serial.printf(
        "[SETTINGS] brightness-exit before=%u restored=%u persist=%u saved=%u\n",
        static_cast<unsigned>(before),
        static_cast<unsigned>(setting_brightness),
        decision.persist ? 1U : 0U, saved ? 1U : 0U);
#endif
}

// ════════════════════════════════════════════════════════════════════════════
// createUI — Build the settings rows
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::createUI() {
    int barH = ui::StatusBar::HEIGHT + 1;

    _container = lv_obj_create(_screen);
    lv_obj_set_size(_container, SCREEN_W, SCREEN_H - barH - HINT_H);
    lv_obj_set_pos(_container, 0, barH);
    lv_obj_set_style_bg_opa(_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_container, 0, LV_PART_MAIN);
    // The rows are taller than this container on the 180 px canvas and the Wi-Fi
    // screen rebuilds them, so the list scrolls and updateFocus() brings the
    // focused row into view. Scrolling is driven by the keypad only — there is no
    // touch input — so the scrollbar would be noise.
    lv_obj_add_flag(_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_container, LV_SCROLLBAR_MODE_OFF);

    // The hint strip lives on the SCREEN, not in the container: it must not
    // scroll away, because while the portal is up it is the only place the AP
    // name, its password and the browser URL are shown.
    _hintLabel = lv_label_create(_screen);
    lv_obj_set_width(_hintLabel, SCREEN_W - 2 * PAD);
    lv_obj_set_height(_hintLabel, LV_SIZE_CONTENT);
    lv_label_set_long_mode(_hintLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_hintLabel, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(_hintLabel, lv_color_hex(COL_HINT), LV_PART_MAIN);
    lv_obj_set_pos(_hintLabel, PAD, SCREEN_H - HINT_H + 2);

    createRows();
}

void SettingsApp::createRows() {
    lv_obj_clean(_container);
    for (int i = 0; i < NUM_ITEMS; ++i) {
        _rows[i]   = nullptr;
        _labels[i] = nullptr;
        _values[i] = nullptr;
    }
    _brightnessSlider = nullptr;

    const char* labels[NUM_ITEMS] = {
        "Angle mode",          // row 0 per SET spec §E.3.4 (highest-priority row)
        "Complex numbers",
        "Decimal precision",
        "Step-by-step mode",
#if NUMOS_BOARD_PROD_WROOM1U_N16R8
        "Brightness",
#endif
        // Opens the Wi-Fi screen: saved networks, signal, connect/forget, and the
        // provisioning portal that puts networks on the list in the first place.
        "Wi-Fi",
    };

    for (int i = 0; i < NUM_ITEMS; ++i) {
        int y = 6 + i * (ROW_H + ROW_GAP);

        // Row background
        _rows[i] = lv_obj_create(_container);
        lv_obj_set_size(_rows[i], SCREEN_W - 2 * PAD, ROW_H);
        lv_obj_set_pos(_rows[i], PAD, y);
        lv_obj_set_style_bg_color(_rows[i], lv_color_hex(COL_ROW_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_rows[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(_rows[i], lv_color_hex(COL_BORDER), LV_PART_MAIN);
        lv_obj_set_style_border_width(_rows[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(_rows[i], 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_rows[i], 0, LV_PART_MAIN);
        lv_obj_remove_flag(_rows[i], LV_OBJ_FLAG_SCROLLABLE);

        // Label (left side)
        _labels[i] = lv_label_create(_rows[i]);
        lv_label_set_text(_labels[i], labels[i]);
        // Phase 7I: plain UI text → lv_font_montserrat_14. stix_math_18's cmap
        // starts at U+0021, so it has no U+0020 (space) glyph; with
        // LV_USE_FONT_PLACEHOLDER the spaced names ("Complex numbers", etc.)
        // painted a tofu box at every space.
        lv_obj_set_style_text_font(_labels[i], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(_labels[i], lv_color_hex(COL_TEXT), LV_PART_MAIN);
        lv_obj_align(_labels[i], LV_ALIGN_LEFT_MID, 12, 0);

        // Value (right side)
        // Phase 7I: plain UI text → lv_font_montserrat_14 (the value "%d digits"
        // contains a space that stix_math_18 cannot render — see _labels above).
        _values[i] = lv_label_create(_rows[i]);
        lv_obj_set_style_text_font(_values[i], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(_values[i], LV_ALIGN_RIGHT_MID, -12, 0);
    }

#if NUMOS_BOARD_PROD_WROOM1U_N16R8
    _brightnessSlider = lv_slider_create(_rows[4]);
    lv_obj_set_size(_brightnessSlider, 112, 8);
    lv_obj_align(_brightnessSlider, LV_ALIGN_LEFT_MID, 96, 0);
    lv_slider_set_range(_brightnessSlider,
                        numos::display::kMinimumPersistedBacklight,
                        numos::display::kMaximumBacklight);
    lv_obj_remove_flag(_brightnessSlider, LV_OBJ_FLAG_CLICKABLE);
#endif

    updateValues();
    updateFocus();
}

// ════════════════════════════════════════════════════════════════════════════
// updateFocus — Highlight the focused row
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::updateFocus() {
    for (int i = 0; i < NUM_ITEMS; ++i) {
        if (i == _focus) {
            lv_obj_set_style_bg_color(_rows[i], lv_color_hex(COL_ROW_FOCUS), LV_PART_MAIN);
            lv_obj_set_style_border_color(_rows[i], lv_color_hex(COL_FOCUS_BD), LV_PART_MAIN);
            lv_obj_set_style_border_width(_rows[i], 2, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(_rows[i], lv_color_hex(COL_ROW_BG), LV_PART_MAIN);
            lv_obj_set_style_border_color(_rows[i], lv_color_hex(COL_BORDER), LV_PART_MAIN);
            lv_obj_set_style_border_width(_rows[i], 1, LV_PART_MAIN);
        }
    }
    // The row list can be taller than the container; keep the cursor visible.
    if (_rows[_focus]) lv_obj_scroll_to_view(_rows[_focus], LV_ANIM_OFF);
    lv_obj_invalidate(_screen);
}

// ════════════════════════════════════════════════════════════════════════════
// updateValues — Refresh displayed values from global settings
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::updateValues() {
    // Angle mode (runtime source of truth — same value the StatusBar badge shows)
    lv_label_set_text(_values[0], numos::angleModeIsDeg() ? "Degrees" : "Radians");
    lv_obj_set_style_text_color(_values[0], lv_color_hex(COL_VALUE), LV_PART_MAIN);

    // Complex toggle
    if (setting_complex_enabled) {
        lv_label_set_text(_values[1], "ON");
        lv_obj_set_style_text_color(_values[1], lv_color_hex(COL_VALUE_ON), LV_PART_MAIN);
    } else {
        lv_label_set_text(_values[1], "OFF");
        lv_obj_set_style_text_color(_values[1], lv_color_hex(COL_VALUE_OFF), LV_PART_MAIN);
    }

    // Decimal precision
    char buf[16];
    snprintf(buf, sizeof(buf), "%d digits", setting_decimal_precision);
    lv_label_set_text(_values[2], buf);
    lv_obj_set_style_text_color(_values[2], lv_color_hex(COL_VALUE), LV_PART_MAIN);

    // Step-by-step educational mode
    if (setting_edu_steps) {
        lv_label_set_text(_values[3], "ON");
        lv_obj_set_style_text_color(_values[3], lv_color_hex(COL_VALUE_ON), LV_PART_MAIN);
    } else {
        lv_label_set_text(_values[3], "OFF");
        lv_obj_set_style_text_color(_values[3], lv_color_hex(COL_VALUE_OFF), LV_PART_MAIN);
    }

#if NUMOS_BOARD_PROD_WROOM1U_N16R8
    const unsigned percent =
        (static_cast<unsigned>(setting_brightness) * 100U +
         numos::display::kMaximumBacklight / 2U) /
        numos::display::kMaximumBacklight;
    char brightnessText[8];
    snprintf(brightnessText, sizeof(brightnessText), "%u%%", percent);
    lv_label_set_text(_values[4], brightnessText);
    lv_obj_set_style_text_color(_values[4], lv_color_hex(COL_VALUE), LV_PART_MAIN);
    lv_slider_set_value(_brightnessSlider, setting_brightness, LV_ANIM_OFF);
#endif

    // Wi-Fi row. Deliberately short: the SSID itself belongs on the Wi-Fi screen
    // and in the hint strip, which have more room than this row's value field.
    {
        char wbuf[20];
        const size_t nets = net::Wifi::networkCount();
        const net::WifiState ws = net::Wifi::state();
        if (ws.connected) {
            std::snprintf(wbuf, sizeof(wbuf), "connected");
            lv_obj_set_style_text_color(_values[WIFI_ROW], lv_color_hex(COL_VALUE_ON), LV_PART_MAIN);
        } else if (nets > 0) {
            std::snprintf(wbuf, sizeof(wbuf), "%u saved", static_cast<unsigned>(nets));
            lv_obj_set_style_text_color(_values[WIFI_ROW], lv_color_hex(COL_VALUE), LV_PART_MAIN);
        } else {
            std::snprintf(wbuf, sizeof(wbuf), "none");
            lv_obj_set_style_text_color(_values[WIFI_ROW], lv_color_hex(COL_VALUE_OFF), LV_PART_MAIN);
        }
        lv_label_set_text(_values[WIFI_ROW], wbuf);
    }
}

/**
 * The hint strip: the nav line, the portal's AP/URL block, or the Wi-Fi screen's
 * own keys. One place, because it is the only strip the user can always see.
 */
void SettingsApp::refreshHint() {
    if (!_hintLabel) return;
    char buf[140];

    const net::PortalState ps = net::Portal::state();

    if (_view == View::Wifi) {
        // The credentials belong HERE, next to the row that raises the AP: this
        // is where the toggle is pressed, so this is where they get read. Two
        // lines, 12 px font, inside HINT_H.
        if (ps.running) {
            std::snprintf(buf, sizeof(buf), "%s   pass %s\n%s   stations %d",
                          ps.apSsid.c_str(), ps.apPass.c_str(),
                          ps.url.c_str(), ps.stations);
        } else if (const char* why = net::Portal::unavailableReason()) {
            std::snprintf(buf, sizeof(buf), "%s", why);
        } else if (!ps.lastError.empty()) {
            // start() was tried and failed on this build: say why instead of
            // leaving a dead-looking toggle. The row would otherwise just stay
            // OFF with no explanation (the reason used to reach the serial log
            // only).
            std::snprintf(buf, sizeof(buf), "portal failed: %s",
                          ps.lastError.c_str());
        } else {
            std::snprintf(buf, sizeof(buf),
                          "EXE portal   DEL forget   RIGHT rescan   LEFT back");
        }
        lv_label_set_text(_hintLabel, buf);
        return;
    }

    // Main Settings screen: how to get in, or what the device is doing. The AP
    // credentials are one level down (the Wi-Fi row) and are not duplicated here.
    if (ps.running) {
        std::snprintf(buf, sizeof(buf), "Portal ON - open Wi-Fi for SSID/pass");
    } else if (ps.staConnected) {
        std::snprintf(buf, sizeof(buf), "Wi-Fi %s %s", ps.staSsid.c_str(), ps.staIp.c_str());
    } else if (net::Wifi::networkCount() == 0) {
        std::snprintf(buf, sizeof(buf), "Wi-Fi: none saved - open Wi-Fi to provision");
    } else {
        std::snprintf(buf, sizeof(buf), "Navigate   Left/Right Adjust   MODE Back");
    }
    lv_label_set_text(_hintLabel, buf);
}

// ════════════════════════════════════════════════════════════════════════════
// Wi-Fi screen — saved networks with their signal, and the provisioning portal
// ════════════════════════════════════════════════════════════════════════════
//
// Row 0 is the portal toggle, rows 1..n are the saved networks in priority
// order. The portal is row 0 and not a separate Settings row because it is what
// PUTS networks on this list — a 20-character WPA2 password is not being typed
// on a 5x10 keypad, so the phone does it and the device stores the result.
//
// Keys: UP/DOWN move, EXE toggles the portal (row 0), ENTER connects to the
// focused network, DEL forgets, RIGHT rescans, LEFT/AC goes back. MODE still
// leaves Settings entirely — that interception happens in SystemApp before this
// app sees the key.

void SettingsApp::buildWifiView() {
    _view  = View::Wifi;
    _focus = 0;

    // A scan takes 1-2 s and is collected asynchronously by Wifi::tick(), so the
    // screen appears immediately and fills in signal as the results land.
    net::Wifi::startScan();

    lv_obj_clean(_container);
    for (int i = 0; i < NUM_ITEMS; ++i) {
        _rows[i]   = nullptr;
        _labels[i] = nullptr;
        _values[i] = nullptr;
    }
    _brightnessSlider = nullptr;

    const size_t nets = net::Wifi::networkCount();
    _wifiRowCount = 1 + static_cast<int>(nets > 0 ? nets : 1);
    if (_wifiRowCount > WIFI_ROWS_MAX) _wifiRowCount = WIFI_ROWS_MAX;

    for (int i = 0; i < _wifiRowCount; ++i) {
        const int y = 4 + i * (WIFI_LIST_H + ROW_GAP);

        _wifiRows[i] = lv_obj_create(_container);
        lv_obj_set_size(_wifiRows[i], SCREEN_W - 2 * PAD, WIFI_LIST_H);
        lv_obj_set_pos(_wifiRows[i], PAD, y);
        lv_obj_set_style_bg_color(_wifiRows[i], lv_color_hex(COL_ROW_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_wifiRows[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(_wifiRows[i], lv_color_hex(COL_BORDER), LV_PART_MAIN);
        lv_obj_set_style_border_width(_wifiRows[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(_wifiRows[i], 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_wifiRows[i], 0, LV_PART_MAIN);
        lv_obj_remove_flag(_wifiRows[i], LV_OBJ_FLAG_SCROLLABLE);

        _wifiLabels[i] = lv_label_create(_wifiRows[i]);
        lv_obj_set_width(_wifiLabels[i], 176);
        // An SSID can be 32 characters; ellipsize rather than overflow the row.
        lv_label_set_long_mode(_wifiLabels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(_wifiLabels[i], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(_wifiLabels[i], lv_color_hex(COL_TEXT), LV_PART_MAIN);
        lv_obj_align(_wifiLabels[i], LV_ALIGN_LEFT_MID, 10, 0);

        _wifiValues[i] = lv_label_create(_wifiRows[i]);
        lv_obj_set_style_text_font(_wifiValues[i], &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_align(_wifiValues[i], LV_ALIGN_RIGHT_MID, -10, 0);
    }

    refreshWifiView();
    refreshHint();
}

void SettingsApp::closeWifiView() {
    _view  = View::Main;
    _focus = WIFI_ROW;   // come back with the cursor on the row that opened it
    createRows();        // rebuilds the Settings rows + calls updateValues/Focus
    refreshHint();
}

void SettingsApp::updateWifiFocus() {
    for (int i = 0; i < _wifiRowCount; ++i) {
        if (!_wifiRows[i]) continue;
        if (i == _focus) {
            lv_obj_set_style_bg_color(_wifiRows[i], lv_color_hex(COL_ROW_FOCUS), LV_PART_MAIN);
            lv_obj_set_style_border_color(_wifiRows[i], lv_color_hex(COL_FOCUS_BD), LV_PART_MAIN);
            lv_obj_set_style_border_width(_wifiRows[i], 2, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(_wifiRows[i], lv_color_hex(COL_ROW_BG), LV_PART_MAIN);
            lv_obj_set_style_border_color(_wifiRows[i], lv_color_hex(COL_BORDER), LV_PART_MAIN);
            lv_obj_set_style_border_width(_wifiRows[i], 1, LV_PART_MAIN);
        }
    }
    if (_wifiRows[_focus]) lv_obj_scroll_to_view(_wifiRows[_focus], LV_ANIM_OFF);
    lv_obj_invalidate(_screen);
}

void SettingsApp::refreshWifiView() {
    if (_view != View::Wifi || !_wifiRows[0]) return;
    char text[80];

    // Row 0: the portal.
    const net::PortalState ps = net::Portal::state();
    lv_label_set_text(_wifiLabels[0], "Web portal");
    lv_label_set_text(_wifiValues[0], ps.running ? "ON" : "OFF");
    lv_obj_set_style_text_color(_wifiValues[0],
                                lv_color_hex(ps.running ? COL_VALUE_ON : COL_VALUE_OFF),
                                LV_PART_MAIN);

    const size_t nets = net::Wifi::networkCount();
    const net::WifiState ws = net::Wifi::state();
    if (nets == 0) {
        if (_wifiRowCount > 1) {
            lv_label_set_text(_wifiLabels[1], "no networks saved");
            lv_obj_set_style_text_color(_wifiLabels[1], lv_color_hex(COL_HINT), LV_PART_MAIN);
            lv_label_set_text(_wifiValues[1], "use portal");
            lv_obj_set_style_text_color(_wifiValues[1], lv_color_hex(COL_HINT), LV_PART_MAIN);
        }
        updateWifiFocus();
        return;
    }

    for (size_t i = 0; i < nets && static_cast<int>(i + 1) < _wifiRowCount; ++i) {
        net::WifiNetwork n;
        if (!net::Wifi::networkAt(i, n)) continue;
        const int row = static_cast<int>(i) + 1;

        std::snprintf(text, sizeof(text), "%u. %s", static_cast<unsigned>(i + 1), n.ssid.c_str());
        lv_label_set_text(_wifiLabels[row], text);
        lv_obj_set_style_text_color(_wifiLabels[row], lv_color_hex(COL_TEXT), LV_PART_MAIN);

        // Availability, in this order: the live link beats a scan sighting, and a
        // scan sighting beats "not seen" — but "not seen" is only honest once a
        // scan has actually finished.
        const int rssi = net::Wifi::signalFor(n.ssid);
        if (ws.connected && ws.ssid == n.ssid) {
            std::snprintf(text, sizeof(text), "connected %d dBm", ws.rssi);
            lv_obj_set_style_text_color(_wifiValues[row], lv_color_hex(COL_VALUE_ON), LV_PART_MAIN);
        } else if (rssi < 0) {
            std::snprintf(text, sizeof(text), "%d dBm", rssi);
            lv_obj_set_style_text_color(_wifiValues[row], lv_color_hex(COL_VALUE), LV_PART_MAIN);
        } else if (net::Wifi::scanRunning()) {
            std::snprintf(text, sizeof(text), "scanning");
            lv_obj_set_style_text_color(_wifiValues[row], lv_color_hex(COL_HINT), LV_PART_MAIN);
        } else {
            std::snprintf(text, sizeof(text), "not in range");
            lv_obj_set_style_text_color(_wifiValues[row], lv_color_hex(COL_VALUE_OFF), LV_PART_MAIN);
        }
        lv_label_set_text(_wifiValues[row], text);
    }
    updateWifiFocus();
}

void SettingsApp::wifiActivate(int row) {
    net::WifiNetwork n;
    if (!net::Wifi::networkAt(static_cast<size_t>(row), n)) return;

    // Promoting to slot 0 is the whole "connect to this one" story: Wifi::tick()
    // walks the list from the front. Restarting the STA makes it take effect now
    // instead of after a retry cycle — and it stays non-blocking, because a
    // 12-second join on the loop task would freeze the UI and the keypad.
    if (!net::Wifi::saveNetwork(n.ssid, n.pass, /*makePrimary=*/true)) return;
    net::Wifi::disconnect(/*eraseCreds=*/false);
    net::Wifi::begin();
    Serial.printf("[SETTINGS] wifi connect requested: '%s'\n", n.ssid.c_str());
    refreshWifiView();
    refreshHint();
}

void SettingsApp::wifiForget(int row) {
    if (row <= 0) return;   // row 0 is the portal toggle
    net::WifiNetwork n;
    if (!net::Wifi::networkAt(static_cast<size_t>(row - 1), n)) return;
    if (!net::Wifi::forgetNetwork(n.ssid)) return;
    Serial.printf("[SETTINGS] wifi forgotten: '%s'\n", n.ssid.c_str());

    // The list just changed size; rebuild it so the rows match it exactly.
    const int keep = (row >= _wifiRowCount - 1) ? _wifiRowCount - 2 : row;
    buildWifiView();
    _focus = (keep < 0) ? 0 : keep;
    if (_focus >= _wifiRowCount) _focus = _wifiRowCount - 1;
    updateWifiFocus();
}

void SettingsApp::update() {
    const uint32_t now = lv_tick_get();
    if ((now - _lastPollMs) < 1000u) return;
    _lastPollMs = now;

    // Both the portal's state and the Wi-Fi association change with no user
    // input (the AP comes up, the STA lands, a scan completes), so the screen
    // catches up by itself. LVGL is only ever touched from here, on the loop task.
    if (_view == View::Wifi) {
        refreshWifiView();
        refreshHint();   // the AP's station count/error text moves while it runs
    } else {
        updateValues();
        refreshHint();
    }
}

void SettingsApp::adjustBrightness(const int delta) {
#if NUMOS_BOARD_PROD_WROOM1U_N16R8
    const int maximum = numos::display::kMaximumBacklight;
    int next = static_cast<int>(setting_brightness) + delta;
    if (next < numos::display::kMinimumPersistedBacklight) {
        next = numos::display::kMinimumPersistedBacklight;
    }
    if (next > maximum) next = maximum;
    if (next == setting_brightness) return;

    setting_brightness = _brightnessSession.setRuntime(next);
    if (_display) _display->setBacklightLevel(setting_brightness);
    updateValues();
#else
    (void)delta;
#endif
}

// ════════════════════════════════════════════════════════════════════════════
// toggleCurrent — Change the current setting's value
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::toggleCurrent() {
    switch (_focus) {
        case 0:  // Angle mode toggle: writes the runtime truth, badge follows
            numos::setAngleMode(numos::angleModeIsDeg() ? vpam::AngleMode::RAD
                                                        : vpam::AngleMode::DEG);
            _statusBar.update();   // repaint this screen's DEG/RAD badge now (§E.3.4)
            break;

        case 1:  // Complex numbers toggle
            setting_complex_enabled = !setting_complex_enabled;
            break;

        case 2: {  // Decimal precision cycle: 6 → 8 → 10 → 12 → 6
            int idx = 0;
            for (int j = 0; j < NUM_PREC; ++j) {
                if (PRECISIONS[j] == setting_decimal_precision) {
                    idx = j;
                    break;
                }
            }
            idx = (idx + 1) % NUM_PREC;
            setting_decimal_precision = PRECISIONS[idx];
            break;
        }

        case 3:  // Step-by-step educational mode toggle
            setting_edu_steps = !setting_edu_steps;
            break;

#if NUMOS_BOARD_PROD_WROOM1U_N16R8
        case 4:  // Minimum -> normal -> maximum -> minimum.
            if (setting_brightness ==
                numos::display::kMinimumPersistedBacklight) {
                adjustBrightness(
                    numos::display::kSafeDisplayProfile.initialBacklight);
            } else if (setting_brightness < numos::display::kMaximumBacklight) {
                adjustBrightness(numos::display::kMaximumBacklight -
                                 setting_brightness);
            } else {
                adjustBrightness(-numos::display::kMaximumBacklight);
            }
            break;
#endif

        case WIFI_ROW:  // opens the Wi-Fi screen; nothing to persist here
            buildWifiView();
            return;     // the rows this function would refresh were just deleted
    }

    updateValues();
#if defined(__EMSCRIPTEN__)
    // One compact record per completed user action. FileSystem marks the
    // successful close dirty; JavaScript coalesces repeated actions.
    savePersistentState();
#elif NUMOS_BOARD_PROD_WROOM1U_N16R8
    // Brightness writes are deferred until prepareToLeave(). Other settings
    // retain immediate persistence, using the last committed brightness.
    if (_focus != 4) savePersistentState();
#endif
}

// ════════════════════════════════════════════════════════════════════════════
// handleKey — Process key events in settings
// ════════════════════════════════════════════════════════════════════════════

void SettingsApp::handleKey(const KeyEvent& ev) {
    if (ev.action != KeyAction::PRESS && ev.action != KeyAction::REPEAT) return;

    // ── the Wi-Fi screen has its own keymap ─────────────────────────────────
    if (_view == View::Wifi) {
        switch (ev.code) {
            case KeyCode::UP:
                if (_focus > 0) { --_focus; updateWifiFocus(); }
                return;
            case KeyCode::DOWN:
                if (_focus + 1 < _wifiRowCount) { ++_focus; updateWifiFocus(); }
                return;
            case KeyCode::RIGHT:
                net::Wifi::startScan();   // results arrive via Wifi::tick()
                refreshWifiView();
                return;
            case KeyCode::DEL:
                wifiForget(_focus);
                return;
            case KeyCode::LEFT:
            case KeyCode::AC:
                closeWifiView();
                return;
            case KeyCode::EXE:
            case KeyCode::ENTER:
                // Row 0 is the portal toggle; rows 1..n connect.
                //
                // Both codes are handled because on the production target the
                // execute key is run through KeySemanticResolver, whose plane
                // definitions rewrite KeyCode::EXE to KeyCode::ENTER. So EXE is
                // emulator/serial only — on the real device the toggle arrives
                // as ENTER. FractalApp handles the pair the same way.
                if (_focus == 0) {
                    // Raising the portal is also how a network gets ON this
                    // list, since the password has to be typed on a phone.
                    if (net::Portal::running()) {
                        net::Portal::stop();
                    } else if (!net::Portal::start()) {
                        Serial.printf("[SETTINGS] portal start failed: %s\n",
                                      net::Portal::state().lastError.c_str());
                    }
                    refreshWifiView();
                    refreshHint();
                    return;
                }
                wifiActivate(_focus - 1);
                return;
            default:
                return;
        }
    }

    switch (ev.code) {
        case KeyCode::UP:
            if (_focus > 0) {
                --_focus;
                updateFocus();
            }
            break;

        case KeyCode::DOWN:
            if (_focus < NUM_ITEMS - 1) {
                ++_focus;
                updateFocus();
            }
            break;

        case KeyCode::ENTER:
        case KeyCode::EXE:   // Prod resolver rewrites EXE->ENTER; on non-Prod
                             // builds EXE arrives raw, so treat it as confirm too.
            toggleCurrent();
            break;

        case KeyCode::LEFT:
            // For precision (row 2): cycle backward
            if (_focus == 2) {
                int idx = 0;
                for (int j = 0; j < NUM_PREC; ++j) {
                    if (PRECISIONS[j] == setting_decimal_precision) {
                        idx = j;
                        break;
                    }
                }
                idx = (idx - 1 + NUM_PREC) % NUM_PREC;
                setting_decimal_precision = PRECISIONS[idx];
                updateValues();
#if defined(__EMSCRIPTEN__) || NUMOS_BOARD_PROD_WROOM1U_N16R8
                savePersistentState();
#endif
            }
#if NUMOS_BOARD_PROD_WROOM1U_N16R8
            else if (_focus == 4) {
                adjustBrightness(-8);
            }
#endif
            break;

        case KeyCode::RIGHT:
            // For precision (row 2): cycle forward
            if (_focus == 2) {
                toggleCurrent();
            }
#if NUMOS_BOARD_PROD_WROOM1U_N16R8
            else if (_focus == 4) {
                adjustBrightness(8);
            }
#endif
            break;

        default:
            break;
    }
}
