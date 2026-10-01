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
 * The screen never scrolls. Long text wraps inside the container and is
 * clipped by it; paged content changes page instead of scrolling.
 *
 * FIX OWED (2026-09-29): the sys prompt in /ai/config.json now breaks pages at
 * thematic/topic changes and no longer caps a page at ~250 visible characters,
 * so a page may legitimately be taller than CONTENT_H. Today such a page is
 * reflowed and, when it still does not fit, truncated with a visible block
 * (MdRenderer::pageTruncated(), which this app already surfaces). That is
 * correct behaviour, but it makes intra-page scrolling load-bearing rather than
 * cosmetic. The missing piece is one of: a scrollable content container, or
 * MdRenderer splitting an over-tall page at a paragraph boundary itself.
 * Neither exists yet — a long topical page gets cut off with the truncated
 * block instead of being readable.
 */

#include "apps/AiApp.h"

#include <cstdarg>
#include <cstdio>

#include "ai/AiModelIcons.h"
#include "ai/ModelCatalog.h"

using mdrender::CONTENT_H;
using mdrender::SCREEN_W;
using mdrender::STATUS_BAR_H;

namespace {

constexpr uint32_t COL_BG     = 0x000000;
constexpr uint32_t COL_TITLE  = 0xCCCCCC;
constexpr uint32_t COL_TEXT   = 0xDDDDDD;
constexpr uint32_t COL_DIM    = 0x8A8A8A;
constexpr uint32_t COL_ACCENT = 0x1565C0;   // same blue as the launcher focus
constexpr uint32_t COL_OK     = 0x66BB6A;
constexpr int      ROW_H      = 26;
constexpr int      PAD        = 6;

/// Settings rows. Keep in step with the array in showSettings().
constexpr int      kSettingsRows = 7;

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

void AiApp::begin() {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
}

void AiApp::buildChrome() {
    clearContent();

    _title = lv_label_create(_screen);
    lv_obj_set_style_text_color(_title, lv_color_hex(COL_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_font(_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(_title, PAD, 3);

    _content = lv_obj_create(_screen);
    lv_obj_set_pos(_content, 0, STATUS_BAR_H);
    lv_obj_set_size(_content, SCREEN_W, CONTENT_H);
    lv_obj_set_style_bg_opa(_content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_content, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(_content, true, LV_PART_MAIN);
    lv_obj_remove_flag(_content, LV_OBJ_FLAG_SCROLLABLE);
}

void AiApp::clearContent() {
    _rows.clear();
    _live = nullptr;
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

// ═══════════════════════════════════════════════════════════════════════════
// Rows — one style shared by Menu / Capture / Recent / Settings
// ═══════════════════════════════════════════════════════════════════════════

lv_obj_t* AiApp::addRow(const char* text, int index, bool focused) {
    if (!_content) return nullptr;
    lv_obj_t* row = lv_obj_create(_content);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * PAD, ROW_H - 2);
    lv_obj_set_pos(row, PAD, index * ROW_H);
    lv_obj_set_style_radius(row, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, focused ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(COL_ACCENT), LV_PART_MAIN);

    lv_obj_t* lab = lv_label_create(row);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(focused ? 0xFFFFFF : COL_TEXT), LV_PART_MAIN);
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

    _rows.push_back(row);
    return row;
}

void AiApp::applyFocus(int index, int count) {
    for (int i = 0; i < static_cast<int>(_rows.size()); ++i) {
        const bool f = (i == index);
        lv_obj_set_style_bg_opa(_rows[static_cast<size_t>(i)],
                               (f && i < count) ? LV_OPA_COVER : LV_OPA_TRANSP,
                               LV_PART_MAIN);
    }
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
    for (int i = 0; i < kMenuCount; ++i) addRow(kMenuItems[i], i, i == 0);

    // A failed run lands back here, so the reason has to be visible AND logged:
    // a silent fallback to the menu is exactly how a broken save path hides.
    if (_status.empty()) _status = "transport: " + _cfg.transport;
    lv_obj_t* foot = lv_label_create(_content);
    lv_label_set_long_mode(foot, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(foot, SCREEN_W - 2 * PAD);
    lv_label_set_text(foot, _status.c_str());
    lv_obj_set_style_text_font(foot, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(foot, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_pos(foot, PAD, kMenuCount * ROW_H + 6);
    std::printf("[AI] menu status: %s\n", _status.c_str());
}

void AiApp::showAsk() {
    _view = Screen::Ask;
    buildChrome();
    setTitle("AI  Ask");

    if (_question.empty()) _question = kAskSample;

    lv_obj_t* box = lv_obj_create(_content);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, SCREEN_W - 2 * PAD, 96);
    lv_obj_set_pos(box, PAD, PAD);
    lv_obj_set_style_radius(box, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(COL_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x101418), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 6, LV_PART_MAIN);

    lv_obj_t* lab = lv_label_create(box);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lab, SCREEN_W - 4 * PAD);
    lv_label_set_text(lab, _question.c_str());
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), LV_PART_MAIN);

    lv_obj_t* hint = lv_label_create(_content);
    lv_label_set_text(hint, "ENTER send   DEL rub out   AC back");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_pos(hint, PAD, 110);
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
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(lab, PAD, PAD);
        return;
    }
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
    lv_obj_set_style_text_font(_live, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_live, lv_color_hex(COL_TEXT), LV_PART_MAIN);
    lv_label_set_text(_live, "…");
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
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_pos(lab, PAD, PAD);
        return;
    }
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
    // seven rows already run to the bottom of CONTENT_H (176 px). So the flag goes
    // in the title, which is the one line guaranteed to be on screen.
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

    for (int i = 0; i < kSettingsRows; ++i)
        addRow(rows[i].c_str(), i, i == _focus);
}

/**
 * The model picker: a scrolling list, model name on the left, the company's mark
 * on the right. This is the one screen in the app that scrolls — _content is
 * deliberately clipped and non-scrollable, so the list gets its own container and
 * the rows keep the absolute index*ROW_H placement every other list uses. The
 * container is just a viewport that lets a tall list move under it.
 */
void AiApp::showModels() {
    _view = Screen::Models;
    buildChrome();
    setTitle(_saveFailed ? "AI  Model  ! not saved" : "AI  Model");

    ensureModelIconDscs();

    _list = lv_obj_create(_content);
    lv_obj_set_size(_list, SCREEN_W, CONTENT_H);
    lv_obj_set_pos(_list, 0, 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_list, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);

    const int cur = ai::findModelById(_cfg.model.c_str());
    if (cur < 0) {
        // Not fatal, but say it: the configured id is not in the bundle, so no row
        // can honestly be marked as the current pick.
        std::printf("[AI] model '%s' is not in the bundled catalogue (%d entries)\n",
                    _cfg.model.c_str(), ai::kModelCount);
    }
    _focus = (cur >= 0) ? cur : 0;

    for (int i = 0; i < ai::kModelCount; ++i) addModelRow(i, i == _focus);

    lv_obj_t* hint = lv_label_create(_list);
    lv_label_set_text(hint, "UP/DOWN move   ENTER pick   AC back");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_pos(hint, PAD, ai::kModelCount * ROW_H + 6);

    scrollModelIntoView();
}

lv_obj_t* AiApp::addModelRow(int index, bool focused) {
    if (!_list || index < 0 || index >= ai::kModelCount) return nullptr;
    const ai::ModelEntry& m = ai::kModels[index];

    lv_obj_t* row = lv_obj_create(_list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SCREEN_W - 2 * PAD, ROW_H - 2);
    lv_obj_set_pos(row, PAD, index * ROW_H);
    lv_obj_set_style_radius(row, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, focused ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(COL_ACCENT), LV_PART_MAIN);

    // Name on the left. LONG_DOT rather than wrap: a too-long label must not eat a
    // second line and break the fixed ROW_H grid.
    lv_obj_t* lab = lv_label_create(row);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lab, SCREEN_W - 2 * PAD - 10 - ai::icons::kIconW - 6);
    lv_label_set_text(lab, m.label);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, lv_color_hex(focused ? 0xFFFFFF : COL_TEXT), LV_PART_MAIN);
    lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

    // Company mark on the right.
    lv_obj_t* icon = lv_image_create(row);
    lv_image_set_src(icon, &g_modelIconDsc[static_cast<int>(m.provider)]);
    lv_obj_align(icon, LV_ALIGN_RIGHT_MID, 0, 0);

    _rows.push_back(row);
    return row;
}

void AiApp::scrollModelIntoView() {
    if (!_list) return;
    if (_focus < 0 || _focus >= static_cast<int>(_rows.size())) return;
    lv_obj_scroll_to_view(_rows[static_cast<size_t>(_focus)], LV_ANIM_OFF);
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

    switch (ev.code) {
        case KeyCode::UP:
            if (_view == Screen::Menu || _view == Screen::Capture || _view == Screen::Recent ||
                _view == Screen::Settings || _view == Screen::Models) {
                if (_focus > 0) { --_focus; applyFocus(_focus, count); scrollModelIntoView(); }
            }
            return;

        case KeyCode::DOWN:
            if (_view == Screen::Menu || _view == Screen::Capture || _view == Screen::Recent ||
                _view == Screen::Settings || _view == Screen::Models) {
                if (_focus + 1 < count) { ++_focus; applyFocus(_focus, count); scrollModelIntoView(); }
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
