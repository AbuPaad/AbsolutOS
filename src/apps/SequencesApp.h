/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * SequencesApp.h — Sequences & Series application for NumOS.
 *
 * LVGL-native app with 2 tabs:
 *   Tab 0 — Define:  Define sequence expressions (u(n), v(n))
 *   Tab 1 — Table:   View computed sequence values
 *
 * Supports arithmetic, geometric, and custom recursive sequences.
 *
 * Part of: NumOS — Mathematics Suite
 */

#pragma once
// SCREEN_WIDTH / SCREEN_HEIGHT = the logical canvas declared once in Config.h
#include "../Config.h"

#include <lvgl.h>
#include "../ui/StatusBar.h"
#include "../input/KeyCodes.h"
#include "../input/KeyboardManager.h"

class SequencesApp {
public:
    SequencesApp();
    ~SequencesApp();

    void begin();
    void end();
    void load();
    void handleKey(const KeyEvent& ev);

    bool isActive() const { return _screen != nullptr; }

private:
    // ── Constants ────────────────────────────────────────────────────────
    static constexpr int SCREEN_W    = 320;
    static constexpr int SCREEN_H    = SCREEN_HEIGHT;  // canvas (Config.h)
    static constexpr int TAB_BAR_H   = 28;
    static constexpr int MAX_SEQ     = 2;      // u(n) and v(n)
    static constexpr int MAX_TERMS   = 20;
    static constexpr int MAX_EXPR    = 64;

    // ── Tabs ─────────────────────────────────────────────────────────────
    enum class Tab : uint8_t { DEFINE = 0, TABLE = 1 };
    static constexpr int NUM_TABS = 2;

    // ── LVGL objects ─────────────────────────────────────────────────────
    lv_obj_t*       _screen;
    ui::StatusBar   _statusBar;

    // Tab bar
    lv_obj_t*       _tabBar;
    lv_obj_t*       _tabBtns[NUM_TABS];
    lv_obj_t*       _tabLabels[NUM_TABS];

    // Tab 0: Define panel
    lv_obj_t*       _definePanel;
    lv_obj_t*       _seqLabels[MAX_SEQ];     // "u(n) =" / "v(n) ="
    lv_obj_t*       _seqExprs[MAX_SEQ];      // Expression display labels
    lv_obj_t*       _defineHint;

    // Tab 1: Table panel
    lv_obj_t*       _tablePanel;
    lv_obj_t*       _table;

    // ── State ────────────────────────────────────────────────────────────
    Tab             _activeTab;
    int             _selectedSeq;    // 0 = u(n), 1 = v(n)
    bool            _editing;
    char            _editBuf[MAX_EXPR];
    int             _editLen;

    // Sequence expressions & computed values
    char            _seqExprText[MAX_SEQ][MAX_EXPR];
    double          _seqValues[MAX_SEQ][MAX_TERMS];
    int             _numTerms;

    // ── Builders ─────────────────────────────────────────────────────────
    void createTabBar();
    void createDefineTab();
    void createTableTab();

    // ── Tab switching ────────────────────────────────────────────────────
    void switchTab(Tab t);
    void updateTabStyles();

    // ── Define helpers ───────────────────────────────────────────────────
    void updateDefineDisplay();
    void startEdit();
    void finishEdit();
    void cancelEdit();

    // ── Computation ──────────────────────────────────────────────────────
    void recompute();
    void updateTable();

    // ── Key handlers per tab ─────────────────────────────────────────────
    void handleKeyDefine(const KeyEvent& ev);
    void handleKeyTable(const KeyEvent& ev);
};
