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
 * AiApp.h — the AI wrapper: action menu, Ask, Capture, the streaming card, the
 * paged answer, recent answers and settings.
 *
 * It owns every pixel and every key (the module in src/ai/AiClient is LVGL-free
 * and does the work). Read the answer through the SAME renderer as the notes
 * reader, so a saved answer needs no special-casing anywhere.
 *
 * Keymap:
 *   Menu/Recent/Capture/Settings   UP/DOWN move · ENTER select · AC/MODE leave
 *   Ask                            characters append · DEL rubs out · ENTER sends
 *                                  AC = back to the menu · MODE = leave the app
 *   Streaming                      MODE = abort (nothing is saved) · AC = same
 *   Result                         LEFT/RIGHT change page · AC back · MODE leave
 *
 * Architecture: ~/musings/sserialprintthing/ai-wrapper-architecture.md
 */

#pragma once

#include <lvgl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../ui/Theme.h"
#include "ai/AiClient.h"
#include "input/KeyCodes.h"
#include "mdrender/MdRenderer.h"

class AiApp {
public:
    AiApp();
    ~AiApp();

    AiApp(const AiApp&) = delete;
    AiApp& operator=(const AiApp&) = delete;

    void load();
    void end();
    void update();                    ///< pumps the run and refreshes the card
    void handleKey(const KeyEvent& ev);
    bool consumeExitRequest();
    bool isActive() const { return _screen != nullptr; }

private:
    enum class Screen : uint8_t {
        Menu, Ask, Capture, Streaming, Result, Recent, Settings, Models
    };

    void begin();
    void buildChrome();
    void clearContent();
    void setTitle(const char* fmt, ...);

    // Theme-driven chrome (doc 02/07). `_sc` is the AI's resolved surface
    // (kBuiltinThemes[numos].appSurfaces pins today's dark look; casio inherits
    // its light tokens). Refreshed on every buildChrome(), so a theme swap while
    // the app is open is picked up when SystemApp reloads it.
    void readSurface();
    /// A list screen needs its rows in a scrollable viewport or the last rows
    /// are unreachable (the Settings screen used to lose two of them).
    void beginList();
    void scrollListIntoView();
    /// Corner up/down arrow on a scrollable list (calc's history hint idiom).
    void updateScrollChevron();
    /// Casio's bottom softkey band. Not built when the interaction model has
    /// softkeyRow == false, so the numos screens gain no pixels.
    void buildSoftkey(const char* label);
    const char* softkeyLabel() const;

    // Screens
    void showMenu();
    void showAsk();
    void showCapture();
    void showStreaming();
    void showResult();
    void showRecent();
    void showSettings();
    void showModels();

    // Flow
    void startRun(const std::string& image);
    void pumpRun();
    void openAnswer(const std::string& path);   ///< parse+layout+paginate a .md
    void rebuildResult();

    // List helpers (Menu / Capture / Recent / Settings share one row style)
    lv_obj_t* addRow(const char* text, int index, bool focused);
    /// A model row: label left, company mark right. Indexes ai::kModels.
    lv_obj_t* addModelRow(int index, bool focused);
    /// Persist the pick, then return to Settings.
    void selectModel(int index);
    /// What ENTER does on the focused row — shared with the casio digit shortcut.
    void activateFocused();
    void applyFocus(int index, int count);
    int  currentListSize() const;

    lv_obj_t* _screen  = nullptr;
    lv_obj_t* _title   = nullptr;
    lv_obj_t* _content = nullptr;
    lv_obj_t* _list    = nullptr;      ///< scrolling list viewport (list screens)
    lv_obj_t* _live    = nullptr;      ///< streaming card text
    lv_obj_t* _softkey = nullptr;      ///< casio bottom softkey band
    lv_obj_t* _scrollArrow = nullptr;  ///< casio corner scroll hint (image mask)
    std::vector<lv_obj_t*> _rows;      ///< row widgets, restyled on focus change

    ui::AppColours _sc{};              ///< resolved surface (see readSurface)
    int            _contentH = mdrender::CONTENT_H;  ///< minus the softkey band
    bool           _focusFill = true;  ///< interaction model: does focus paint?
    bool           _softkeys  = false; ///< interaction model: softkey row?
    bool           _numbered  = false; ///< interaction model: "N:" row prefixes?
    bool           _chevron   = false; ///< interaction model: corner scroll arrow?

    Screen _view  = Screen::Menu;
    int    _focus = 0;
    int    _page  = 0;
    bool   _exit  = false;
    bool   _committedShown = false;
    /// Set when a model pick could not be written to the config. Surfaced in the
    /// screen title, because Settings has no spare line to show a footer in.
    bool   _saveFailed = false;

    ai::AiConfig  _cfg;
    ai::AiSession _session;

    std::string _question;             ///< the Ask buffer
    std::string _status;               ///< transient line under the rows
    std::string _resultPath;           ///< where the answer was committed
    std::string _image;                ///< the prompt image this run used

    std::vector<std::string> _files;   ///< prompts dir listing
    std::vector<std::string> _results; ///< results dir listing

    mdrender::MdRenderer  _renderer;
    mdrender::MdStyles    _styles;
    mdrender::LvglMetrics _metrics;
};
