/*
 * NumOS — HelpOverlay.cpp
 * Copyright (C) 2026 Juan Ramon
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "HelpOverlay.h"
#include "ThemeManager.h"
#include "ThemeFonts.h"
#include "help/HelpContent.h"
#include "../Config.h"

namespace ui {

namespace {

constexpr int PAD_X     = 8;
constexpr int TITLE_Y   = 6;
constexpr int ROW_TOP   = 26;
constexpr int ROW_PITCH = 13;
constexpr int KEY_W     = 92;   // key column width; description starts after it
constexpr int FOOT_H    = 16;

inline lv_obj_t* label(lv_obj_t* parent, const char* txt, const lv_font_t* font,
                       uint32_t colour) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), LV_PART_MAIN);
    lv_obj_set_style_text_opa(l, LV_OPA_COVER, LV_PART_MAIN);
    return l;
}

} // namespace

// ── HelpChip ────────────────────────────────────────────────────────────────
void HelpChip::create(lv_obj_t* parent, int x, int y) {
    if (!parent) return;

    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_set_pos(chip, x, y);
    lv_obj_set_size(chip, W, H);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(chip, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(chip, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chip, 0, LV_PART_MAIN);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    // "?" only: the key binding is taught by the overlay, not by the chip.
    lv_obj_t* q = label(chip, "?", fontUiSmall(), 0xFFFFFF);
    lv_obj_center(q);
}

// ── HelpOverlay ─────────────────────────────────────────────────────────────
void HelpOverlay::open(lv_obj_t* parent, int appId) {
    if (!parent || _panel) return;

    const auto& th = ThemeManager::instance().current();
    const uint32_t bg    = th.bg    ? th.bg    : 0xFFFFFF;
    const uint32_t text  = th.text  ? th.text  : 0x111111;
    const uint32_t dim   = th.textDim ? th.textDim : 0x5A6470;
    const uint32_t accent = th.accent ? th.accent : 0xE05500;

    _panel = lv_obj_create(parent);
    lv_obj_set_pos(_panel, 0, 0);
    lv_obj_set_size(_panel, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(_panel, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_panel, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);

    const help::Page page = help::page(appId);

    // Title + a rule under it, so the page reads as a sheet rather than a dump.
    lv_obj_t* title = label(_panel, page.title, fontUi(), text);
    lv_obj_set_pos(title, PAD_X, TITLE_Y);

    lv_obj_t* rule = lv_obj_create(_panel);
    lv_obj_set_pos(rule, PAD_X, TITLE_Y + 13);
    lv_obj_set_size(rule, SCREEN_WIDTH - 2 * PAD_X, 1);
    lv_obj_set_style_bg_color(rule, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rule, 0, LV_PART_MAIN);

    // Rows. Clamped to what fits above the footer: a help page that scrolls on a
    // keypad is a help page nobody reads.
    const int maxRows = (SCREEN_HEIGHT - ROW_TOP - FOOT_H - 4) / ROW_PITCH;
    const int n = (page.count < maxRows) ? page.count : maxRows;
    for (int i = 0; i < n; ++i) {
        const int y = ROW_TOP + i * ROW_PITCH;
        lv_obj_t* k = label(_panel, page.rows[i].keys, fontUiSmall(), accent);
        lv_obj_set_pos(k, PAD_X, y);
        lv_obj_set_width(k, KEY_W);
        lv_label_set_long_mode(k, LV_LABEL_LONG_MODE_CLIP);

        lv_obj_t* d = label(_panel, page.rows[i].what, fontUiSmall(), text);
        lv_obj_set_pos(d, PAD_X + KEY_W + 4, y);
        lv_obj_set_width(d, SCREEN_WIDTH - (PAD_X * 2) - KEY_W - 4);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_CLIP);
    }

    lv_obj_t* foot = label(_panel, "AC close", fontUiSmall(), dim);
    lv_obj_set_pos(foot, PAD_X, SCREEN_HEIGHT - FOOT_H + 2);
}

void HelpOverlay::close() {
    if (!_panel) return;
    lv_obj_delete(_panel);
    _panel = nullptr;
}

} // namespace ui
