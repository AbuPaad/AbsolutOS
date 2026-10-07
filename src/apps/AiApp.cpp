/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * AiApp.cpp — the AI wrapper UI. See the header for the keymap.
 *
 * Layout: title band at y=3, content container at y=STATUS_BAR_H with height
 * CONTENT_H — the same shape as NotesApp, so the bottom 40 px stay black for
 * the fx-82 shell crop and this app's screenshots line up with the reader's.
 *
 * Colours and fonts are THEME tokens, never literals (agents.md §5.6). The app
 * asks `ui::appSurface(ui::appid::kAi)`: under the numos theme that resolves to
 * the dark palette this screen has always had; under casio it inherits the
 * light LCD tokens. Nothing in this file branches on the theme id.
 *
 * Casio (SPEC-stageC C2) also changes the SHAPE, not just the palette, and it
 * comes from the interaction model, not from a theme check:
 *   · no focus rectangle — `interaction().focusFill == false`, so a focused row
 *     changes its INK only;
 *   · a digit key 1..9 selects and activates that row directly, no cursor walk;
 *   · a bottom softkey band (`interaction().softkeyRow`), which shortens the
 *     content area by kSoftkeyH.
 *
 * The screen never scrolls as a whole. List screens put their rows in a
 * scrollable viewport (with the numos look unchanged: no scrollbar appears
 * while the rows fit), so nothing is unreachable — the Settings screen used to
 * lose its last two rows off the bottom of the 132 px content box.
 */

#include "apps/AiApp.h"

#include "../ui/ThemeFonts.h"
#include "../ui/ThemeManager.h"

#include <cstdarg>
#include <cstdio>

#include "ai/AiModelIcons.h"
#include "ai/ModelCatalog.h"

using mdrender::CONTENT_H;
using mdrender::SCREEN_H;
using mdrender::SCREEN_W;
using mdrender::STATUS_BAR_H;

namespace {

constexpr int ROW_H      = 26;
constexpr int PAD        = 6;
constexpr int kSoftkeyH  = 18;   ///< casio bottom band height
/// The model picker's row geometry comes from the interaction profile: the TALL
/// variant (casio) gives the row enough height for the 1.25x provider mark and
/// shows exactly three whole rows — at the shared 26 px pitch the viewport (~88 px)
/// cut a fourth row in half, which is the "it looks clipped" report. numos keeps
/// the shared pitch, the 2 px padding, 1x marks and a box-filling list.
constexpr int MODEL_ROW_H        = 29;   // tall pitch (casio)
constexpr int MODEL_ROWS_VISIBLE = 3;    // whole rows the tall viewport shows
/// LVGL image scale: 256 = 1x.
constexpr int ICON_SCALE_1X      = 256;
constexpr int ICON_SCALE_125     = 320;  // 1.25x

/// Settings rows. Keep in step with the array in showSettings().
constexpr int kSettingsRows = 6;

/// "3:Question" — the launcher's slot-number idiom, applied to a list row.
std::string numbered(int n, const char* text) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d:%s", n, text);
    return std::string(buf);
}

/**
 * The focus treatment for one list row. With `focusFill` the FILL is the focus
 * (numos). Without it (casio) the focus is a perimeter OUTLINE in the accent
 * colour and NO fill — the row keeps its transparent background and gains a
 * hairline border, so the cursor reads as a frame around the row, not a card.
 * Shared by every row builder and by applyFocus, so the two cannot drift.
 */
void applyRowFocus(lv_obj_t* row, bool focused, bool focusFill,
                   uint32_t accent, uint32_t rowFocus) {
    lv_obj_set_style_bg_opa(row, (focused && focusFill) ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(rowFocus), LV_PART_MAIN);
    if (!focusFill) {
        lv_obj_set_style_border_width(row, focused ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(accent), LV_PART_MAIN);
    }
}

/**
 * LVGL holds the lv_image_dsc_t pointer for as long as the image lives, so these
 * have to outlive every row that shows one. Filled once from the generated blobs;
 * the app is single-instance, so file scope is the right home.
 */
lv_image_dsc_t g_modelIconDsc[ai::icons::kProviderCount];
bool           g_modelIconDscReady = false;

void ensureModelIconDscs() {
    if (g_modelIconDscReady) return;
    for (int i = 0; i < ai::icons::kProviderCount; ++i) {
        lv_image_dsc_t& d = g_modelIconDsc[i];
        d = lv_image_dsc_t{};
        d.header.magic  = LV_IMAGE_HEADER_MAGIC;
        d.header.cf     = LV_COLOR_FORMAT_RGB565A8;   // RGB565 plane then A8 alpha
        d.header.flags  = 0;
        d.header.w      = ai::icons::kIconW;
        d.header.h      = ai::icons::kIconH;
        d.header.stride = ai::icons::kIconStride;
        d.data_size     = ai::icons::kIconDataSize;
        d.data = ai::icons::providerIcon(static_cast<ai::icons::Provider>(i),
                                        ai::icons::IconSize::Compact);
    }
    g_modelIconDscReady = true;
}

const char* const kMenuItems[] = {"Ask", "Capture", "Recent answers", "Settings"};
constexpr int     kMenuCount   = 4;

/// Prefilled Ask question. The pad has no letter keys, so arbitrary prose needs
/// the on-screen KeyboardManager (a later pass) and the question is a compiled
/// sample. This one is deliberately heavy AND deliberately structured: a redox
/// balance alone came back as a single page (the model stops as soon as the
/// simplest reading of the question is satisfied), so the question names the
/// sections outright. That is what actually exercises the page-break rule,
/// multi-page pagination and the fell-swoop commit.
const char* const kAskSample =
    "Balance the redox equation MnO4^- + Fe^2+ + H^+ -> Mn^2+ + Fe^3+ + H2O. "
    "Put EACH of these in its own page, separated by a line containing only ---: "
    "1) oxidation states of every element; 2) the reduction half-reaction with its "
    "electrons; 3) the oxidation half-reaction with its electrons; 4) the electron "
    "balance that makes them cancel; 5) the final balanced equation, then a Check "
    "line counting atoms on both sides.";

/// Calculator-keypad keys as text. The pad has no letters, so maths questions
/// are typeable natively and arbitrary prose needs the on-screen KeyboardManager
/// (next pass).
bool keyToText(KeyCode kc, std::string& out) {
    switch (kc) {
        case KeyCode::NUM_0: out = "0"; return true;
        case KeyCode::NUM_1: out = "1"; return true;
        case KeyCode::NUM_2: out = "2"; return true;
        case KeyCode::NUM_3: out = "3"; return true;
        case KeyCode::NUM_4: out = "4"; return true;
        case KeyCode::NUM_5: out = "5"; return true;
        case KeyCode::NUM_6: out = "6"; return true;
        case KeyCode::NUM_7: out = "7"; return true;
        case KeyCode::NUM_8: out = "8"; return true;
        case KeyCode::NUM_9: out = "9"; return true;
        case KeyCode::DOT:    out = "."; return true;
        case KeyCode::LPAREN: out = "("; return true;
        case KeyCode::RPAREN: out = ")"; return true;
        case KeyCode::ADD:    out = "+"; return true;
        case KeyCode::SUB:    out = "-"; return true;
        case KeyCode::MUL:    out = "*"; return true;
        case KeyCode::DIV:    out = "/"; return true;
        case KeyCode::POW:    out = "^"; return true;
        case KeyCode::VAR_X:  out = "x"; return true;
        case KeyCode::VAR_Y:  out = "y"; return true;
        case KeyCode::ALPHA_A: out = "A"; return true;
        case KeyCode::ALPHA_B: out = "B"; return true;
        case KeyCode::ALPHA_C: out = "C"; return true;
        case KeyCode::ALPHA_D: out = "D"; return true;
        case KeyCode::ALPHA_E: out = "E"; return true;
        case KeyCode::ALPHA_F: out = "F"; return true;
        case KeyCode::SQRT:   out = "sqrt("; return true;
        default: return false;
    }
}

}  // namespace

AiApp::AiApp() {}
AiApp::~AiApp() { end(); }

// ═══════════════════════════════════════════════════════════════════════════
// Chrome
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Resolve everything the chrome needs from the theme + interaction model. This
 * is the ONLY place the app reads them, so a theme swap changes the whole screen
 * through one function (SystemApp reloads the app on a toggle, so it re-runs).
 */
void AiApp::readSurface() {
    _sc = ui::appSurface(ui::appid::kAi);

    const ui::InteractionModel& im = ui::ThemeManager::instance().interaction();
    _softkeys  = im.softkeyRow;
    _focusFill = im.focusFill;
    _numbered  = im.numberedSlots;
    _chevron   = im.scrollChevron;
    _tallRows  = im.tallListRows;
    _contentH  = CONTENT_H - (_softkeys ? kSoftkeyH : 0);

    // The answer renderer paginates against the SAME box the app gives it, and
    // it draws on the SAME surface. Without this the module's dark defaults
    // (colorText 0xFFFFFF) paint white-on-light under casio — an invisible page.
    _styles.contentH  = _contentH;
    _styles.bodyFont    = ui::fontUi();
    _styles.headingFont = ui::fontUi();
    // codeFont/mathFont stay null on purpose: the module's fallback resolves a
    // null code face to Montserrat at codeSize, which is exactly what the numos
    // pages have always rendered. The math face is set by the math-wiring pass.
    _styles.colorText      = _sc.text;
    _styles.colorDim       = _sc.textDim;
    _styles.colorAccent    = _sc.accent;
    _styles.colorQuote     = _sc.textDim;
    _styles.colorWikilink  = _sc.accent;
    _styles.colorCodeBg    = _sc.pane;
}

void AiApp::begin() {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(ui::appSurface(ui::appid::kAi).bg),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
}

void AiApp::buildChrome() {
    clearContent();
    readSurface();

    _title = lv_label_create(_screen);
    lv_obj_set_style_text_color(_title, lv_color_hex(_sc.title), LV_PART_MAIN);
    lv_obj_set_style_text_font(_title, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_pos(_title, PAD, 3);

    _content = lv_obj_create(_screen);
    lv_obj_set_pos(_content, 0, STATUS_BAR_H);
    lv_obj_set_size(_content, SCREEN_W, _contentH);
    lv_obj_set_style_bg_opa(_content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_content, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(_content, true, LV_PART_MAIN);
    lv_obj_remove_flag(_content, LV_OBJ_FLAG_SCROLLABLE);

    if (_softkeys) buildSoftkey(softkeyLabel());
}

void AiApp::clearContent() {
    _rows.clear();
    _live = nullptr;
    // Both live on _screen (not _content), so losing the pointer would leak the
    // widget on screen for the next view: delete them here, rebuild in the
    // builders that want them.
    if (_softkey)     { lv_obj_delete(_softkey);     _softkey     = nullptr; }
    if (_scrollUp)    { lv_obj_delete(_scrollUp);    _scrollUp    = nullptr; }
    if (_scrollDown)  { lv_obj_delete(_scrollDown);  _scrollDown  = nullptr; }
    // The renderer's page object LIVES IN _content, which dies two lines down.
    // Without this the renderer keeps a dangling pointer and the next render()
    // deletes a freed object — the crash that made the check screen unusable the
    // second time it drew a page.
    _renderer.releasePage();
    _list = nullptr;   // child of _content, so deleting _content deletes it
    if (_content) { lv_obj_delete(_content); _content = nullptr; }
    if (_title)   { lv_obj_delete(_title);   _title   = nullptr; }
}

void AiApp::setTitle(const char* fmt, ...) {
    if (!_title) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(_title, buf);
}

/** The casio softkey band. Never built for a profile with softkeyRow == false. */
void AiApp::buildSoftkey(const char* label) {
    _softkey = lv_label_create(_screen);
    lv_label_set_text(_softkey, label);
    lv_obj_set_style_text_font(_softkey, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_softkey, lv_color_hex(_sc.textDim), LV_PART_MAIN);
    lv_obj_set_pos(_softkey, PAD, SCREEN_H - kSoftkeyH + 3);
    // The band sits below the content box, but a list viewport created after it
    // would otherwise be a later sibling and draw over it.
    lv_obj_move_foreground(_softkey);
}

const char* AiApp::softkeyLabel() const {
    switch (_view) {
        case Screen::Ask:     return "EXE send   DEL erase   AC back";
        case Screen::Result:
            // The check is offered only where it can run, so the band says
            // nothing about it otherwise.
            return checkAvailable() ? "7 check   <- -> page   AC back"
                                    : "<- -> page      AC back";
        case Screen::Check:   return _edit.edited() ? "AC undo   EXE check" : "AC back   EXE check";
        case Screen::Verify:  return "<- -> page      AC back";
        case Screen::Models:  return "AC back";   // the hint line carries the actions
        case Screen::Settings:return "EXE change   AC back";
        case Screen::Menu:    return "AC exit";
        default:              return "AC back";   // rows carry their own digits now
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Rows — one style shared by Menu / Capture / Recent / Settings / Models
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Open the scrollable viewport every list screen draws into. Sizing it to the
 * content box and letting LVGL draw a scrollbar only while the rows overflow
 * keeps the numos screens pixel-identical (they never overflow) while making the
 * longer casio lists reachable.
 */
void AiApp::beginList(int topInset, int height) {
    if (!_content) return;
    _list = lv_obj_create(_content);
    // The inset (the Models hint band) is not scrollable content: the list starts
    // below it and gives up exactly that much height, which is what keeps the
    // hint off the last visible row. `height` pins the viewport to whole rows.
    lv_obj_set_size(_list, SCREEN_W, height > 0 ? height : _contentH - topInset);
    lv_obj_set_pos(_list, 0, topInset);
    lv_obj_set_style_bg_opa(_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_list, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);

    // Corner scroll hint — the theme face's own arrow glyphs (U+2191/U+2193), the
    // same ink and the same top-right corner the calculator puts its history hint
    // in. The pair sits on ONE hint line (the glyph's line box is 21 px, not the
    // 12 px mask's) so the column stays as shallow as the bitmap pair was; each
    // half is still shown independently by updateScrollChevron().
    if (_chevron && !_scrollUp) {
        _scrollUp = lv_label_create(_screen);
        lv_obj_set_style_text_font(_scrollUp, ui::fontUiSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_scrollUp, lv_color_hex(_sc.text), LV_PART_MAIN);
        lv_obj_set_style_text_opa(_scrollUp, LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_text(_scrollUp, "\xE2\x86\x91");   // ↑
        lv_obj_add_flag(_scrollUp, LV_OBJ_FLAG_HIDDEN);

        _scrollDown = lv_label_create(_screen);
        lv_obj_set_style_text_font(_scrollDown, ui::fontUiSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_scrollDown, lv_color_hex(_sc.text), LV_PART_MAIN);
        lv_obj_set_style_text_opa(_scrollDown, LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_text(_scrollDown, "\xE2\x86\x93");   // ↓
        lv_obj_align(_scrollDown, LV_ALIGN_TOP_RIGHT, -PAD, 2);
        lv_obj_add_flag(_scrollDown, LV_OBJ_FLAG_HIDDEN);

        // ↑ to the left of ↓, on the same line: "↑↓ scrollable".
        lv_obj_update_layout(_scrollDown);
        lv_obj_align(_scrollUp, LV_ALIGN_TOP_RIGHT,
                     -(PAD + lv_obj_get_width(_scrollDown) + 1), 2);
    }
}

/** Show the corner up/down pair when — and only when — the list can move. */
void AiApp::updateScrollChevron() {
    if (!_scrollUp || !_scrollDown) return;
    if (!_list) {
        lv_obj_add_flag(_scrollUp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_scrollDown, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_update_layout(_list);
    // lv_obj_get_scroll_y() is the live offset: 0 at the top, negative once
    // scrolled down. (lv_obj_get_scroll_top() did NOT give the offset here, so
    // the up arrow read as "available" even at the top.)
    const int y     = lv_obj_get_scroll_y(_list);
    const int below = lv_obj_get_scroll_bottom(_list);   // >0 when more content below

    // Each half is simply ON when there is something to scroll that way and OFF
    // otherwise: down shows when content continues below, up shows once the list
    // has been scrolled (something above). Mid-list both are on; at an end only
    // one is. No dimming — off means hidden.
    if (below > 0) lv_obj_remove_flag(_scrollDown, LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(_scrollDown, LV_OBJ_FLAG_HIDDEN);

    if (y < 0) lv_obj_remove_flag(_scrollUp, LV_OBJ_FLAG_HIDDEN);
    else       lv_obj_add_flag(_scrollUp, LV_OBJ_FLAG_HIDDEN);
}

void AiApp::scrollListIntoView() {
    if (!_list) return;
    if (_focus >= 0 && _focus < static_cast<int>(_rows.size()))
        lv_obj_scroll_to_view(_rows[static_cast<size_t>(_focus)], LV_ANIM_OFF);
    updateScrollChevron();
}

lv_obj_t* AiApp::addRow(const char* text, int index, bool focused) {
    lv_obj_t* parent = _list ? _list : _content;
    if (!parent) return nullptr;

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * PAD, ROW_H - 2);
    lv_obj_set_pos(row, PAD, index * ROW_H);
    lv_obj_set_style_radius(row, _sc.radiusRow, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 2, LV_PART_MAIN);
    applyRowFocus(row, focused, _focusFill, _sc.accent, _sc.rowFocus);

    lv_obj_t* lab = lv_label_create(row);
    // Casio numbers list rows the way the launcher numbers its slots ("1:COMP"),
    // so the row IS the hint and no "1-4 open" line is needed. numos: no prefix.
    char nb[128];
    if (_numbered) { std::snprintf(nb, sizeof(nb), "%d:%s", index + 1, text); text = nb; }
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(rowTextColor(focused)),
                                LV_PART_MAIN);
    // Ellipsise instead of hard-clipping at the row edge: the Casio LCD face is
    // wider than Montserrat, so Settings values that fit under numos do not fit
    // under casio. LONG_DOT is a no-op while the text fits, so the numos pixels
    // are unchanged.
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lab, SCREEN_W - 2 * PAD - 12);
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

    _rows.push_back(row);
    return row;
}

/**
 * A settings row as label (left) + value (right). Casio only — under the wider
 * LCD face a space-padded single string ("model     google/gemini…") does not
 * line up, so the halves are separate labels and the value is right-aligned on
 * the smaller rung, which keeps a long value readable in the right half.
 */
lv_obj_t* AiApp::addRowKV(const char* label, const char* value, int index, bool focused) {
    lv_obj_t* parent = _list ? _list : _content;
    if (!parent) return nullptr;

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * PAD, ROW_H - 2);
    lv_obj_set_pos(row, PAD, index * ROW_H);
    lv_obj_set_style_radius(row, _sc.radiusRow, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 2, LV_PART_MAIN);
    applyRowFocus(row, focused, _focusFill, _sc.accent, _sc.rowFocus);

    char nb[40];
    if (_numbered) { std::snprintf(nb, sizeof(nb), "%d:%s", index + 1, label); label = nb; }

    const int halfW = (SCREEN_W - 2 * PAD) / 2 - 6;

    lv_obj_t* key = lv_label_create(row);
    lv_label_set_text(key, label);
    lv_obj_set_style_text_font(key, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(key, lv_color_hex(rowTextColor(focused)),
                                LV_PART_MAIN);
    lv_obj_set_width(key, halfW);
    lv_label_set_long_mode(key, LV_LABEL_LONG_DOT);
    lv_obj_align(key, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(val, lv_color_hex(rowTextColor(focused)),
                                LV_PART_MAIN);
    lv_obj_set_width(val, halfW);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

    _rows.push_back(row);
    return row;
}

void AiApp::applyFocus(int index, int count) {
    for (int i = 0; i < static_cast<int>(_rows.size()); ++i) {
        lv_obj_t* row = _rows[static_cast<size_t>(i)];
        const bool f = (i == index) && i < count;

        // Focus is a filled card only when the interaction profile says so
        // (numos); casio shows it as an accent perimeter — never a fill.
        applyRowFocus(row, f, _focusFill, _sc.accent, _sc.rowFocus);

        // Recolour EVERY text child, not just the first: a casio settings row is
        // a label + a right-aligned value, and recolouring only child 0 left the
        // value in the unfocused ink. numos rows have one label, so this is a
        // no-op there.
        const uint32_t kids = lv_obj_get_child_count(row);
        for (uint32_t c = 0; c < kids; ++c) {
            lv_obj_t* ch = lv_obj_get_child(row, static_cast<int32_t>(c));
            if (ch && lv_obj_check_type(ch, &lv_label_class)) {
                lv_obj_set_style_text_color(ch, lv_color_hex(rowTextColor(f)),
                                            LV_PART_MAIN);
            }
        }
    }
    scrollListIntoView();
}

int AiApp::currentListSize() const {
    switch (_view) {
        case Screen::Menu:     return kMenuCount;
        case Screen::Capture:  return static_cast<int>(_files.size());
        case Screen::Recent:   return static_cast<int>(_results.size());
        case Screen::Settings: return kSettingsRows;
        case Screen::Models:   return ai::kModelCount;
        default:               return 0;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Screens
// ═══════════════════════════════════════════════════════════════════════════

void AiApp::showMenu() {
    _view  = Screen::Menu;
    _focus = 0;
    buildChrome();
    setTitle("AI");
    beginList();
    for (int i = 0; i < kMenuCount; ++i) addRow(kMenuItems[i], i, i == 0);

    // A failed run lands back here, so the reason has to be visible AND logged:
    // a silent fallback to the menu is exactly how a broken save path hides.
    if (_status.empty()) _status = "model: " + _cfg.model;
    lv_obj_t* foot = lv_label_create(_list ? _list : _content);
    lv_label_set_long_mode(foot, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(foot, SCREEN_W - 2 * PAD);
    lv_label_set_text(foot, _status.c_str());
    lv_obj_set_style_text_font(foot, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(foot, lv_color_hex(_sc.textDim), LV_PART_MAIN);
    lv_obj_set_pos(foot, PAD, _softkeys ? (_contentH - 16) : (kMenuCount * ROW_H + 6));
    std::printf("[AI] menu status: %s\n", _status.c_str());
}

void AiApp::showAsk() {
    _view = Screen::Ask;
    buildChrome();
    setTitle("AI  Ask");

    if (_question.empty()) _question = kAskSample;

    // The Ask box grows to the content box on a profile that reserves a softkey
    // band; the numos geometry (96 / hint at 110) is untouched.
    const int boxH  = _softkeys ? (_contentH - 26) : 96;
    const int hintY = _softkeys ? (_contentH - 15) : 110;

    lv_obj_t* box = lv_obj_create(_content);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, SCREEN_W - 2 * PAD, boxH);
    lv_obj_set_pos(box, PAD, PAD);
    lv_obj_set_style_radius(box, _sc.radiusPane, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, _sc.borderWidth, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(_sc.accent), LV_PART_MAIN);
    lv_obj_set_style_bg_color(box, lv_color_hex(_sc.pane), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 6, LV_PART_MAIN);

    lv_obj_t* lab = lv_label_create(box);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lab, SCREEN_W - 4 * PAD);
    lv_label_set_text(lab, _question.c_str());
    lv_obj_set_style_text_font(lab, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(_sc.text), LV_PART_MAIN);

    lv_obj_t* hint = lv_label_create(_content);
    lv_label_set_text(hint, "EXE send   DEL rub out   AC back");
    lv_obj_set_style_text_font(hint, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(_sc.textDim), LV_PART_MAIN);
    lv_obj_set_pos(hint, PAD, hintY);
}

void AiApp::showCapture() {
    _view  = Screen::Capture;
    _focus = 0;
    buildChrome();
    setTitle("AI  Capture");

    _files = ai::listFiles(_cfg.promptsDir, ".jpg");
    if (_files.empty()) {
        lv_obj_t* lab = lv_label_create(_content);
        lv_label_set_text(lab, "No images in the prompts folder.");
        lv_obj_set_style_text_color(lab, lv_color_hex(_sc.textDim), LV_PART_MAIN);
        lv_obj_set_style_text_font(lab, ui::fontUi(), LV_PART_MAIN);
        lv_obj_set_pos(lab, PAD, PAD);
        return;
    }
    beginList();
    for (int i = 0; i < static_cast<int>(_files.size()); ++i)
        addRow(_files[static_cast<size_t>(i)].c_str(), i, i == 0);
}

void AiApp::showStreaming() {
    _view = Screen::Streaming;
    buildChrome();
    setTitle("AI  asking...");

    _live = lv_label_create(_content);
    lv_label_set_long_mode(_live, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_live, SCREEN_W - 2 * PAD);
    lv_obj_set_pos(_live, PAD, PAD);
    lv_obj_set_style_text_font(_live, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_live, lv_color_hex(_sc.text), LV_PART_MAIN);
    lv_label_set_text(_live, "\u2026");
}

void AiApp::showResult() {
    _view = Screen::Result;
    buildChrome();
    rebuildResult();
}

void AiApp::rebuildResult() {
    const int n = _renderer.pageCount();
    const bool clipped = _renderer.pageTruncated(_page);
    // The title band is the app's own chrome, so the one affordance this screen
    // needs rides here rather than in a status line the content box has no room
    // for: the answer proposes a check, and key 7 is how the user spends it.
    const char* check = checkAvailable() ? "  7 check" : "";
    setTitle((std::string(clipped ? "AI  %d/%d  clipped" : "AI  %d/%d") + check).c_str(),
             _page + 1, n > 0 ? n : 1);
    if (_content) _renderer.render(_page, _content, _styles);
}

// ═══════════════════════════════════════════════════════════════════════════
// The Wolfram|Alpha check — confirm, then the result
// ═══════════════════════════════════════════════════════════════════════════

/**
 * What the answer can be checked with. Three gates, and any one of them closed
 * means the check is NOT OFFERED (no affordance, key 7 does nothing):
 *   · the model proposed a query — an empty one is the common case;
 *   · it is for `wolfram`, the only server implemented: dispatch is by name, so
 *     an unknown one is refused rather than sent somewhere we cannot describe;
 *   · an AppID is configured, because BYO is the only allowance that may pay for
 *     a call — no compiled default, no relay, no fleet allowance.
 */
bool AiApp::checkAvailable() const {
    if (_toolQuery.empty()) return false;
    if (_toolServer != "wolfram") return false;
    return !_cfg.waAppId.empty();
}

/**
 * Read the answer's check fields from its saved `%%ai:` line.
 *
 * The FILE is the single source of truth, not the live scanner: commit() writes
 * the fields with the answer, so the run that just finished and an answer
 * reopened from Recent take exactly the same path here. An answer with no such
 * line (one saved before this hop existed) simply offers no check.
 */
void AiApp::loadCheckFields(const std::string& path) {
    _toolQuery.clear();
    _toolServer.clear();
    _transcribed.clear();
    _sourceSlug.clear();

    if (path.empty()) return;

    const size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (base.size() > 3 && base.compare(base.size() - 3, 3, ".md") == 0)
        base.erase(base.size() - 3);
    _sourceSlug = base;

    const ai::AnswerMeta meta = ai::readAnswerMeta(path);
    _toolQuery   = meta.toolQuery;
    _toolServer  = meta.toolServer;
    _transcribed = meta.transcribed;

    // The query is the user's own string, not a credential, so it is safe to log
    // — and this line is how a scripted run shows whether the gate opened.
    std::printf("[AI] check fields: server=%s offered=%d query='%s'\n",
                _toolServer.empty() ? "-" : _toolServer.c_str(),
                checkAvailable() ? 1 : 0, _toolQuery.c_str());
}

/**
 * The check screen. It exists to spend ONE Wolfram|Alpha call deliberately, so
 * it shows everything that decision needs and nothing else: what the model
 * proposed, and what the model believes it read off the photo. The transcription
 * is the part that matters — a misread expression is computed correctly for the
 * wrong problem, so the user is checking the reading, not the arithmetic.
 *
 * The query is editable with digits ONLY (the keypad has no letters): the caret
 * is drawn in the text as `|`, because a second label for a caret cannot follow
 * a wrapped line.
 */
void AiApp::showCheck() {
    _view = Screen::Check;
    buildChrome();
    setTitle("AI  check");
    if (!_content) return;

    lv_obj_t* cap = lv_label_create(_content);
    lv_label_set_text(cap, "the model read it as");
    lv_obj_set_style_text_font(cap, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(cap, lv_color_hex(_sc.textDim), LV_PART_MAIN);
    lv_obj_set_pos(cap, PAD, 0);

    lv_obj_t* tr = lv_label_create(_content);
    lv_label_set_long_mode(tr, LV_LABEL_LONG_DOT);
    lv_obj_set_width(tr, SCREEN_W - 2 * PAD);
    lv_obj_set_height(tr, 28);
    lv_label_set_text(tr, _transcribed.empty() ? "-" : _transcribed.c_str());
    lv_obj_set_style_text_font(tr, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(tr, lv_color_hex(_sc.text), LV_PART_MAIN);
    lv_obj_set_pos(tr, PAD, 12);

    // The query, in the same bordered pane the Ask screen uses.
    const int paneY = 42;
    const int paneH = _contentH - paneY - 28;
    lv_obj_t* pane = lv_obj_create(_content);
    lv_obj_remove_flag(pane, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pane, SCREEN_W - 2 * PAD, paneH > 18 ? paneH : 18);
    lv_obj_set_pos(pane, PAD, paneY);
    lv_obj_set_style_radius(pane, _sc.radiusPane, LV_PART_MAIN);
    lv_obj_set_style_border_width(pane, _sc.borderWidth, LV_PART_MAIN);
    lv_obj_set_style_border_color(pane, lv_color_hex(_sc.accent), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pane, lv_color_hex(_sc.pane), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pane, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pane, 5, LV_PART_MAIN);

    std::string shown = _edit.text();
    const int caret = _edit.caret();
    if (caret >= 0 && caret <= static_cast<int>(shown.size()))
        shown.insert(shown.begin() + caret, '|');

    lv_obj_t* q = lv_label_create(pane);
    lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(q, SCREEN_W - 4 * PAD);
    lv_label_set_text(q, shown.c_str());
    lv_obj_set_style_text_font(q, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(q, lv_color_hex(_sc.text), LV_PART_MAIN);

    // Two hint lines, and the second one moves with the state: AC means "undo"
    // while there is an edit to undo and "back" once there is not, which is the
    // only exit this screen has (MODE leaves the whole app).
    lv_obj_t* h1 = lv_label_create(_content);
    lv_label_set_text(h1, "digits edit   DEL rub   <- -> caret");
    lv_obj_set_style_text_font(h1, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(h1, lv_color_hex(_sc.textDim), LV_PART_MAIN);
    lv_obj_set_pos(h1, PAD, paneY + (paneH > 18 ? paneH : 18) + 1);

    lv_obj_t* h2 = lv_label_create(_content);
    lv_label_set_text(h2, _edit.edited() ? "AC undo the edit   EXE check"
                                         : "AC back   EXE check");
    lv_obj_set_style_text_font(h2, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(h2, lv_color_hex(_sc.textDim), LV_PART_MAIN);
    lv_obj_set_pos(h2, PAD, _contentH - 15);
}

/** EXE on the check screen: the keypress IS the quota gate. */
void AiApp::startCheck() {
    _page      = 0;
    _waSaved   = false;
    _waNote.clear();
    _checkText.clear();
    const bool opened = _wa.begin(_cfg, _edit.text());
    std::printf("[AI] check '%s': %s\n", _edit.text().c_str(),
                opened ? "running" : _wa.error().c_str());
    showVerify();
}

void AiApp::showVerify() {
    _view = Screen::Verify;
    buildChrome();
    setTitle("AI  check");
    if (!_content) return;

    if (_wa.finished()) {
        // begin() fails BEFORE a socket exists for every named prerequisite (no
        // AppID, no Wi-Fi, unsynced clock), and the screen still has to say why
        // rather than sit blank.
        if (_wa.failed()) {
            _waNote = _wa.error();
            _checkText = buildCheckText();
            renderText(_checkText);
        }
        rebuildVerify();
        return;
    }

    _live = lv_label_create(_content);
    lv_label_set_long_mode(_live, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_live, SCREEN_W - 2 * PAD);
    lv_obj_set_pos(_live, PAD, PAD);
    lv_obj_set_style_text_font(_live, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_live, lv_color_hex(_sc.text), LV_PART_MAIN);
    lv_label_set_text(_live, "checking Wolfram|Alpha\u2026");
}

void AiApp::rebuildVerify() {
    // The live card and the paginated view are the same container: drop the
    // label before the renderer draws into it.
    if (_live) { lv_obj_delete(_live); _live = nullptr; }

    const int n = _renderer.pageCount();
    if (_wa.failed()) setTitle("AI  check  failed");
    else              setTitle("AI  check  %d/%d", _page + 1, n > 0 ? n : 1);
    if (_content) _renderer.render(_page, _content, _styles);
}

/**
 * The display text for a finished check. RAM ONLY — this is Wolfram's content
 * and the API terms prohibit caching it, so it is composed, rendered and dropped;
 * nothing on this path reaches the filesystem. The one thing that is written is
 * the sibling `<slug>_Wolfram.md`, which carries the query, our own
 * transcription and the results-page LINK (an obligation of the same terms), and
 * never a word of WA's answer.
 */
std::string AiApp::buildCheckText() const {
    std::string out;
    if (_wa.failed()) {
        out += "# Wolfram|Alpha check failed\n\n";
        out += _waNote.empty() ? std::string("no reason reported") : _waNote;
        out += "\n";
        return out;
    }

    const ai::WolframResult& r = _wa.result();
    out += "# Wolfram|Alpha check\n\n";
    out += "**Query:** " + _wa.query() + "\n\n";
    if (!_transcribed.empty())
        out += "_Read from the photo as:_ " + _transcribed + "\n\n";
    if (!r.interpretation.empty())
        out += "**Wolfram read it as:** " + r.interpretation + "\n\n";
    if (!r.body.empty()) {
        out += r.body;
        out += "\n\n";
    }
    if (!r.link.empty()) {
        // Its own page: the link is what the terms require the user to be able
        // to reach, and a page is the atomic unit on this device.
        out += "---\n\nWolfram|Alpha results page:\n\n";
        out += r.link;
        out += "\n";
    }
    return out;
}

/** One frame of the check. The card shows what has arrived, then paginates. */
void AiApp::pumpCheck() {
    if (_wa.finished()) return;

    const bool more = _wa.pump();

    if (_live) {
        const char* p = _wa.rawData();
        const size_t n = _wa.rawSize();
        // Tail-bounded: the label redraws every frame, and the interesting part
        // of a body arriving is the end of it.
        constexpr size_t kShow = 700;
        if (p && n > 0) {
            const size_t off = (n > kShow) ? (n - kShow) : 0;
            lv_label_set_text(_live, std::string(p + off, n - off).c_str());
        } else {
            lv_label_set_text(_live, "checking Wolfram|Alpha\u2026");
        }
    }

    if (more) return;

    if (_wa.failed()) {
        _waNote = _wa.error();
        std::printf("[AI] check failed: %s\n", _waNote.c_str());
    } else {
        const std::string doc = ai::buildWolframDoc(_sourceSlug, _wa.query(),
                                                    _transcribed,
                                                    _wa.result().link, _cfg.model);
        std::string path;
        _waSaved = ai::saveWolframDoc(_cfg, _sourceSlug, doc, &path);
        _waNote  = _waSaved ? ("check file: " + path)
                            : std::string("the check file was not saved");
        std::printf("[AI] check done (%zu bytes): %s\n", _wa.bytes(), _waNote.c_str());
    }

    _checkText = buildCheckText();
    renderText(_checkText);
    rebuildVerify();
}

/** Parse+layout+paginate+render a markdown string held in RAM. */
void AiApp::renderText(const std::string& text) {
    mdrender::BufferSource src(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    _renderer.parse(src);
    _renderer.layout(_metrics, _styles);
    _renderer.paginate(_styles);
    _page = 0;
}

void AiApp::showRecent() {
    _view  = Screen::Recent;
    _focus = 0;
    buildChrome();
    setTitle("AI  Recent answers");

    _results = ai::listRecent(_cfg.resultsDir, 12);
    if (_results.empty()) {
        lv_obj_t* lab = lv_label_create(_content);
        lv_label_set_text(lab, "Nothing saved yet.");
        lv_obj_set_style_text_color(lab, lv_color_hex(_sc.textDim), LV_PART_MAIN);
        lv_obj_set_style_text_font(lab, ui::fontUi(), LV_PART_MAIN);
        lv_obj_set_pos(lab, PAD, PAD);
        return;
    }
    beginList();
    for (int i = 0; i < static_cast<int>(_results.size()); ++i)
        addRow(_results[static_cast<size_t>(i)].c_str(), i, i == 0);
}

void AiApp::showSettings() {
    const bool returning = (_view == Screen::Models);
    _view = Screen::Settings;
    // Coming back from the picker, leave the cursor on the model row so ENTER
    // re-opens it; entering fresh, start at the top.
    if (!returning || _focus >= kSettingsRows) _focus = 0;
    buildChrome();
    // The row itself shows the new id when a pick lands, so the only thing worth
    // a second line is a FAILED write — and there is no room for a second line:
    // the row list is now scrollable, but the title is the one line guaranteed
    // to be on screen, so the flag goes there.
    setTitle(_saveFailed ? "AI  Settings  ! not saved" : "AI  Settings");

    const std::string rows[] = {
        "model     " + _cfg.model,
        "base_url  " + _cfg.baseUrl,
        "prompts    " + _cfg.promptsDir,
        "results    " + _cfg.resultsDir,
        "key       " + _cfg.keySource(),
        "retention " + std::to_string(_cfg.retentionMaxFiles) + " files",
    };
    static_assert(sizeof(rows) / sizeof(rows[0]) == static_cast<size_t>(kSettingsRows),
                  "Settings row count drifted from kSettingsRows");

    beginList();
    if (_softkeys) {
        // Casio: label + right-aligned value, so the values line up under the
        // wider LCD face instead of trailing a space-padded string the face
        // cannot align.
        static const char* const keys[kSettingsRows] = {
            "model", "base_url", "prompts", "results", "key", "retention"};
        const std::string vals[kSettingsRows] = {
            _cfg.model,
            _cfg.baseUrl,
            _cfg.promptsDir,
            _cfg.resultsDir,
            _cfg.keySource(),
            std::to_string(_cfg.retentionMaxFiles) + " files",
        };
        for (int i = 0; i < kSettingsRows; ++i)
            addRowKV(keys[i], vals[i].c_str(), i, i == _focus);
    } else {
        for (int i = 0; i < kSettingsRows; ++i)
            addRow(rows[i].c_str(), i, i == _focus);
    }
    scrollListIntoView();
}

/**
 * The model picker: model name on the left, the company's mark on the right.
 * The app's only list long enough to scroll on a numos screen (the content
 * viewport is deliberately clipped, so the list gets its own container and the
 * rows keep the absolute index*ROW_H placement every other list uses).
 */
void AiApp::showModels() {
    _view = Screen::Models;
    buildChrome();
    setTitle(_saveFailed ? "AI  Model  ! not saved" : "AI  Model");

    ensureModelIconDscs();

    // Hint pinned at the TOP of the content box. It used to be appended under the
    // last model, where it extended the scroll content and clipped against the
    // softkey band. It is a sibling of the list, not a row, so it never scrolls
    // away — and the list below is one ROW_H shorter, one fewer visible model,
    // which is the legibility the longer list wanted.
    if (_content) {
        lv_obj_t* hint = lv_label_create(_content);
        lv_label_set_text(hint, _softkeys ? "7 refresh   EXE pick"
                                          : "7 refresh   EXE pick   AC back");
        lv_obj_set_style_text_font(hint, ui::fontUiSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(hint, lv_color_hex(_sc.textDim), LV_PART_MAIN);
        lv_obj_set_pos(hint, PAD, 1);
    }

    beginList(ROW_H, _tallRows ? MODEL_ROWS_VISIBLE * MODEL_ROW_H : 0);
    if (!_list) return;

    const int cur = ai::findModelById(_cfg.model.c_str());
    if (cur < 0) {
        // Not fatal, but say it: the configured id is not in the bundle, so no row
        // can honestly be marked as the current pick.
        std::printf("[AI] model '%s' is not in the bundled catalogue (%d entries)\n",
                    _cfg.model.c_str(), ai::kModelCount);
    }
    _focus = (cur >= 0) ? cur : 0;

    for (int i = 0; i < ai::kModelCount; ++i) addModelRow(i, i == _focus);

    scrollListIntoView();
}

lv_obj_t* AiApp::addModelRow(int index, bool focused) {
    if (!_list || index < 0 || index >= ai::kModelCount) return nullptr;
    const ai::ModelEntry& m = ai::kModels[index];

    const int pitch  = _tallRows ? MODEL_ROW_H : ROW_H;
    const int padY   = _tallRows ? 1 : 2;
    const int scale  = _tallRows ? ICON_SCALE_125 : ICON_SCALE_1X;
    const int iconW  = (ai::icons::kIconW * scale) / ICON_SCALE_1X;   // 25 or 20 px

    lv_obj_t* row = lv_obj_create(_list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * PAD, pitch - 2);
    lv_obj_set_pos(row, PAD, index * pitch);
    lv_obj_set_style_radius(row, _sc.radiusRow, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 4, LV_PART_MAIN);
    // 1 px on the tall row, not 2: the box is 27 px and a 1.25x mark 25 px, so the
    // content box only just holds it.
    lv_obj_set_style_pad_top(row, padY, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, padY, LV_PART_MAIN);
    applyRowFocus(row, focused, _focusFill, _sc.accent, _sc.rowFocus);

    // Name on the left. LONG_DOT rather than wrap: a too-long label must not eat a
    // second line and break the fixed pitch grid.
    lv_obj_t* lab = lv_label_create(row);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lab, SCREEN_W - 2 * PAD - 10 - iconW - 6);
    lv_label_set_text(lab, _numbered ? numbered(index + 1, m.label).c_str() : m.label);
    lv_obj_set_style_text_font(lab, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(rowTextColor(focused)),
                                LV_PART_MAIN);
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

    // Company mark on the right. The bundled marks are a WHITE glyph, which is what
    // the numos rows are built around (black row, blue focused row); on the casio
    // LCD the row is light, so the mark is drawn in the surface's own icon ink —
    // black there, white on numos — via recolor, which preserves the mark's alpha.
    // No theme branch: the token carries the colour.
    lv_obj_t* icon = lv_image_create(row);
    lv_image_set_src(icon, &g_modelIconDsc[static_cast<int>(m.provider)]);
    lv_obj_set_style_image_recolor(icon, lv_color_hex(_sc.iconInk), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_image_set_scale(icon, scale);
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, 0, 0);

    _rows.push_back(row);
    return row;
}

void AiApp::selectModel(int index) {
    if (index < 0 || index >= ai::kModelCount) return;
    const ai::ModelEntry& m = ai::kModels[index];

    if (_cfg.model != m.id) {
        if (!ai::setConfigModel(_cfg.configPath, m.id)) {
            // Never claim a write that did not land. The title carries the flag;
            // Settings has no room for a footer line.
            _saveFailed = true;
            _status = std::string("model not saved: ") + m.id;
            std::printf("[AI] %s\n", _status.c_str());
            showModels();
            return;
        }
        _cfg.model = m.id;
    }
    _saveFailed = false;
    _status = "model: " + _cfg.model;
    std::printf("[AI] model set to %s\n", _cfg.model.c_str());
    showSettings();
}

/**
 * Key 7 on the picker: re-read /ai/config.json and redraw the list. The model set
 * can change underneath the app (the portal writes the same file), and the only
 * way to see that was to leave the screen and come back.
 */
void AiApp::refreshModels() {
    _cfg        = ai::AiConfig::load(_cfg.configPath);
    _saveFailed = false;
    _status.clear();
    std::printf("[AI] models refreshed, model=%s\n", _cfg.model.c_str());
    showModels();
}

/**
 * What ENTER does on the focused row — shared by ENTER and the casio digit
 * shortcut so the two can never drift apart.
 */
void AiApp::activateFocused() {
    switch (_view) {
        case Screen::Menu:
            switch (_focus) {
                case 0: showAsk(); return;
                case 1: showCapture(); return;
                case 2: showRecent(); return;
                default: showSettings(); return;
            }
        case Screen::Settings:
            if (_focus == 0) showModels();       // only "model" is live
            return;
        case Screen::Models:
            selectModel(_focus);
            return;
        case Screen::Ask:
            startRun(std::string());
            return;
        case Screen::Capture:
            if (_focus < static_cast<int>(_files.size()))
                startRun(_files[static_cast<size_t>(_focus)]);
            return;
        case Screen::Recent:
            if (_focus < static_cast<int>(_results.size())) {
                _resultPath = _cfg.resultsDir + "/" + _results[static_cast<size_t>(_focus)];
                openAnswer(_resultPath);
                showResult();
            }
            return;
        default:
            return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Flow
// ═══════════════════════════════════════════════════════════════════════════

void AiApp::load() {
    if (!_screen) begin();
    _exit = false;
    _committedShown = false;
    _saveFailed = false;
    _question.clear();
    _status.clear();
    _cfg      = ai::AiConfig::load();
    // The check is per-answer and per-visit: nothing about it survives leaving
    // the app, and an aborted fetch must not leave a socket behind.
    _wa.abort();
    _edit.reset(std::string());
    _toolQuery.clear();
    _toolServer.clear();
    _transcribed.clear();
    _sourceSlug.clear();
    _checkText.clear();
    _waNote.clear();
    _waSaved = false;
    _styles.bodySize    = 14;
    _styles.headingSize = 14;
    _styles.codeSize    = 12;
    showMenu();
    lv_screen_load(_screen);
    std::printf("[AI] load: transport=%s model=%s key=%s\n",
                _cfg.transport.c_str(), _cfg.model.c_str(), _cfg.keySource().c_str());
}

void AiApp::end() {
    _session.abort();
    _wa.abort();
    clearContent();
    if (_screen) { lv_obj_delete(_screen); _screen = nullptr; }
}

void AiApp::startRun(const std::string& image) {
    _image = image;
    // The typed question only travels with the Ask run: on the Capture path the
    // image IS the question, and sending the Ask screen's sample text along with
    // it would ask about both at once.
    const std::string q = (_view == Screen::Ask) ? _question : std::string();
    if (!_session.begin(_cfg, image, q)) {
        _status = "cannot start: " + _session.error();
        showMenu();
        return;
    }
    showStreaming();
    std::printf("[AI] run started (image=%s, transport=%s)\n",
                image.empty() ? "-" : image.c_str(), _cfg.transport.c_str());
}

void AiApp::pumpRun() {
    if (!_session.running()) return;

    const bool more = _session.pump();

    if (_live) lv_label_set_text(_live, _session.liveText().c_str());

    if (!more) {
        std::string path;
        if (_session.commit(&path)) {
            _resultPath = path;
            openAnswer(path);   // also loads the check fields from the file
            showResult();
            std::printf("[AI] committed -> %s\n", path.c_str());
        } else {
            // Name it. A transport failure is not the same thing as an empty
            // answer, and "save failed" for an HTTP 401 sends you looking in
            // the wrong place entirely.
            _status = _session.failed()
                          ? std::string("failed: ") + _session.error()
                          : std::string("save failed: ") + _session.error();
            showMenu();
        }
    }
}

void AiApp::openAnswer(const std::string& path) {
    renderText(ai::readTextFile(path));
    // The answer on screen is what the check is about, so its fields come with
    // it: from the file's %%ai: line, because that is all a reopened answer has.
    loadCheckFields(path);
}

#ifdef NATIVE_SIM
std::string AiApp::debugPageText() const {
    // Solo tiene sentido en las vistas paginadas: en Result `pages()` es la
    // respuesta abierta y en Verify es el texto del chequeo, y en ambas es
    // exactamente lo que hay en pantalla. Fuera de ellas `pages()` describe la
    // ultima respuesta abierta, no lo que se ve.
    if (_view != Screen::Result && _view != Screen::Verify) return std::string();
    const std::vector<mdrender::Page>& pg = _renderer.pages();
    if (_page < 0 || _page >= static_cast<int>(pg.size())) return std::string();
    const mdrender::DisplayList& dl = _renderer.display();
    std::string out;
    for (uint16_t k = 0; k < pg[_page].lineCount; ++k) {
        out += _renderer.lineText(dl.lines[pg[_page].firstLine + k]);
        out += ' ';
    }
    return out;
}

std::string AiApp::debugCheckText() const {
    // Solo en la vista Check: fuera de ella `_edit` describe la última consulta
    // editada, no lo que hay en pantalla.
    if (_view != Screen::Check) return std::string();
    std::string out = "query=" + _edit.text();
    out += " caret=" + std::to_string(_edit.caret());
    out += _edit.edited() ? " edited=1" : " edited=0";
    out += " offered=";
    out += checkAvailable() ? "1" : "0";
    out += " orig=" + _edit.original();
    out += " trans=" + (_transcribed.empty() ? std::string("-") : _transcribed);
    return out;
}
#endif

void AiApp::update() {
    if (_view == Screen::Streaming) pumpRun();
    else if (_view == Screen::Verify) pumpCheck();
}

bool AiApp::consumeExitRequest() {
    const bool e = _exit;
    _exit = false;
    return e;
}

// ═══════════════════════════════════════════════════════════════════════════
// Keys
// ═══════════════════════════════════════════════════════════════════════════

void AiApp::handleKey(const KeyEvent& ev) {
    if (ev.code == KeyCode::MODE) {
        // MODE mid-stream = abort and discard: nothing partial is ever saved.
        if (_view == Screen::Streaming) _session.abort();
        // Same for a check in flight: no file, and the socket goes with it.
        if (_view == Screen::Verify) _wa.abort();
        _exit = true;
        return;
    }
    if (ev.action != KeyAction::PRESS && ev.action != KeyAction::REPEAT) return;

    const int count = currentListSize();

    // Key 7 on the answer: the Wolfram|Alpha check. The model only ever PROPOSES
    // the query — this keypress is the user's decision to spend a call, and it is
    // the quota gate. When the answer carries no query, or no AppID is
    // configured, the tool is not offered and nothing happens at all.
    if (_view == Screen::Result && keyCodeDigitValue(ev.code) == 7) {
        if (checkAvailable()) {
            _edit.reset(_toolQuery);
            _waNote.clear();
            showCheck();
            std::printf("[AI] check opened: '%s' (%s)\n",
                        _edit.text().c_str(), _cfg.waKeySource().c_str());
        } else {
            std::printf("[AI] check not offered: query='%s' server='%s' appid=%s\n",
                        _toolQuery.c_str(), _toolServer.c_str(),
                        _cfg.waAppId.empty() ? "none" : "set");
        }
        return;
    }

    // The check screen owns every key while it is up: LEFT/RIGHT walk the caret,
    // a digit types at it, DEL rubs out, AC undoes (and, with nothing to undo, is
    // the way back), EXE runs it.
    if (_view == Screen::Check) {
        switch (ev.code) {
            case KeyCode::LEFT:
                if (_edit.moveLeft()) showCheck();
                return;
            case KeyCode::RIGHT:
                if (_edit.moveRight()) showCheck();
                return;
            case KeyCode::DEL:
                if (_edit.backspace()) showCheck();
                return;
            case KeyCode::AC:
                if (_edit.edited()) {
                    _edit.restore();
                    showCheck();
                } else {
                    showResult();
                }
                return;
            case KeyCode::EXE:
                startCheck();
                return;
            default: {
                const int d = keyCodeDigitValue(ev.code);
                if (d >= 0 && _edit.insert(static_cast<char>('0' + d))) showCheck();
                return;   // UP/DOWN have no meaning on this screen
            }
        }
    }

    // Casio: a digit selects AND activates that row directly — no cursor walk,
    // no confirmation (SPEC-stageC C2, "digit = launch"). List screens only: on
    // Ask the digits are the question buffer, and this must not eat them.
    if (_softkeys) {
        // Key 7 refreshes the model picker: re-read the config and redraw, so a
        // model changed from the portal shows up without leaving the screen.
        if (_view == Screen::Models && keyCodeDigitValue(ev.code) == 7) {
            refreshModels();
            return;
        }
        switch (_view) {
            case Screen::Menu:
            case Screen::Capture:
            case Screen::Recent:
            case Screen::Settings:
            case Screen::Models: {
                const int d = keyCodeDigitValue(ev.code);
                if (d >= 1 && d <= count && d <= 9) {
                    _focus = d - 1;
                    applyFocus(_focus, count);
                    activateFocused();
                    return;
                }
                break;
            }
            default:
                break;
        }
    }

    switch (ev.code) {
        case KeyCode::UP:
            if (_view == Screen::Menu || _view == Screen::Capture || _view == Screen::Recent ||
                _view == Screen::Settings || _view == Screen::Models) {
                if (_focus > 0) { --_focus; applyFocus(_focus, count); }
            }
            return;

        case KeyCode::DOWN:
            if (_view == Screen::Menu || _view == Screen::Capture || _view == Screen::Recent ||
                _view == Screen::Settings || _view == Screen::Models) {
                if (_focus + 1 < count) { ++_focus; applyFocus(_focus, count); }
            }
            return;

        case KeyCode::LEFT:
            if (_view == Screen::Result && _page > 0) { --_page; rebuildResult(); }
            else if (_view == Screen::Verify && _page > 0) { --_page; rebuildVerify(); }
            return;

        case KeyCode::RIGHT:
            if (_view == Screen::Result && _page + 1 < _renderer.pageCount()) {
                ++_page; rebuildResult();
            } else if (_view == Screen::Verify && _page + 1 < _renderer.pageCount()) {
                ++_page; rebuildVerify();
            }
            return;

        case KeyCode::DEL:
            if (_view == Screen::Ask && !_question.empty()) {
                _question.pop_back();
                showAsk();
            }
            return;

        case KeyCode::AC:
            if (_view == Screen::Streaming) { _session.abort(); showMenu(); return; }
            if (_view == Screen::Verify) {
                // Back to the answer this check was about. The renderer is
                // holding the CHECK's text, so the answer has to be re-opened —
                // rebuilding from the renderer would draw Wolfram's page in the
                // answer's place.
                _wa.abort();
                if (_resultPath.empty()) { showRecent(); return; }
                openAnswer(_resultPath);
                showResult();
                return;
            }
            if (_view == Screen::Menu)      { _exit = true; return; }
            if (_view == Screen::Models)    { showSettings(); return; }
            showMenu();
            return;

        case KeyCode::EXE:
            activateFocused();
            return;

        default:
            if (_view == Screen::Ask) {
                std::string add;
                if (keyToText(ev.code, add)) {
                    if (_question.size() + add.size() < 160) _question += add;
                    showAsk();
                }
            }
            return;
    }
}
