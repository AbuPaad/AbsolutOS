/*
 * NumOS — HelpContent.h
 * Copyright (C) 2026 Juan Ramon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The per-app key table behind the help overlay.
 *
 * The bottom hint bar and the Calculus tab strip used to document the controls;
 * both are gone. This table is now the only place a user can learn what a screen
 * accepts, so it is data, not a drawing: one row per key, key first.
 *
 * Keys are written as they are printed on the shell: SHIFT and ALPHA as words,
 * the rest as the glyph the main face can draw. No LV_SYMBOL_* anywhere in here.
 */

#pragma once

namespace ui {
namespace help {

struct Row {
    const char* keys;   // "SHIFT+EXE"
    const char* what;   // "switch d/dx <-> integral"
};

// ── Calculus ────────────────────────────────────────────────────────────────
static const Row kCalculus[] = {
    { "0-9 . x + -",       "type the function f(x)" },
    { "EXE",               "differentiate / integrate" },
    { "SHIFT+EXE",         "switch between d/dx and integral" },
    { "GRAPH",             "build f(x) with the function keys" },
    { "AC",                "clear the entry" },
    { "AC (empty)",        "back to the launcher" },
};

// ── Grapher ─────────────────────────────────────────────────────────────────
static const Row kGrapher[] = {
    { "1-5",               "select a function (colour dots on the left)" },
    { "EXE",               "open the editor for the selected function" },
    { "1-5 again",         "same as EXE, for the function already selected" },
    { "GRAPH",             "draw the plot" },
    { "TABLE",             "value table for the selected function" },
    { "arrows",            "pan the view" },
    { "SHIFT+arrows",      "zoom in / out" },
    { "AC",                "close the editor / reset the view" },
};

// ── Calculation ─────────────────────────────────────────────────────────────
static const Row kCalculation[] = {
    { "0-9 . + - * /",     "enter an expression" },
    { "EXE",               "evaluate" },
    { "DEL",               "delete one character" },
    { "AC",                "clear" },
    { "SHIFT+7",           "this screen" },
};

// ── Launcher ────────────────────────────────────────────────────────────────
static const Row kLauncher[] = {
    { "arrows",            "move between apps" },
    { "EXE",               "open the app" },
    { "SHIFT+7",           "this screen" },
};

// ── fallback for every other app ────────────────────────────────────────────
static const Row kGeneric[] = {
    { "arrows",            "move / change selection" },
    { "EXE",               "confirm" },
    { "AC",                "back one level" },
    { "SHIFT+7",           "this screen" },
};

struct Page {
    const char* title;
    const Row*  rows;
    int         count;
};

inline Page page(int appId) {
    switch (appId) {
        case 0:  return { "Calculation", kCalculation, (int)(sizeof(kCalculation) / sizeof(Row)) };
        case 1:  return { "Grapher",     kGrapher,     (int)(sizeof(kGrapher)     / sizeof(Row)) };
        case 3:  return { "Calculus",    kCalculus,    (int)(sizeof(kCalculus)    / sizeof(Row)) };
        case -1: return { "Launcher",    kLauncher,    (int)(sizeof(kLauncher)    / sizeof(Row)) };
        default: return { "Help",        kGeneric,     (int)(sizeof(kGeneric)     / sizeof(Row)) };
    }
}

} // namespace help
} // namespace ui
