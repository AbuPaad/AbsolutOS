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
 * AppContext.h — the shared identity of "which app are we in".
 *
 * WHY THIS EXISTS
 * ---------------
 * Contextual UI (the on-device soft-key bar, the web pad highlight) has to key
 * off a *stable* app identity that BOTH targets can name. The emulator's
 * `activeAppName()` lives in hal/NativeHal.cpp, which is emulator-only, so the
 * device cannot call it. Hence one small enum in shared, hardware-free code:
 * the emulator maps AppMode -> Ctx, the device maps its own Mode -> Ctx, and
 * everything downstream (the key table, the print channel, the generated JS)
 * keys off `Ctx` and `ctxSlug()`.
 *
 * RULE: the numeric values of Ctx are a public contract (the `@ctx` serial line
 * and the generated wasm/numos-keycontext.js both carry them). Append only.
 *
 * No LVGL, no Arduino, no hardware headers — this is model-layer code and
 * agents.md §5.3 forbids naming hardware APIs from here.
 */

#pragma once

#include <cstdint>

namespace numos {

enum class Ctx : uint8_t {
    Splash = 0,
    Menu,
    Calculation,
    Grapher,
    Equations,
    Calculus,
    Statistics,
    Probability,
    Regression,
    Sequences,
    GameBoy,
    Notes,
    Ai,
    Settings,
    MathShowcase,
    MathVisual,
    NeoLanguage,
    Count
};

/**
 * Human-facing name. These strings are byte-identical to the legacy
 * `activeAppName()` output on purpose: that function had exactly this set, and
 * the emulator's diagnostics JSON (`numos_diagnostic_state()`) carries it, so
 * changing one without the other would silently split the contract.
 */
inline const char* ctxName(Ctx ctx)
{
    switch (ctx) {
        case Ctx::Splash:       return "Splash";
        case Ctx::Menu:         return "Menu";
        case Ctx::Calculation:  return "Calculation";
        case Ctx::Grapher:      return "Grapher";
        case Ctx::Equations:    return "Equations";
        case Ctx::Calculus:     return "Calculus";
        case Ctx::Statistics:   return "Statistics";
        case Ctx::Probability:  return "Probability";
        case Ctx::Regression:   return "Regression";
        case Ctx::Sequences:    return "Sequences";
        case Ctx::GameBoy:      return "Game Boy";
        case Ctx::Notes:        return "Notes";
        case Ctx::Ai:           return "AI";
        case Ctx::Settings:     return "Settings";
        case Ctx::MathShowcase: return "MathShowcase";
        case Ctx::MathVisual:   return "Math Visual";
        case Ctx::NeoLanguage:  return "NeoLanguage";
        case Ctx::Count:        break;
    }
    return "Calculation";
}

/**
 * Machine-facing slug: lowercase, no spaces, ASCII — safe inside a CSS
 * attribute selector (`[data-ctx="grapher"]`), a serial field, and a JS object
 * key. scripts/gen_key_context_js.py reads these from this very function, so
 * the C++ and the JavaScript cannot drift to different spellings.
 */
inline const char* ctxSlug(Ctx ctx)
{
    switch (ctx) {
        case Ctx::Splash:       return "splash";
        case Ctx::Menu:         return "menu";
        case Ctx::Calculation:  return "calculation";
        case Ctx::Grapher:      return "grapher";
        case Ctx::Equations:    return "equations";
        case Ctx::Calculus:     return "calculus";
        case Ctx::Statistics:   return "statistics";
        case Ctx::Probability:  return "probability";
        case Ctx::Regression:   return "regression";
        case Ctx::Sequences:    return "sequences";
        case Ctx::GameBoy:      return "gameboy";
        case Ctx::Notes:        return "notes";
        case Ctx::Ai:           return "ai";
        case Ctx::Settings:     return "settings";
        case Ctx::MathShowcase: return "mathshowcase";
        case Ctx::MathVisual:   return "mathvisual";
        case Ctx::NeoLanguage:  return "neolanguage";
        case Ctx::Count:        break;
    }
    return "calculation";
}

/// True for every value that names a real context (guards bad serial input).
inline bool ctxIsValid(int value)
{
    return value >= 0 && value < static_cast<int>(Ctx::Count);
}

}  // namespace numos
