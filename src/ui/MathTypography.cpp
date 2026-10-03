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

#include "MathTypography.h"

#include "../fonts/StixMathFont.h"
#include "Theme.h"
#include "ThemeManager.h"

namespace ui {

lv_style_t style_math_primary;

static bool g_mathTypographyInited = false;

// STIX fallback used before the theme system is initialised (doc 13 Phase 1).
// Once ThemeManager exists its default is the numos theme, whose mathFonts
// member IS this same STIX set — so boot-order callers see identical glyphs.
static constexpr int16_t kStixPrimaryMathEmPx = 18;
static constexpr int16_t kStixScriptMathEmPx = 12;
static constexpr int16_t kStixScriptScriptMathEmPx = 8;

void initMathTypography() {
    if (!g_mathTypographyInited) {
        lv_style_init(&style_math_primary);
        lv_style_set_text_color(&style_math_primary, lv_color_black());
        lv_style_set_text_opa(&style_math_primary, LV_OPA_COVER);
        g_mathTypographyInited = true;
    }

    // The math font is a theme asset (doc 13): on every (re)init the style
    // font is re-read from the ACTIVE theme so a theme swap never leaks stale
    // glyphs into step containers. STIX is the pre-init fallback.
    const Theme& th = ThemeManager::instance().current();
    const lv_font_t* primary = th.mathFonts.primary;
    lv_style_set_text_font(&style_math_primary,
                           primary ? primary : &stix_math_18);
}

MathFontFace mathPrimaryFontFace() {
    const MathFontSet& mf = ThemeManager::instance().current().mathFonts;
    return { mf.primary ? mf.primary : &stix_math_18, mf.emPrimary };
}

MathFontFace mathScriptFontFace() {
    const MathFontSet& mf = ThemeManager::instance().current().mathFonts;
    return { mf.script ? mf.script : &stix_math_12, mf.emScript };
}

MathFontFace mathScriptScriptFontFace() {
    const MathFontSet& mf = ThemeManager::instance().current().mathFonts;
    return { mf.scriptScript ? mf.scriptScript : &stix_math_8,
             mf.emScriptScript };
}

const lv_font_t* mathPrimaryFont() {
    return mathPrimaryFontFace().font;
}

const lv_font_t* mathScriptFont() {
    return mathScriptFontFace().font;
}

const lv_font_t* mathScriptScriptFont() {
    return mathScriptScriptFontFace().font;
}

int16_t nominalMathEmSizeForFont(const lv_font_t* font) {
    if (font == &stix_math_18) return kStixPrimaryMathEmPx;
    if (font == &stix_math_12) return kStixScriptMathEmPx;
    if (font == &stix_math_8) return kStixScriptScriptMathEmPx;

    // Theme-driven faces: return their authored OpenType MATH design size.
    const MathFontSet& mf = ThemeManager::instance().current().mathFonts;
    if (font == mf.primary)       return mf.emPrimary;
    if (font == mf.script)        return mf.emScript;
    if (font == mf.scriptScript)  return mf.emScriptScript;

    return font ? static_cast<int16_t>(font->line_height)
                : kStixPrimaryMathEmPx;
}

} // namespace ui