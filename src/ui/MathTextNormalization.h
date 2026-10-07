/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <cstddef>
#include <cstring>

#include "MathSymbols.h"

namespace numos::mathsym {

inline constexpr std::size_t kNormalizedTextBufferBytes = 96;

enum class NormalizeTextStatus : unsigned char {
    Unchanged,
    Buffered,
    InsufficientBuffer,
};

struct NormalizedMathText {
    const char* source;
    const char* text;
    NormalizeTextStatus status;
};

struct TextAlias {
    const char* token;
    const char* utf8;
};

// Additional ASCII token -> UTF-8 fallback map for broad math symbol input.
inline constexpr TextAlias kExtendedTextAliases[] = {
    // GLYPH-AVAILABILITY AUDIT (2026-10-05, font = Casio Small MS/ES Sans).
    // 53 of the entries below pointed at codepoints the subsetted face does not
    // carry (all of set theory, logic, the big operators \iint/\iiint/\oint,
    // double-struck sets, fences, relations like \approx \equiv \sim, and the
    // arrows other than \to and \leftarrow). They were removed so the
    // normaliser can no longer substitute a glyph that renders as nothing.
    // What remains is exactly what the face can draw. See the AI system prompt,
    // which is restricted to the same set.
    {"\\alpha", SYMB_ALPHA},
    {"\\beta", SYMB_BETA},
    {"\\Gamma", "\xCE\x93"},
    {"\\gamma", SYMB_GAMMA},
    {"\\Delta", "\xCE\x94"},
    {"\\delta", "\xCE\xB4"},
    {"\\epsilon", "\xCE\xB5"},
    {"\\zeta", "\xCE\xB6"},
    {"\\eta", "\xCE\xB7"},
    {"\\Theta", "\xCE\x98"},
    {"\\theta", "\xCE\xB8"},
    {"\\iota", "\xCE\xB9"},
    {"\\kappa", "\xCE\xBA"},
    {"\\Lambda", "\xCE\x9B"},
    {"\\lambda", "\xCE\xBB"},
    {"\\mu", "\xCE\xBC"},
    {"\\nu", "\xCE\xBD"},
    {"\\Xi", "\xCE\x9E"},
    {"\\xi", "\xCE\xBE"},
    {"\\Pi", "\xCE\xA0"},
    {"\\pi", "\xCF\x80"},
    {"\\rho", "\xCF\x81"},
    {"\\Sigma", "\xCE\xA3"},
    {"\\sigma", "\xCF\x83"},
    {"\\tau", "\xCF\x84"},
    {"\\Upsilon", "\xCE\xA5"},
    {"\\upsilon", "\xCF\x85"},
    {"\\Phi", "\xCE\xA6"},
    {"\\phi", "\xCF\x86"},
    {"\\chi", "\xCF\x87"},
    {"\\Psi", "\xCE\xA8"},
    {"\\psi", "\xCF\x88"},
    {"\\Omega", "\xCE\xA9"},
    {"\\omega", "\xCF\x89"},
    {"\\infty", SYMB_INFINITY},
    {"\\int", SYMB_INT},
    {"\\implies", "\xE2\x87\x92"},
    {"\\to", SYMB_ARROW_R},
    {"\\leftarrow", SYMB_ARROW_L},
    {"\\angle", "\xE2\x88\xA0"},
    {"\\degree", "\xC2\xB0"},
    {"\\leq", SYMB_LEQ},
    {"\\geq", SYMB_GEQ},
    {"\\neq", SYMB_NEQ},
    {"\\times", SYMB_TIMES},
    {"\\hbar", "\xE2\x84\x8F"},
};

struct TextReplacement {
    const char* token;
    const char* utf8;
    std::size_t tokenLen;
    std::size_t utf8Len;
};

inline bool startsWithToken(const char* text, const char* token) {
    return std::strncmp(text, token, std::strlen(token)) == 0;
}

inline TextReplacement findTextReplacementAt(const char* text) {
    for (const auto& entry : kVpamSymbolMap) {
        if (startsWithToken(text, entry.token)) {
            return {entry.token, entry.glyph,
                    std::strlen(entry.token), std::strlen(entry.glyph)};
        }
    }

    for (const auto& entry : kExtendedTextAliases) {
        if (startsWithToken(text, entry.token)) {
            return {entry.token, entry.utf8,
                    std::strlen(entry.token), std::strlen(entry.utf8)};
        }
    }

    return {nullptr, nullptr, 0, 0};
}

inline NormalizedMathText normalizeMathTextNoAlloc(const char* input,
                                                   char* buffer,
                                                   std::size_t bufferSize) {
    if (input == nullptr) {
        return {nullptr, nullptr, NormalizeTextStatus::Unchanged};
    }

    bool changed = false;
    std::size_t outputLen = 0;
    for (const char* p = input; *p != '\0';) {
        const TextReplacement replacement = findTextReplacementAt(p);
        if (replacement.token != nullptr) {
            changed = true;
            outputLen += replacement.utf8Len;
            p += replacement.tokenLen;
        } else {
            ++outputLen;
            ++p;
        }
    }

    if (!changed) {
        return {input, input, NormalizeTextStatus::Unchanged};
    }

    if (buffer == nullptr || bufferSize == 0 || outputLen + 1 > bufferSize) {
        return {input, input, NormalizeTextStatus::InsufficientBuffer};
    }

    char* out = buffer;
    for (const char* p = input; *p != '\0';) {
        const TextReplacement replacement = findTextReplacementAt(p);
        if (replacement.token != nullptr) {
            std::memcpy(out, replacement.utf8, replacement.utf8Len);
            out += replacement.utf8Len;
            p += replacement.tokenLen;
        } else {
            *out++ = *p++;
        }
    }
    *out = '\0';

    return {input, buffer, NormalizeTextStatus::Buffered};
}

} // namespace numos::mathsym
