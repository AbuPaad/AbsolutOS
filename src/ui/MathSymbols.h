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

#pragma once

namespace numos::mathsym {

inline constexpr const char* SYMB_INT          = "\xE2\x88\xAB";
inline constexpr const char* SYMB_SUM          = "\xE2\x88\x91";
inline constexpr const char* SYMB_SQRT         = "\xE2\x88\x9A";
inline constexpr const char* SYMB_INFINITY     = "\xE2\x88\x9E";
inline constexpr const char* SYMB_REAL         = "\xE2\x84\x9D";
inline constexpr const char* SYMB_COMPLEX      = "\xE2\x84\x82";
inline constexpr const char* SYMB_ALPHA        = "\xCE\xB1";
inline constexpr const char* SYMB_BETA         = "\xCE\xB2";
inline constexpr const char* SYMB_GAMMA        = "\xCE\xB3";
inline constexpr const char* SYMB_SUBSET       = "\xE2\x8A\x82";
inline constexpr const char* SYMB_SUBSETEQ     = "\xE2\x8A\x86";
inline constexpr const char* SYMB_ARROW_R      = "\xE2\x86\x92";
inline constexpr const char* SYMB_ARROW_L      = "\xE2\x86\x90";
inline constexpr const char* SYMB_ARROW_LR     = "\xE2\x86\x94";
inline constexpr const char* SYMB_LEQ          = "\xE2\x89\xA4";
inline constexpr const char* SYMB_GEQ          = "\xE2\x89\xA5";
inline constexpr const char* SYMB_NEQ          = "\xE2\x89\xA0";
inline constexpr const char* SYMB_PLUS_MINUS   = "\xC2\xB1";
inline constexpr const char* SYMB_TIMES        = "\xC3\x97";
inline constexpr const char* SYMB_DIVIDE       = "\xC3\xB7";
inline constexpr const char* SYMB_PARTIAL      = "\xE2\x88\x82";
inline constexpr const char* SYMB_NABLA        = "\xE2\x88\x87";
inline constexpr const char* SYMB_IN           = "\xE2\x88\x88";
inline constexpr const char* SYMB_NOT_IN       = "\xE2\x88\x89";
inline constexpr const char* SYMB_UNION        = "\xE2\x88\xAA";
inline constexpr const char* SYMB_INTERSECTION = "\xE2\x88\xA9";
inline constexpr const char* SYMB_EMPTY_SET    = "\xE2\x88\x85";
inline constexpr const char* SYMB_PI           = "\xCF\x80";
inline constexpr const char* SYMB_Z            = "\xE2\x84\xA4";
inline constexpr const char* SYMB_Q            = "\xE2\x84\x9A";
inline constexpr const char* SYMB_N            = "\xE2\x84\x95";

struct SymbolMapEntry {
    const char* token;
    const char* glyph;
};

// Token map for VPAM/MathRenderer text normalization.
inline constexpr SymbolMapEntry kVpamSymbolMap[] = {
    // GLYPH-AVAILABILITY AUDIT (2026-10-05, font = Casio Small MS/ES Sans).
    // The display face has 292 subsetted glyphs. Any token whose glyph it does
    // NOT carry was REMOVED here rather than left in: an alias that resolves to
    // an absent codepoint draws NOTHING, and silent nothing is worse than the
    // literal token, which at least stays visible and copyable. Removed:
    // \subset \subseteq \mathbbR \mathbbC and the "+-" -> U+00B1 alias —
    // that last one mattered most, because "+-" is the ASCII fallback the AI
    // system prompt tells the model to write, and this table was turning it
    // into a missing glyph.
    {"\\int", SYMB_INT},
    {"\\sum", SYMB_SUM},
    {"\\sqrt", SYMB_SQRT},
    {"\\infty", SYMB_INFINITY},
    {"\\alpha", SYMB_ALPHA},
    {"\\beta", SYMB_BETA},
    {"\\gamma", SYMB_GAMMA},
    {"<=", SYMB_LEQ},
    {">=", SYMB_GEQ},
    {"!=", SYMB_NEQ},
};

} // namespace numos::mathsym
