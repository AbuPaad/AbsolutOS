/*
 * ThemeFonts.h — the font half of the theme contract, as callable accessors.
 *
 * Rule: no app may name a font FACE. Every explicit text font in src/ reads one of
 * these, so switching theme switches typography with zero app edits (doc 06 sweep).
 * They are live reads of ThemeManager::current(), exactly like the colour tokens.
 *
 * The ladder, and why it is a ladder: the sources use four text sizes plus a mono
 * face. Collapsing them onto a single role would repaint the numos screens, which
 * must stay byte-identical, so each rung keeps its own size per theme.
 *   fontUi        body        numos 11 / casio 18   (math face, one face everywhere)
 *   fontUiSmall   secondary   numos 10 / casio 12
 *   fontUiXSmall  tertiary    numos  8 / casio 12
 *   fontDisplay   headline    numos 15 / casio 18   (no 26pt math rung)
 *   fontMono      editor/code unscii-8 (a real mono face, both themes)
 */

#pragma once

#include "ThemeManager.h"

namespace ui {

inline const lv_font_t* fontUi() {
    const lv_font_t* f = ThemeManager::instance().current().fontUi;
    return f ? f : LV_FONT_DEFAULT;
}
inline const lv_font_t* fontUiSmall() {
    const lv_font_t* f = ThemeManager::instance().current().fontUiSmall;
    return f ? f : fontUi();
}
inline const lv_font_t* fontUiXSmall() {
    const lv_font_t* f = ThemeManager::instance().current().fontUiXSmall;
    return f ? f : fontUiSmall();
}
inline const lv_font_t* fontDisplay() {
    const lv_font_t* f = ThemeManager::instance().current().fontDisplay;
    return f ? f : fontUi();
}
/*
 * fontSymbol() — the ONLY font allowed to carry LV_SYMBOL_* glyphs.
 *
 * The ladder faces are generated with --range 0x20-0x7F, so they contain no
 * FontAwesome codepoints: a symbol drawn in fontUi() renders as tofu. LVGL's
 * own built-in Montserrat is patched with the symbol range, so symbol-only
 * labels (arrows, battery, warning) must read this accessor instead of the
 * ladder. Do not use it for text.
 */
/*
 * fontLogotype() — the splash wordmark. One size above the ladder, ASCII only,
 * and the only caller should be SplashScreen: a display face at 15px is a label,
 * not a logo.
 */
inline const lv_font_t* fontLogotype() {
    return &lv_font_montserrat_26;
}
inline const lv_font_t* fontSymbol() {
    return &lv_font_montserrat_14;
}
inline const lv_font_t* fontMono() {
    const lv_font_t* f = ThemeManager::instance().current().fontMono;
    return f ? f : fontUi();
}

} // namespace ui
