/*
 * ThemeC.h — C-linkage wrappers over NumOS's C++ ThemeManager / ThemeFonts.
 *
 * EEZ Studio (and other visual designers) export plain C. NumOS themes are C++
 * (namespaces/classes). Generated .c files cannot call ui::ThemeManager directly,
 * so they go through these extern "C" accessors instead. They are live reads of
 * the active theme, exactly like the C++ accessors — a theme swap is reflected
 * on the next draw without regenerating.
 *
 * Add a new token here whenever the theme contract (src/ui/Theme.h) gains one
 * that generated code needs.
 */
#pragma once

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- colour tokens (RGB888) ---- */
uint32_t numos_theme_bg(void);
uint32_t numos_theme_bgCanvas(void);
uint32_t numos_theme_bgPane(void);
uint32_t numos_theme_bgHeader(void);
uint32_t numos_theme_bgToolbar(void);
uint32_t numos_theme_text(void);
uint32_t numos_theme_textDim(void);
uint32_t numos_theme_textOnAccent(void);
uint32_t numos_theme_accent(void);
uint32_t numos_theme_accentSecondary(void);
uint32_t numos_theme_border(void);
uint32_t numos_theme_focus(void);
uint32_t numos_theme_focusBorder(void);
uint32_t numos_theme_stepHighlight(void);
uint32_t numos_theme_result(void);
uint32_t numos_theme_danger(void);
uint32_t numos_theme_plot(int index);   /* 0..3, clamped */
uint32_t numos_theme_iconInk(void);

/* ---- chrome dials ---- */
uint8_t  numos_theme_cornerRadius(void);
uint8_t  numos_theme_borderWidth(void);
uint8_t  numos_theme_shadows(void);     /* 0/1 */

/* ---- fonts ---- */
const lv_font_t *numos_font_ui(void);
const lv_font_t *numos_font_uiSmall(void);
const lv_font_t *numos_font_uiXSmall(void);
const lv_font_t *numos_font_display(void);
const lv_font_t *numos_font_mono(void);

#ifdef __cplusplus
}
#endif
