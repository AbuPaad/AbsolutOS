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
#include "../ui/generated/CasioArrowMasks.generated.h"   // corner scroll hint (A8 mask)

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

/// Settings rows. Keep in step with the array in showSettings().
constexpr int kSettingsRows = 7;

/// "3:Question" — the launcher's slot-number idiom, applied to a list row.
std::string numbered(int n, const char* text) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d:%s", n, text);
    return std::string(buf);
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
    if (_scrollArrow) { lv_obj_delete(_scrollArrow); _scrollArrow = nullptr; }
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
        case Screen::Ask:     return "ENTER send   DEL erase   AC back";
        case Screen::Result:  return "\u2190 \u2192 page      AC back";
        case Screen::Models:  return "ENTER pick   AC back";
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
void AiApp::beginList() {
    if (!_content) return;
    _list = lv_obj_create(_content);
    lv_obj_set_size(_list, SCREEN_W, _contentH);
    lv_obj_set_pos(_list, 0, 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_list, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);

    // Corner scroll arrow — calc's history hint (a generated A8 mask recoloured to
    // the text token), reused as the "this list goes on" mark. Lives on the
    // screen, not in the list, so the list cannot clip it.
    if (_chevron && !_scrollArrow) {
        _scrollArrow = lv_image_create(_screen);
        lv_obj_set_style_image_recolor(_scrollArrow, lv_color_hex(_sc.text), LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(_scrollArrow, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(_scrollArrow, LV_ALIGN_TOP_RIGHT, -PAD, 3);
        lv_obj_add_flag(_scrollArrow, LV_OBJ_FLAG_HIDDEN);
    }
}

/** Show the corner arrow when — and only when — the list can move. */
void AiApp::updateScrollChevron() {
    if (!_scrollArrow) return;
    if (!_list) { lv_obj_add_flag(_scrollArrow, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_update_layout(_list);
    const int below = lv_obj_get_scroll_bottom(_list);
    const int above = lv_obj_get_scroll_top(_list);
    if (below <= 0 && above <= 0) {
        lv_obj_add_flag(_scrollArrow, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // More below wins: that is the direction the list is being read in.
    lv_image_set_src(_scrollArrow, below > 0 ? &ui::kCasioArrowDown : &ui::kCasioArrowUp);
    lv_obj_remove_flag(_scrollArrow, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_set_style_bg_opa(row, (focused && _focusFill) ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(_sc.rowFocus), LV_PART_MAIN);

    lv_obj_t* lab = lv_label_create(row);
    // Casio numbers list rows the way the launcher numbers its slots ("1:COMP"),
    // so the row IS the hint and no "1-4 open" line is needed. numos: no prefix.
    char nb[128];
    if (_numbered) { std::snprintf(nb, sizeof(nb), "%d:%s", index + 1, text); text = nb; }
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(focused ? _sc.textOnFocus : _sc.text),
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

void AiApp::applyFocus(int index, int count) {
    for (int i = 0; i < static_cast<int>(_rows.size()); ++i) {
        lv_obj_t* row = _rows[static_cast<size_t>(i)];
        const bool f = (i == index) && i < count;

        // Focus is a filled card only when the interaction profile says so
        // (numos); casio shows it by ink alone — never a rectangle (SPEC-stageC C2).
        lv_obj_set_style_bg_opa(row, (f && _focusFill) ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN);

        lv_obj_t* lab = lv_obj_get_child(row, 0);
        if (lab) {
            lv_obj_set_style_text_color(lab, lv_color_hex(f ? _sc.textOnFocus : _sc.text),
                                        LV_PART_MAIN);
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
    if (_status.empty()) _status = "transport: " + _cfg.transport;
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
    lv_label_set_text(hint, "ENTER send   DEL rub out   AC back");
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
    setTitle(clipped ? "AI  %d/%d  clipped" : "AI  %d/%d",
             _page + 1, n > 0 ? n : 1);
    if (_content) _renderer.render(_page, _content, _styles);
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
        "transport " + _cfg.transport,
        "base_url  " + _cfg.baseUrl,
        "prompts    " + _cfg.promptsDir,
        "results    " + _cfg.resultsDir,
        "key       " + _cfg.keySource(),
        "retention " + std::to_string(_cfg.retentionMaxFiles) + " files",
    };
    static_assert(sizeof(rows) / sizeof(rows[0]) == static_cast<size_t>(kSettingsRows),
                  "Settings row count drifted from kSettingsRows");

    beginList();
    for (int i = 0; i < kSettingsRows; ++i)
        addRow(rows[i].c_str(), i, i == _focus);
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
    beginList();
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

    // No in-list hint: it duplicates the softkey band, extends the scroll content
    // past the last model, and was clipping against the band. The profile that
    // shows no band (numos) still gets its hint line.
    if (!_softkeys) {
        lv_obj_t* hint = lv_label_create(_list);
        lv_label_set_text(hint, "UP/DOWN move   ENTER pick   AC back");
        lv_obj_set_style_text_font(hint, ui::fontUiSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(hint, lv_color_hex(_sc.textDim), LV_PART_MAIN);
        lv_obj_set_pos(hint, PAD, ai::kModelCount * ROW_H + 6);
    }

    scrollListIntoView();
}

lv_obj_t* AiApp::addModelRow(int index, bool focused) {
    if (!_list || index < 0 || index >= ai::kModelCount) return nullptr;
    const ai::ModelEntry& m = ai::kModels[index];

    lv_obj_t* row = lv_obj_create(_list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * PAD, ROW_H - 2);
    lv_obj_set_pos(row, PAD, index * ROW_H);
    lv_obj_set_style_radius(row, _sc.radiusRow, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, (focused && _focusFill) ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(_sc.rowFocus), LV_PART_MAIN);

    // Name on the left. LONG_DOT rather than wrap: a too-long label must not eat a
    // second line and break the fixed ROW_H grid.
    lv_obj_t* lab = lv_label_create(row);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lab, SCREEN_W - 2 * PAD - 10 - ai::icons::kIconW - 6);
    lv_label_set_text(lab, _numbered ? numbered(index + 1, m.label).c_str() : m.label);
    lv_obj_set_style_text_font(lab, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(focused ? _sc.textOnFocus : _sc.text),
                                LV_PART_MAIN);
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

    // Company mark on the right.
    lv_obj_t* icon = lv_image_create(row);
    lv_image_set_src(icon, &g_modelIconDsc[static_cast<int>(m.provider)]);
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
            openAnswer(path);
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
    const std::string text = ai::readTextFile(path);
    mdrender::BufferSource src(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    _renderer.parse(src);
    _renderer.layout(_metrics, _styles);
    _renderer.paginate(_styles);
    _page = 0;
}

void AiApp::update() {
    if (_view == Screen::Streaming) pumpRun();
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
        _exit = true;
        return;
    }
    if (ev.action != KeyAction::PRESS && ev.action != KeyAction::REPEAT) return;

    const int count = currentListSize();

    // Casio: a digit selects AND activates that row directly — no cursor walk,
    // no confirmation (SPEC-stageC C2, "digit = launch"). List screens only: on
    // Ask the digits are the question buffer, and this must not eat them.
    if (_softkeys) {
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
            return;

        case KeyCode::RIGHT:
            if (_view == Screen::Result && _page + 1 < _renderer.pageCount()) {
                ++_page; rebuildResult();
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
            if (_view == Screen::Menu)      { _exit = true; return; }
            if (_view == Screen::Models)    { showSettings(); return; }
            showMenu();
            return;

        case KeyCode::ENTER:
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
