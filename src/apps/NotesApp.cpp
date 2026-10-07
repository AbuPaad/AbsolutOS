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
 * NotesApp.cpp — emulator demo host for src/mdrender. See the header.
 *
 * Layout note: the content container is placed at y = STATUS_BAR_H with
 * height = CONTENT_H, i.e. it mimics the device's ~320x200 shell crop + 24 px
 * status band. The bottom 40 px stay black, exactly like the panel after the
 * fx-82 shell crop, so a golden captured here matches what the renderer will
 * produce on device.
 */

#include "apps/NotesApp.h"
#include "../ui/ThemeFonts.h"

#include <cstdio>

using namespace mdrender;

namespace {
constexpr uint32_t COL_BG    = 0x000000;
constexpr uint32_t COL_TITLE = 0xCCCCCC;
constexpr uint32_t COL_CUR   = 0x1F3A5F;
}  // namespace

NotesApp::NotesApp() {}
NotesApp::~NotesApp() { end(); }

void NotesApp::begin() {
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_screen, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);
}

void NotesApp::setFont(int idx) {
    _fontIdx = (idx < 0) ? 0 : (idx > 2 ? 2 : idx);
    switch (_fontIdx) {
        case 0: _styles.bodySize = 12; _styles.headingSize = 12; break;
        case 2: _styles.bodySize = 20; _styles.headingSize = 20; break;
        case 1:
        default: _styles.bodySize = 14; _styles.headingSize = 14; break;
    }
    _styles.codeSize = 12;   // monospace font is deferred (README decision #9)
}

void NotesApp::openNote(const char* path) {
    FileSource src(path);
    _renderer.parse(src);
    _renderer.layout(_metrics, _styles);
    _renderer.paginate(_styles);
    _page = 0;
    _cursor = 0;
}

void NotesApp::buildChrome() {
    _title = lv_label_create(_screen);
    lv_obj_set_style_text_color(_title, lv_color_hex(COL_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_font(_title, ui::fontUi(), LV_PART_MAIN);
    lv_obj_set_pos(_title, 4, 3);

    _content = lv_obj_create(_screen);
    lv_obj_set_pos(_content, 0, STATUS_BAR_H);
    lv_obj_set_size(_content, SCREEN_W, CONTENT_H);
    lv_obj_set_style_bg_opa(_content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_content, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(_content, true, LV_PART_MAIN);
    lv_obj_remove_flag(_content, LV_OBJ_FLAG_SCROLLABLE);
}

void NotesApp::load() {
    if (!_screen) begin();
    _exit = false;
    setFont(_fontIdx);
    openNote(_path.c_str());
    buildChrome();
    updateTitle();
    rebuildPage();
    lv_screen_load(_screen);
}

void NotesApp::end() {
    if (_cursorRect) { lv_obj_delete(_cursorRect); _cursorRect = nullptr; }
    // The renderer's page object lives in _content: forget it BEFORE the
    // container dies, or the next load() renders into a fresh container and
    // render() deletes the freed page object first (use-after-free).
    _renderer.releasePage();
    if (_content) { lv_obj_delete(_content); _content = nullptr; }
    if (_title) { lv_obj_delete(_title); _title = nullptr; }
    if (_screen) { lv_obj_delete(_screen); _screen = nullptr; }
}

void NotesApp::updateTitle() {
    if (!_title) return;
    const int n = _renderer.pageCount();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Notes  %d/%d", _page + 1, (n > 0 ? n : 1));
    lv_label_set_text(_title, buf);
}

void NotesApp::rebuildPage() {
    if (!_content) return;
    if (_cursorRect) { lv_obj_delete(_cursorRect); _cursorRect = nullptr; }
    _renderer.render(_page, _content, _styles);
    updateCursor();
    updateTitle();
}

void NotesApp::updateCursor() {
    if (_cursorRect) { lv_obj_delete(_cursorRect); _cursorRect = nullptr; }
    if (!_content) return;
    const auto& pages = _renderer.pages();
    if (_page < 0 || _page >= static_cast<int>(pages.size())) return;
    const auto& lines = _renderer.display().lines;
    const Page& pg = pages[static_cast<size_t>(_page)];
    if (pg.lineCount == 0) return;
    if (_cursor < 0) _cursor = 0;
    if (_cursor >= static_cast<int>(pg.lineCount)) _cursor = pg.lineCount - 1;
    const uint32_t li = pg.firstLine + static_cast<uint32_t>(_cursor);
    if (li >= lines.size()) return;
    const int baseY = lines[pg.firstLine].y;

    _cursorRect = lv_obj_create(_content);
    lv_obj_remove_flag(_cursorRect, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_cursorRect, lv_color_hex(COL_CUR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_cursorRect, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(_cursorRect, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_cursorRect, 0, LV_PART_MAIN);
    lv_obj_set_pos(_cursorRect, 0, static_cast<int>(lines[li].y) - baseY);
    lv_obj_set_size(_cursorRect, SCREEN_W, lines[li].h > 0 ? lines[li].h : 1);
}

bool NotesApp::consumeExitRequest() {
    const bool e = _exit;
    _exit = false;
    return e;
}

void NotesApp::handleKey(const KeyEvent& ev) {
    if (ev.code == KeyCode::MODE) { _exit = true; return; }
    if (ev.action != KeyAction::PRESS && ev.action != KeyAction::REPEAT) return;

    const int pageCount = _renderer.pageCount();
    switch (ev.code) {
        case KeyCode::LEFT:
            if (_page > 0) { --_page; _cursor = 0; rebuildPage(); }
            return;
        case KeyCode::RIGHT:
            if (_page + 1 < pageCount) { ++_page; _cursor = 0; rebuildPage(); }
            return;
        case KeyCode::UP:
            if (_cursor > 0) { --_cursor; updateCursor(); }
            return;
        case KeyCode::DOWN:
            if (_page >= 0 && _page < pageCount) {
                const int last = static_cast<int>(_renderer.pages()[static_cast<size_t>(_page)].lineCount) - 1;
                if (_cursor < last) { ++_cursor; updateCursor(); }
            }
            return;
        case KeyCode::F2:
            setFont(_fontIdx - 1);
            openNote(_path.c_str());
            rebuildPage();
            return;
        case KeyCode::F3:
            setFont(_fontIdx + 1);
            openNote(_path.c_str());
            rebuildPage();
            return;
        case KeyCode::AC:
            _exit = true;
            return;
        default:
            return;
    }
}
