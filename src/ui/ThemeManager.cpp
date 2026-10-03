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
 * ThemeManager.cpp (doc 03 + doc 10)
 * The single write path for the active theme. activate() swaps current(),
 * binds the theme's InteractionModel (both refresh together — no gap),
 * rebuilds the LVGL theme and persists the ThemeId.
 *
 * The emulator's SystemApp is not compiled (NATIVE_SIM builds NativeHal instead),
 * so NativeHal mirrors the boot activation + hotkey funnel; both call this class.
 */

#include "ThemeManager.h"

#include "MathTypography.h"

#include "Config.h"
#include "Theme.h"

#ifndef LV_THEME_DEFAULT_DARK
#define LV_THEME_DEFAULT_DARK 0
#endif

#if defined(__EMSCRIPTEN__) || NUMOS_BOARD_PROD_WROOM1U_N16R8
#include "../apps/SettingsApp.h"
#endif

namespace ui {

ThemeManager::ThemeManager()
    : _id(ThemeId::NumOS)
    , _interaction(&kNumOSInteraction)
    , _lvTheme(nullptr) {
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager g_instance;
    return g_instance;
}

const Theme& ThemeManager::current() const {
    return *themeFor(_id);
}

const InteractionModel& ThemeManager::interaction() const {
    return *_interaction;
}

lv_theme_t* ThemeManager::buildLvglTheme(const Theme& theme) {
    lv_display_t* disp = lv_display_get_default();
    if (!disp) return nullptr;

    lv_color_t primary;
    lv_color_t secondary;
    if (theme.layout == Layout::NumOS) {
        // Byte-identical to today's implicit LVGL default theme, which the
        // display created at lv_display_create() (lv_display.c): primary blue,
        // secondary red, dark flag, default font. Recreating it with the same
        // arguments yields the same styles, so the numos look never changes.
        primary   = lv_palette_main(LV_PALETTE_BLUE);
        secondary = lv_palette_main(LV_PALETTE_RED);
    } else {
        primary   = lv_color_hex(theme.accent);
        secondary = lv_color_hex(theme.accentSecondary);
    }

    // The default font is the theme's UI face, so every widget that does not set
    // an explicit font inherits the ACTIVE theme rather than LVGL's compile-time
    // LV_FONT_DEFAULT. numos' fontUi IS LV_FONT_DEFAULT (montserrat 14), so this
    // changes nothing for numos; casio's widgets get the LCD face for free.
    const lv_font_t* defaultFont = theme.fontUi ? theme.fontUi : LV_FONT_DEFAULT;
    _lvTheme = lv_theme_default_init(disp, primary, secondary,
                                     static_cast<bool>(LV_THEME_DEFAULT_DARK),
                                     defaultFont);
    if (_lvTheme) {
        lv_display_set_theme(disp, _lvTheme);
    }
    return _lvTheme;
}

void ThemeManager::activate(ThemeId id) {
    const Theme* theme = themeFor(id);
    _id = id;
    // Bind theme + interaction atomically so the palette and the behaviour
    // profile never disagree (doc 03/10).
    _interaction = theme->interaction ? theme->interaction : &kNumOSInteraction;

    buildLvglTheme(*theme);

    // The math font is a theme asset (doc 13 Phase 1): re-apply the active
    // set to the global math style so step containers do not leak glyphs.
    ui::initMathTypography();

    // The math font is a theme asset (doc 13 Phase 1): re-apply the active
    // set to the global math style so step containers do not leak glyphs.
    ui::initMathTypography();

    // Persist the typed id (single owner: ThemeManager). The global is the
    // in-RAM mirror; the file write funnels through SettingsApp's record.
    setting_theme = static_cast<uint8_t>(id);
#if defined(__EMSCRIPTEN__) || NUMOS_BOARD_PROD_WROOM1U_N16R8
    SettingsApp::savePersistentState();
#endif
}

} // namespace ui