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
 * Theme.h
 * Runtime theme contract (doc 02/03/13): a `Theme` is a flat POD that bundles
 * the colour tokens, the chrome dials, the UI fonts and the math font set.
 * `ThemeManager` owns the active instance; apps read
 * `ui::ThemeManager::instance().current().<token>` instead of literals.
 *
 * The legacy `COLOR_*` / `FONT_*` constants below are KEPT so untouched call
 * sites still compile — new code reads tokens.
 */

#pragma once

#include <cstdint>
#include <lvgl.h>

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include <cstdint>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Legacy flat palette (RGB565) — preserved for compatibility.
// New code reads `struct Theme` tokens instead.
// ─────────────────────────────────────────────────────────────────────────────

// Colors (RGB565)
// White/Cream background
static const uint16_t COLOR_BACKGROUND = 0xFFFF; // Pure White 
// static const uint16_t COLOR_BACKGROUND = 0xFFFE; // Slightly off-white if needed

// NumWorks Orange/Gold
// Hex: #FFB531 -> RGB565 approx
static const uint16_t COLOR_PRIMARY = 0xFD20; 

// Top Bar / Header Background usually same as Primary or distinct
static const uint16_t COLOR_HEADER_BG = COLOR_PRIMARY; 
static const uint16_t COLOR_HEADER_TEXT = 0x0000;

// Text Colors
static const uint16_t COLOR_TEXT = 0x0000; // Black
static const uint16_t COLOR_TEXT_WHITE = 0xFFFF; // For dark backgrounds
static const uint16_t COLOR_TEXT_LIGHT = 0x9CD3; // Light Grey
static const uint16_t COLOR_TEXT_DARK = 0x3333; // Dark Grey

// Accents
static const uint16_t COLOR_ACCENT = 0xE71C; // Light grey for borders/grid
static const uint16_t COLOR_SELECTION = 0xDEDB; // Light grey selection

// Fonts
// Using built-in GLCD/Fonts for now, aliases for logic
// 1 = GLCD (Tiny), 2 = Small, 4 = Medium, 6 = Large Number
static const uint8_t FONT_SMALL = 1;
static const uint8_t FONT_REGULAR = 2; // Or 2
static const uint8_t FONT_HEADER = 2;  // Or 4
static const uint8_t FONT_LARGE = 4;

// ─────────────────────────────────────────────────────────────────────────────
// Theme contract
// ─────────────────────────────────────────────────────────────────────────────

namespace ui {

struct InteractionModel;

/** Which visual layout this theme rides on (doc 02). */
enum class Layout : uint8_t {
    NumOS = 0,
    Casio = 1,
};

/** Theme identifiers: persisted as a uint8_t in the SettingsApp record. */
enum class ThemeId : uint8_t {
    NumOS = 0,
    Casio = 1,
    // third-party skins get ids >= 2 once the import path exists (doc 03)
};

/**
 * App identifiers, as used by `MainMenu::APPS[]` and `SystemApp::launchApp()`.
 * Only the apps that need a per-app surface are named here; the numbers must
 * match the launcher's table.
 */
namespace appid {
constexpr int kGrapher = 1;
constexpr int kGameBoy = 21;
constexpr int kAi      = 23;
} // namespace appid

/**
 * Per-app surface override (doc 02/07 extension — "extend the contract, never
 * inline the literal").
 *
 * The global tokens describe the OS chrome. A handful of apps are designed on a
 * surface those tokens cannot express: they are dark for legibility (AI, Game
 * Boy, and the lab/visualisation family) and must not turn light with the
 * chrome. This table lets a theme pin one app's surface by app id.
 *
 * An id with no entry — or a theme with `appSurfaces == nullptr`, which is what
 * a third-party skin normally ships — means "inherit the theme tokens", so a
 * skin that ships zero art and zero surfaces still renders every app.
 */
struct AppSurface {
    int      appId;
    uint32_t bg;          ///< screen background
    uint32_t pane;        ///< inset panel fill (input box, card)
    uint32_t row;         ///< list row fill, unfocused
    uint32_t rowFocus;    ///< list row fill, focused
    uint32_t text;        ///< primary ink
    uint32_t textDim;     ///< secondary / hint ink
    uint32_t title;       ///< chrome title ink
    uint32_t textOnFocus; ///< ink on a focused row
    uint32_t accent;      ///< borders / accents / focus edge
    uint8_t  radiusRow;   ///< row corner radius (0 = crude)
    uint8_t  radiusPane;  ///< panel corner radius
    uint8_t  borderWidth; ///< panel border width
};

/** A resolved surface: an override if the theme has one, else its own tokens. */
struct AppColours {
    uint32_t bg, pane, row, rowFocus, text, textDim, title, textOnFocus, accent;
    uint8_t  radiusRow, radiusPane, borderWidth;
};

/**
 * The math font set (doc 13, Phase 0): the renderer needs the three sizes plus
 * their OpenType MATH design sizes. `numos` = the STIX set (byte-identical),
 * `casio` = the CASIO-Calculator-Font set.
 */
struct MathFontSet {
    const lv_font_t* primary;        // display/text style  (18pt today)
    const lv_font_t* script;         // first-level sub/sup  (12pt)
    const lv_font_t* scriptScript;   // nested sub/sup       ( 8pt)
    int16_t emPrimary;
    int16_t emScript;
    int16_t emScriptScript;
};

/**
 * One theme = layout + interaction + skin (doc 02).
 * Tokens are RGB888 (`utils::rgb888to565()` where a buffer needs 565).
 */
struct Theme {
    // ── identity ──
    const char*            id;           // "numos", "casio"
    const char*            name;         // "NumOS", "Casio ClassWiz"
    Layout                 layout;       // visual arrangement
    const InteractionModel* interaction; // behaviour profile (doc 12); nullptr -> NumOS default

    // ── color tokens (RGB888) ──
    uint32_t bg;             // main screen background
    uint32_t bgCanvas;       // drawing-surface bg (math graph canvas)
    uint32_t bgPane;         // cards / rows / panels
    uint32_t bgHeader;       // top chrome / header
    uint32_t bgToolbar;      // bottom / tab toolbar
    uint32_t text;           // primary text
    uint32_t textDim;        // secondary / hint text
    uint32_t textOnAccent;   // text on accent/header fills
    uint32_t accent;         // primary accent
    uint32_t accentSecondary;// second accent / chart line 2
    uint32_t border;         // hairline borders
    uint32_t focus;          // focused row/card fill
    uint32_t focusBorder;    // focus outline
    uint32_t stepHighlight;  // math intermediate step  (was 0x1565C0)
    uint32_t result;         // math final result       (was 0xE05500)
    uint32_t danger;         // errors / warnings
    uint32_t plot[4];        // graph series ramp

    // ── chrome dials ──
    uint8_t  cornerRadius;   // 0 = crude, >0 = modern
    uint8_t  borderWidth;    // 1 = crude, 0 = modern
    bool     shadows;
    uint8_t  graphLineWidth; // stroke width for graph curves (px). 1 = hairline;
                             // the fitted 320x156 canvas is far smaller than the
                             // OEM one, so a hairline curve reads as noise there.

    // ── UI fonts (a small LADDER, not one face) ──
    // Four role slots + mono, so every explicit font in src/ maps onto a token and
    // no call site keeps a literal face. The rungs exist because the sources use
    // exactly four text sizes (10/12/14/20 + unscii-8 mono): collapsing them onto
    // one size would repaint the numos screens, which must stay byte-identical.
    // Casio's face ships at 12/18/26, so its xs and sm both map to 12.
    const lv_font_t* fontUi;       // body: labels, menus, buttons      (numos 14 / casio 18)
    const lv_font_t* fontUiSmall;  // secondary: hints, status bar      (numos 12 / casio 12)
    const lv_font_t* fontUiXSmall; // tertiary: smallest heading        (numos 10 / casio 12)
    const lv_font_t* fontDisplay;  // headline: splash logo, headings   (numos 20 / casio 26)
    const lv_font_t* fontMono;     // editor / code                     (unscii-8)

    // ── math font set (doc 13) ──
    MathFontSet mathFonts;

    // ── per-app surface overrides (appid table above) ──
    // nullptr-terminated, or nullptr for "this theme overrides nothing".
    const AppSurface* appSurfaces;
};

/**
 * Resolve the surface an app should draw on: the active theme's override for
 * `appId` when it has one, otherwise the theme's own tokens. Apps call this
 * (never a literal, never a theme-id branch), so both themes and any future
 * skin work with no app change.
 */
AppColours appSurface(int appId);

/**
 * Built-in theme table (themeFor/activated themes). Lives in Theme.cpp.
 * A new skin = one `Theme{}` initializer + one table row (doc 03).
 */
const Theme* themeFor(ThemeId id);

} // namespace ui