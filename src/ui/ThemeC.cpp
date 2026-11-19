#include "ThemeC.h"
#include "ThemeManager.h"
#include "ThemeFonts.h"

extern "C" {

uint32_t numos_theme_bg(void)             { return ui::ThemeManager::instance().current().bg; }
uint32_t numos_theme_bgCanvas(void)       { return ui::ThemeManager::instance().current().bgCanvas; }
uint32_t numos_theme_bgPane(void)         { return ui::ThemeManager::instance().current().bgPane; }
uint32_t numos_theme_bgHeader(void)       { return ui::ThemeManager::instance().current().bgHeader; }
uint32_t numos_theme_bgToolbar(void)      { return ui::ThemeManager::instance().current().bgToolbar; }
uint32_t numos_theme_text(void)           { return ui::ThemeManager::instance().current().text; }
uint32_t numos_theme_textDim(void)        { return ui::ThemeManager::instance().current().textDim; }
uint32_t numos_theme_textOnAccent(void)   { return ui::ThemeManager::instance().current().textOnAccent; }
uint32_t numos_theme_accent(void)         { return ui::ThemeManager::instance().current().accent; }
uint32_t numos_theme_accentSecondary(void){ return ui::ThemeManager::instance().current().accentSecondary; }
uint32_t numos_theme_border(void)         { return ui::ThemeManager::instance().current().border; }
uint32_t numos_theme_focus(void)          { return ui::ThemeManager::instance().current().focus; }
uint32_t numos_theme_focusBorder(void)    { return ui::ThemeManager::instance().current().focusBorder; }
uint32_t numos_theme_stepHighlight(void)  { return ui::ThemeManager::instance().current().stepHighlight; }
uint32_t numos_theme_result(void)         { return ui::ThemeManager::instance().current().result; }
uint32_t numos_theme_danger(void)         { return ui::ThemeManager::instance().current().danger; }
uint32_t numos_theme_plot(int index) {
    if (index < 0) index = 0;
    if (index > 3) index = 3;
    return ui::ThemeManager::instance().current().plot[index];
}
uint32_t numos_theme_iconInk(void)        { return ui::ThemeManager::instance().current().iconInk; }

uint8_t numos_theme_cornerRadius(void)    { return ui::ThemeManager::instance().current().cornerRadius; }
uint8_t numos_theme_borderWidth(void)     { return ui::ThemeManager::instance().current().borderWidth; }
uint8_t numos_theme_shadows(void)         { return ui::ThemeManager::instance().current().shadows ? 1 : 0; }

const lv_font_t *numos_font_ui(void)       { return ui::fontUi(); }
const lv_font_t *numos_font_uiSmall(void) { return ui::fontUiSmall(); }
const lv_font_t *numos_font_uiXSmall(void){ return ui::fontUiXSmall(); }
const lv_font_t *numos_font_display(void) { return ui::fontDisplay(); }
const lv_font_t *numos_font_mono(void)    { return ui::fontMono(); }

} // extern "C"
