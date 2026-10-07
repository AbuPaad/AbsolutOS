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
 *   Menu/Recent/Capture/Settings   UP/DOWN move · EXE select · AC/MODE leave
 *   Ask                            characters append · DEL rubs out · EXE sends
 *                                  AC = back to the menu · MODE = leave the app
 *   Streaming                      MODE = abort (nothing is saved) · AC = same
 *   Result                         LEFT/RIGHT change page · 7 = the Wolfram|Alpha
 *                                  check · AC back · MODE leave
 *   Check                          LEFT/RIGHT move the caret · digits edit ·
 *                                  DEL rubs out · AC undoes the edit (and, at the
 *                                  original, goes back) · EXE runs the check
 *   Verify                         LEFT/RIGHT change page · AC back to the answer
 *
 * Never label a key "ENTER": the physical keypad has EXE, and ENTER is only a
 * legacy alias folded into it (see KeyCodes.h). Hints say EXE.
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
#include "ai/WolframClient.h"     // the check hop (LVGL-free; the app owns the keys)
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

#ifdef NATIVE_SIM
    // ── Sonda de prueba (SOLO emulador, read-only) ────────────────────────
    // Una captura del Result es la misma app en hasta 20 páginas distintas, así
    // que `assert_app AI` solo demuestra QUÉ app está abierta, no QUÉ pantalla.
    // Estos accesores devuelven el texto de la página cargada para que
    // `assert_ai_page_contains` justifique cada frame del sitio web.
    // Firmware-neutro: NATIVE_SIM solo existe en [env:emulator_pc].
    std::string debugPageText() const;   ///< "" fuera de la vista Result
    /// El estado de la pantalla Check, para `assert_ai_check_contains`: la
    /// consulta TAL COMO ESTÁ editada, el índice del cursor y si sigue siendo lo
    /// que propuso el modelo. "" fuera de la vista Check.
    std::string debugCheckText() const;
#endif

private:
    enum class Screen : uint8_t {
        Menu, Ask, Capture, Streaming, Result, Check, Verify, Recent, Settings, Models
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
    /// Text colour for a row's labels. The filled-focus profiles (numos) invert
    /// to textOnFocus on the filled card; the OUTLINE profiles (casio) keep the
    /// normal text colour, because casio's textOnFocus is the accent — painting
    /// the focused row in it made the SELECTED row look dimmed, not selected.
    uint32_t rowTextColor(bool focused) const {
        return (focused && _focusFill) ? _sc.textOnFocus : _sc.text;
    }
    /// A list screen needs its rows in a scrollable viewport or the last rows
    /// are unreachable (the Settings screen used to lose two of them).
    /// `topInset` reserves a non-scrolling band at the top of the content box
    /// (the Models hint line); the list then gets that much less height, so the
    /// number of visible rows drops by exactly one.
    /// `height` (0 = fill the content box) pins the viewport to an exact number
    /// of rows, so the last visible row is never cut by the viewport edge.
    void beginList(int topInset = 0, int height = 0);
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
    /// Re-read /ai/config.json and redraw the picker (key 7). Only the "current"
    /// marker can change, e.g. after the portal edited the model underneath us.
    void refreshModels();

    // The Wolfram|Alpha check (the user's own verification hop)
    void showCheck();     ///< what the model asked, and the digits-only edit
    void showVerify();    ///< the WA text as it arrives, then paginated
    void startCheck();    ///< EXE on Check: run it (the keypress IS the gate)
    void pumpCheck();     ///< one frame of the fetch; saves + paginates at the end
    void rebuildVerify(); ///< title + the page currently shown
    /// Parse+layout+paginate+render a markdown string held in RAM. Shared by the
    /// saved answer and the check's on-screen text, so both go through the notes
    /// pipeline — never a second renderer.
    void renderText(const std::string& text);
    /// Read the query/tool/transcription for the answer at `path` from its saved
    /// `%%ai:` line — the single source of truth, so a live run and a reopened
    /// answer take exactly the same path (an answer with no such line, e.g. one
    /// saved before this hop existed, simply offers no check).
    void loadCheckFields(const std::string& path);
    /// True when the answer on screen can actually be checked: a query, the
    /// wolfram server, and an AppID. No AppID = the tool is not offered at all.
    bool checkAvailable() const;
    /// The composed RAM-only display text for a finished check. Never persisted:
    /// the terms prohibit caching Wolfram|Alpha content.
    std::string buildCheckText() const;

    // Flow
    void startRun(const std::string& image);
    void pumpRun();
    void openAnswer(const std::string& path);   ///< parse+layout+paginate a .md
    void rebuildResult();

    // List helpers (Menu / Capture / Recent / Settings share one row style)
    lv_obj_t* addRow(const char* text, int index, bool focused);
    /// A settings row as label (left) + value (right). Casio only: the value
    /// lines up under the wider LCD face, where a space-padded single string
    /// does not. numos keeps the concatenated `addRow` form, byte-identical.
    lv_obj_t* addRowKV(const char* label, const char* value, int index, bool focused);
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
    lv_obj_t* _scrollUp   = nullptr;   ///< casio corner scroll hint (up mask)
    lv_obj_t* _scrollDown = nullptr;   ///< casio corner scroll hint (down mask)
    std::vector<lv_obj_t*> _rows;      ///< row widgets, restyled on focus change

    ui::AppColours _sc{};              ///< resolved surface (see readSurface)
    int            _contentH = mdrender::CONTENT_H;  ///< minus the softkey band
    bool           _focusFill = true;  ///< interaction model: does focus paint?
    bool           _softkeys  = false; ///< interaction model: softkey row?
    bool           _numbered  = false; ///< interaction model: "N:" row prefixes?
    bool           _chevron   = false; ///< interaction model: corner scroll arrow?
    bool           _tallRows  = false; ///< interaction model: tall list rows (29 px
                                      ///< pitch, 1 px padding, 1.25x marks, whole-row
                                      ///< viewport) — casio only; numos keeps the
                                      ///< shared 26 px rows and 1x marks.

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

    /// The Wolfram|Alpha check for the answer on screen. `_edit` owns the
    /// digits-only query, `_wa` owns the one GET. Both are RAM only.
    ai::WolframClient _wa;
    ai::WaQueryEdit   _edit;
    std::string _toolQuery;              ///< the model's proposed query
    std::string _toolServer;             ///< what it is for ("wolfram")
    std::string _transcribed;            ///< what the model read off the photo
    std::string _sourceSlug;             ///< basename of _resultPath: <slug>_Wolfram.md
    std::string _checkText;              ///< composed display text (NEVER written)
    std::string _waNote;                 ///< one line about the last check
    bool        _waSaved = false;        ///< the sibling doc landed

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
