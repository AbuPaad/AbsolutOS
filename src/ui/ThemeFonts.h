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
 *   fontUi        body        numos 14 / casio 18   (fontLcd)
 *   fontUiSmall   secondary   numos 12 / casio 12   (fontLcdSm)
 *   fontUiXSmall  tertiary    numos 10 / casio 12
 *   fontDisplay   headline    numos 20 / casio 26   (fontLcdLg)
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
inline const lv_font_t* fontMono() {
    const lv_font_t* f = ThemeManager::instance().current().fontMono;
    return f ? f : fontUi();
}

} // namespace ui
