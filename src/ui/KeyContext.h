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
 * KeyContext.h — which keys mean something, right now.
 *
 * THIS IS THE SINGLE SOURCE OF TRUTH for contextual key relevance. Two
 * renderers read it and must not invent their own copy:
 *   1. the on-device soft-key bar (LVGL, F1..F5 caps + `legend` text);
 *   2. the web pad (wasm/numos-keycontext.js, GENERATED from this file by
 *      scripts/gen_key_context_js.py — never hand-edited).
 *
 * THE RULE: a key that is not listed for a context is NOT relevant there and
 * renders disabled/dimmed. The only exceptions are the universal escapes in
 * `defaultRole()`, so the user can never be stranded inside an app.
 *
 * Why a per-key role table instead of per-app button art: art is per KEY, the
 * SET comes from here. Per-app art is apps x keys and does not scale.
 *
 * Array order IS display order for both renderers, so the 5 device primary
 * slots cap at 5 — keep `Primary` entries to five per context.
 *
 * Pure data: no LVGL, no Arduino (agents.md §5.3). KeyCode comes from
 * input/KeyCodes.h, whose numeric values are already a frozen contract audited
 * by tests/wasm/keycode-catalog.mjs — never invent ids here.
 */

#pragma once

#include <cstdint>

#include "AppContext.h"
#include "../input/KeyCodes.h"

namespace numos {

/// How prominent a key is in the current context.
enum class Role : uint8_t {
    Primary = 0,   ///< does real work here — the soft-key bar shows these
    Secondary,     ///< valid, but not the point of this screen
    Disabled,      ///< meaningless here — dimmed, never removed
};

struct KeyRole {
    KeyCode     key;
    Role        role;
    const char* legend;   ///< short soft-key caption; "" = use the key's face
};

struct ContextMap {
    Ctx            ctx;
    const KeyRole* keys;
    uint8_t        count;
};

// ── Universal escapes ───────────────────────────────────────────────────────
// Applied to any key a context does not mention. Keeping HOME / MODE / ON alive
// everywhere is what makes "not listed = disabled" safe: there is always a way
// out of the screen and always a way to power off.
inline Role defaultRole(KeyCode key)
{
    switch (key) {
        case KeyCode::HOME:
        case KeyCode::MODE:
        case KeyCode::ON:
            return Role::Secondary;
        default:
            return Role::Disabled;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// The table
// ═══════════════════════════════════════════════════════════════════════════

/// Digits 0-9 and the arithmetic operators: shared by every entry screen.
#define NUMOS_CTX_DIGITS()                                        \
    { KeyCode::NUM_7, Role::Secondary, "" },                      \
    { KeyCode::NUM_8, Role::Secondary, "" },                      \
    { KeyCode::NUM_9, Role::Secondary, "" },                      \
    { KeyCode::NUM_4, Role::Secondary, "" },                      \
    { KeyCode::NUM_5, Role::Secondary, "" },                      \
    { KeyCode::NUM_6, Role::Secondary, "" },                      \
    { KeyCode::NUM_1, Role::Secondary, "" },                      \
    { KeyCode::NUM_2, Role::Secondary, "" },                      \
    { KeyCode::NUM_3, Role::Secondary, "" },                      \
    { KeyCode::NUM_0, Role::Secondary, "" },                      \
    { KeyCode::DOT,   Role::Secondary, "" },                      \
    { KeyCode::ADD,   Role::Secondary, "" },                      \
    { KeyCode::SUB,   Role::Secondary, "" },                      \
    { KeyCode::MUL,   Role::Secondary, "" },                      \
    { KeyCode::DIV,   Role::Secondary, "" },                      \
    { KeyCode::LPAREN, Role::Secondary, "" },                     \
    { KeyCode::RPAREN, Role::Secondary, "" },                     \
    { KeyCode::POW,   Role::Secondary, "" },                      \
    { KeyCode::SQRT,  Role::Secondary, "" }

// Splash: no input at all. Only the escape hatch survives (from defaultRole).
inline constexpr KeyRole kCtxSplash[] = {
    { KeyCode::ON, Role::Secondary, "On" },
};

// Launcher: navigate the card grid and open one.
inline constexpr KeyRole kCtxMenu[] = {
    { KeyCode::LEFT,  Role::Primary, "Prev" },
    { KeyCode::RIGHT, Role::Primary, "Next" },
    { KeyCode::UP,    Role::Primary, "Up" },
    { KeyCode::DOWN,  Role::Primary, "Down" },
    { KeyCode::ENTER, Role::Primary, "Open" },
};

// Calculator: everything is relevant, so almost nothing is Disabled. The win
// here is the reverse case — the graph-only keys really are meaningless.
inline constexpr KeyRole kCtxCalculation[] = {
    { KeyCode::SHIFT,     Role::Primary, "Shift" },
    { KeyCode::ALPHA,     Role::Primary, "Alpha" },
    { KeyCode::AC,        Role::Primary, "Clear" },
    { KeyCode::DEL,       Role::Primary, "Del" },
    { KeyCode::ENTER,     Role::Primary, "=" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::SIN,       Role::Secondary, "" },
    { KeyCode::COS,       Role::Secondary, "" },
    { KeyCode::TAN,       Role::Secondary, "" },
    { KeyCode::LN,        Role::Secondary, "" },
    { KeyCode::LOG,       Role::Secondary, "" },
    { KeyCode::ANS,       Role::Secondary, "" },
    { KeyCode::NEG,       Role::Secondary, "" },
    { KeyCode::FRAC,      Role::Secondary, "" },
    { KeyCode::SQUARE,    Role::Secondary, "" },
    { KeyCode::EXP,       Role::Secondary, "" },
    { KeyCode::CONST_PI,  Role::Secondary, "" },
    { KeyCode::CONST_E,   Role::Secondary, "" },
    { KeyCode::VAR_X,     Role::Secondary, "" },
    { KeyCode::VAR_Y,     Role::Secondary, "" },
    { KeyCode::FORMAT,    Role::Secondary, "" },
    { KeyCode::FREE_EQ,   Role::Secondary, "S<>D" },
    { KeyCode::SHOW_STEPS, Role::Disabled, "" },
    { KeyCode::SOLVE,     Role::Disabled, "" },
    { KeyCode::GRAPH,     Role::Disabled, "" },
    { KeyCode::ZOOM,      Role::Disabled, "" },
    { KeyCode::TRACE,     Role::Disabled, "" },
    { KeyCode::TABLE,     Role::Disabled, "" },
};

// Grapher: the window controls are the point; the pad pans and zooms.
inline constexpr KeyRole kCtxGrapher[] = {
    { KeyCode::LEFT,      Role::Primary, "Left" },
    { KeyCode::RIGHT,     Role::Primary, "Right" },
    { KeyCode::UP,        Role::Primary, "Up" },
    { KeyCode::DOWN,      Role::Primary, "Down" },
    { KeyCode::ZOOM,      Role::Primary, "Zoom" },
    { KeyCode::GRAPH,     Role::Secondary, "Draw" },
    { KeyCode::TRACE,     Role::Secondary, "Trace" },
    { KeyCode::TABLE,     Role::Secondary, "Table" },
    { KeyCode::VAR_X,     Role::Secondary, "x" },
    { KeyCode::VAR_Y,     Role::Secondary, "y" },
    { KeyCode::LESS,      Role::Secondary, "<" },
    { KeyCode::GREATER,   Role::Secondary, ">" },
    { KeyCode::AC,        Role::Secondary, "Clear" },
    { KeyCode::DEL,       Role::Secondary, "Del" },
    { KeyCode::SHIFT,     Role::Secondary, "" },
    { KeyCode::ALPHA,     Role::Secondary, "" },
    { KeyCode::ENTER,     Role::Secondary, "" },
    NUMOS_CTX_DIGITS(),
};

// Equations: read it, edit it, solve it, inspect the steps.
inline constexpr KeyRole kCtxEquations[] = {
    { KeyCode::SOLVE,      Role::Primary, "Solve" },
    { KeyCode::SHOW_STEPS, Role::Primary, "Steps" },
    { KeyCode::ENTER,      Role::Primary, "Run" },
    { KeyCode::LEFT,       Role::Primary, "Left" },
    { KeyCode::RIGHT,      Role::Primary, "Right" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::VAR_X,      Role::Secondary, "x" },
    { KeyCode::VAR_Y,      Role::Secondary, "y" },
    { KeyCode::EQUAL,      Role::Secondary, "" },
    { KeyCode::AC,         Role::Secondary, "Clear" },
    { KeyCode::DEL,        Role::Secondary, "Del" },
    { KeyCode::UP,         Role::Secondary, "" },
    { KeyCode::DOWN,       Role::Secondary, "" },
    { KeyCode::GRAPH,      Role::Secondary, "Draw" },
    { KeyCode::ZOOM,       Role::Disabled, "" },
    { KeyCode::TRACE,      Role::Disabled, "" },
    { KeyCode::TABLE,      Role::Disabled, "" },
};

// Calculus: the differentiator is showing steps, so it leads.
inline constexpr KeyRole kCtxCalculus[] = {
    { KeyCode::SHOW_STEPS, Role::Primary, "Steps" },
    { KeyCode::SOLVE,      Role::Primary, "Eval" },
    { KeyCode::ENTER,      Role::Primary, "Run" },
    { KeyCode::LEFT,       Role::Primary, "Left" },
    { KeyCode::RIGHT,      Role::Primary, "Right" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::VAR_X,      Role::Secondary, "x" },
    { KeyCode::VAR_Y,      Role::Secondary, "y" },
    { KeyCode::AC,         Role::Secondary, "Clear" },
    { KeyCode::DEL,        Role::Secondary, "Del" },
    { KeyCode::GRAPH,      Role::Secondary, "Draw" },
    { KeyCode::ZOOM,       Role::Disabled, "" },
    { KeyCode::TRACE,      Role::Disabled, "" },
    { KeyCode::TABLE,      Role::Disabled, "" },
};

// Statistics / Probability / Sequences: a moving cursor over rows of numbers.
inline constexpr KeyRole kCtxStatistics[] = {
    { KeyCode::UP,    Role::Primary, "Row -" },
    { KeyCode::DOWN,  Role::Primary, "Row +" },
    { KeyCode::ENTER, Role::Primary, "Select" },
    { KeyCode::DEL,   Role::Primary, "Del" },
    { KeyCode::AC,    Role::Primary, "Clear" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::LEFT,  Role::Secondary, "Col -" },
    { KeyCode::RIGHT, Role::Secondary, "Col +" },
    { KeyCode::GRAPH, Role::Secondary, "Plot" },
    { KeyCode::SHIFT, Role::Secondary, "" },
    { KeyCode::ZOOM,  Role::Disabled, "" },
    { KeyCode::TRACE, Role::Disabled, "" },
    { KeyCode::SHOW_STEPS, Role::Disabled, "" },
    { KeyCode::SOLVE, Role::Disabled, "" },
};

inline constexpr KeyRole kCtxProbability[] = {
    { KeyCode::UP,    Role::Primary, "Row -" },
    { KeyCode::DOWN,  Role::Primary, "Row +" },
    { KeyCode::ENTER, Role::Primary, "Select" },
    { KeyCode::DEL,   Role::Primary, "Del" },
    { KeyCode::AC,    Role::Primary, "Clear" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::LEFT,  Role::Secondary, "Col -" },
    { KeyCode::RIGHT, Role::Secondary, "Col +" },
    { KeyCode::GRAPH, Role::Secondary, "Plot" },
    { KeyCode::ZOOM,  Role::Disabled, "" },
    { KeyCode::TRACE, Role::Disabled, "" },
    { KeyCode::SHOW_STEPS, Role::Disabled, "" },
    { KeyCode::SOLVE, Role::Disabled, "" },
};

inline constexpr KeyRole kCtxRegression[] = {
    { KeyCode::GRAPH, Role::Primary, "Fit" },
    { KeyCode::UP,    Role::Primary, "Row -" },
    { KeyCode::DOWN,  Role::Primary, "Row +" },
    { KeyCode::ENTER, Role::Primary, "Select" },
    { KeyCode::DEL,   Role::Primary, "Del" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::LEFT,  Role::Secondary, "Col -" },
    { KeyCode::RIGHT, Role::Secondary, "Col +" },
    { KeyCode::AC,    Role::Secondary, "Clear" },
    { KeyCode::ZOOM,  Role::Disabled, "" },
    { KeyCode::TRACE, Role::Disabled, "" },
    { KeyCode::SHOW_STEPS, Role::Disabled, "" },
    { KeyCode::SOLVE, Role::Disabled, "" },
};

inline constexpr KeyRole kCtxSequences[] = {
    { KeyCode::UP,    Role::Primary, "Term -" },
    { KeyCode::DOWN,  Role::Primary, "Term +" },
    { KeyCode::ENTER, Role::Primary, "Select" },
    { KeyCode::DEL,   Role::Primary, "Del" },
    { KeyCode::AC,    Role::Primary, "Clear" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::VAR_X, Role::Secondary, "x" },
    { KeyCode::TABLE, Role::Secondary, "Table" },
    { KeyCode::GRAPH, Role::Secondary, "Plot" },
    { KeyCode::ZOOM,  Role::Disabled, "" },
    { KeyCode::TRACE, Role::Disabled, "" },
    { KeyCode::SHOW_STEPS, Role::Disabled, "" },
    { KeyCode::SOLVE, Role::Disabled, "" },
};

// Game Boy: the single clearest demonstration of the whole feature — a D-pad
// and two buttons, with the entire maths keypad dead.
inline constexpr KeyRole kCtxGameBoy[] = {
    { KeyCode::LEFT,  Role::Primary, "Left" },
    { KeyCode::RIGHT, Role::Primary, "Right" },
    { KeyCode::UP,    Role::Primary, "Up" },
    { KeyCode::DOWN,  Role::Primary, "Down" },
    { KeyCode::ENTER, Role::Primary, "A" },
    { KeyCode::EXE,   Role::Secondary, "B" },
    { KeyCode::F1,    Role::Secondary, "Start" },
    { KeyCode::F2,    Role::Secondary, "Select" },
    { KeyCode::SHIFT, Role::Secondary, "Run" },
    { KeyCode::AC,    Role::Secondary, "Reset" },
};

// Notes: pure reading. Page and line movement only.
inline constexpr KeyRole kCtxNotes[] = {
    { KeyCode::LEFT,  Role::Primary, "Pg -" },
    { KeyCode::RIGHT, Role::Primary, "Pg +" },
    { KeyCode::UP,    Role::Primary, "Line -" },
    { KeyCode::DOWN,  Role::Primary, "Line +" },
    { KeyCode::BACK,  Role::Primary, "Back" },
    { KeyCode::ENTER, Role::Secondary, "" },
    { KeyCode::AC,    Role::Secondary, "Clear" },
    { KeyCode::SHIFT, Role::Secondary, "" },
};

// AI: navigate, trigger the verification hop, abort. NUM_7 is the tool trigger
// ("press 7"), so it is a first-class key here and dead everywhere else.
inline constexpr KeyRole kCtxAi[] = {
    { KeyCode::UP,    Role::Primary, "Line -" },
    { KeyCode::DOWN,  Role::Primary, "Line +" },
    { KeyCode::ENTER, Role::Primary, "OK" },
    { KeyCode::NUM_7, Role::Primary, "Verify" },
    { KeyCode::MODE,  Role::Primary, "Abort" },
    { KeyCode::LEFT,  Role::Secondary, "Pg -" },
    { KeyCode::RIGHT, Role::Secondary, "Pg +" },
    { KeyCode::BACK,  Role::Secondary, "Back" },
    { KeyCode::SHIFT, Role::Secondary, "" },
};

inline constexpr KeyRole kCtxSettings[] = {
    { KeyCode::UP,    Role::Primary, "Up" },
    { KeyCode::DOWN,  Role::Primary, "Down" },
    { KeyCode::LEFT,  Role::Primary, "Left" },
    { KeyCode::RIGHT, Role::Primary, "Right" },
    { KeyCode::ENTER, Role::Primary, "Toggle" },
    { KeyCode::BACK,  Role::Secondary, "Back" },
};

// Showcases: a demo reel, so the only real keys are the ones that step it.
inline constexpr KeyRole kCtxShowcase[] = {
    { KeyCode::LEFT,  Role::Primary, "Prev" },
    { KeyCode::RIGHT, Role::Primary, "Next" },
    { KeyCode::UP,    Role::Primary, "Up" },
    { KeyCode::DOWN,  Role::Primary, "Down" },
    { KeyCode::AC,    Role::Secondary, "Clear" },
};

inline constexpr KeyRole kCtxNeoLanguage[] = {
    { KeyCode::ENTER, Role::Primary, "Run" },
    { KeyCode::AC,    Role::Primary, "Clear" },
    { KeyCode::LEFT,  Role::Primary, "Left" },
    { KeyCode::RIGHT, Role::Primary, "Right" },
    { KeyCode::DEL,   Role::Primary, "Del" },
    NUMOS_CTX_DIGITS(),
    { KeyCode::UP,    Role::Secondary, "" },
    { KeyCode::DOWN,  Role::Secondary, "" },
};

#define NUMOS_CTX_ENTRY(name, arr) \
    { Ctx::name, arr, static_cast<uint8_t>(sizeof(arr) / sizeof(arr[0])) }

inline constexpr ContextMap kContextTable[] = {
    NUMOS_CTX_ENTRY(Splash,       kCtxSplash),
    NUMOS_CTX_ENTRY(Menu,         kCtxMenu),
    NUMOS_CTX_ENTRY(Calculation,  kCtxCalculation),
    NUMOS_CTX_ENTRY(Grapher,      kCtxGrapher),
    NUMOS_CTX_ENTRY(Equations,    kCtxEquations),
    NUMOS_CTX_ENTRY(Calculus,     kCtxCalculus),
    NUMOS_CTX_ENTRY(Statistics,   kCtxStatistics),
    NUMOS_CTX_ENTRY(Probability,  kCtxProbability),
    NUMOS_CTX_ENTRY(Regression,   kCtxRegression),
    NUMOS_CTX_ENTRY(Sequences,    kCtxSequences),
    NUMOS_CTX_ENTRY(GameBoy,      kCtxGameBoy),
    NUMOS_CTX_ENTRY(Notes,        kCtxNotes),
    NUMOS_CTX_ENTRY(Ai,           kCtxAi),
    NUMOS_CTX_ENTRY(Settings,     kCtxSettings),
    NUMOS_CTX_ENTRY(MathShowcase, kCtxShowcase),
    NUMOS_CTX_ENTRY(MathVisual,   kCtxShowcase),
    NUMOS_CTX_ENTRY(NeoLanguage,  kCtxNeoLanguage),
};

#undef NUMOS_CTX_ENTRY

inline const ContextMap* contextFor(Ctx ctx)
{
    for (const ContextMap& entry : kContextTable) {
        if (entry.ctx == ctx) return &entry;
    }
    return nullptr;
}

/// Role of one key in one context. Unlisted keys fall back to defaultRole(),
/// which is what makes "not listed = not relevant" safe.
inline Role ctxRoleFor(Ctx ctx, KeyCode key)
{
    const ContextMap* map = contextFor(ctx);
    if (map) {
        for (uint8_t i = 0; i < map->count; ++i) {
            if (map->keys[i].key == key) return map->keys[i].role;
        }
    }
    return defaultRole(key);
}

/// Soft-key caption for a key, or "" when the key's own face is the label.
inline const char* ctxLegendFor(Ctx ctx, KeyCode key)
{
    const ContextMap* map = contextFor(ctx);
    if (map) {
        for (uint8_t i = 0; i < map->count; ++i) {
            if (map->keys[i].key == key) return map->keys[i].legend;
        }
    }
    return "";
}

}  // namespace numos
