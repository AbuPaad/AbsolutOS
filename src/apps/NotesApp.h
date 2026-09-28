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
 * NotesApp.h — emulator-only demo host for the shared Markdown render
 * module (src/mdrender). It is deliberately tiny: it opens one .md through
 * FileSource, runs parse -> layout -> paginate once, and draws ONE page with
 * MdRenderer::render(). It owns the chrome around the text (title band, line
 * cursor, keymap) so the module itself never touches a screen or the StatusBar.
 *
 * Keymap (reader):
 *   UP/DOWN   move the line cursor (no intra-page scroll)
 *   LEFT/RIGHT change page
 *   F2/F3     font size 12/14/20 (16 is compiled out — see MdStyles)
 *   AC/MODE   leave the app
 *
 * The .numos smoke opens it with `open_app notes` against
 * <fs-root>/notes/demo.md.
 */

#pragma once

#include <lvgl.h>

#include <string>

#include "input/KeyCodes.h"
#include "mdrender/MdRenderer.h"

class NotesApp {
public:
    NotesApp();
    ~NotesApp();

    NotesApp(const NotesApp&) = delete;
    NotesApp& operator=(const NotesApp&) = delete;

    void load();
    void end();
    void update() {}   ///< renderer is synchronous; nothing per-frame

    void handleKey(const KeyEvent& ev);
    bool consumeExitRequest();

private:
    void begin();
    void setFont(int idx);       ///< 0=12px, 1=14px, 2=20px
    void openNote(const char* path);
    void buildChrome();
    void rebuildPage();
    void updateCursor();
    void updateTitle();

    lv_obj_t* _screen     = nullptr;
    lv_obj_t* _title      = nullptr;
    lv_obj_t* _content    = nullptr;
    lv_obj_t* _cursorRect = nullptr;

    mdrender::MdRenderer   _renderer;
    mdrender::MdStyles     _styles;
    mdrender::LvglMetrics  _metrics;

    std::string _path = "/notes/demo.md";
    int  _page    = 0;
    int  _cursor  = 0;
    int  _fontIdx = 1;
    bool _exit    = false;
};
