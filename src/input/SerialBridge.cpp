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
 * SerialBridge.cpp
 * PC → NumOS keyboard bridge.
 * Non-blocking Serial reader that generates KeyEvent structs.
 */

#include "SerialBridge.h"
#include "NumosSerialBackend.h"
#include "math/giac/GiacBridge.h"
#include <ctype.h>
#include <string>
#include <algorithm>

#define Serial NUMOS_SERIAL

namespace {

// Named keys, shared by the bare-word form ("DEL") and the hold/release forms
// ("+DEL" / "-DEL") so the two spellings can never drift apart.
struct NamedKey {
    const char* name;
    KeyCode code;
};

const NamedKey kNamedKeys[] = {
    {"HOME", KeyCode::MODE},   {"AC", KeyCode::AC},   {"DEL", KeyCode::DEL},
    {"ENTER", KeyCode::ENTER}, {"EXE", KeyCode::EXE}, {"F1", KeyCode::F1},
    {"F2", KeyCode::F2},       {"F3", KeyCode::F3},   {"F4", KeyCode::F4},
    {"F5", KeyCode::F5},       {"SIN", KeyCode::SIN}, {"COS", KeyCode::COS},
    {"TAN", KeyCode::TAN},     {"LN", KeyCode::LN},   {"LOG", KeyCode::LOG},
    {"SQRT", KeyCode::SQRT},   {"ANS", KeyCode::ANS}, {"PI", KeyCode::CONST_PI},
};

bool lookupNamedKey(const std::string& upper, KeyCode& out) {
    for (const NamedKey& entry : kNamedKeys) {
        if (upper == entry.name) { out = entry.code; return true; }
    }
    return false;
}

// Arrow shortcut letters, for "+w"/"-d" style hold/release.
bool lookupArrowKey(char upperChar, KeyCode& out) {
    switch (upperChar) {
        case 'W': out = KeyCode::UP;    return true;
        case 'S': out = KeyCode::DOWN;  return true;
        case 'A': out = KeyCode::LEFT;  return true;
        case 'D': out = KeyCode::RIGHT; return true;
        default:  return false;
    }
}

// Keys whose consumers act on edges, so a PRESS must be followed by a RELEASE.
// SHIFT and ALPHA are deliberately absent: SystemApp handles them as latches and
// toggles the modifier on ANY matching event regardless of action
// (SystemApp.cpp handleKey), so an injected RELEASE would toggle them back off.
bool isMomentary(KeyCode code) {
    switch (code) {
        case KeyCode::UP:
        case KeyCode::DOWN:
        case KeyCode::LEFT:
        case KeyCode::RIGHT:
        case KeyCode::ENTER:
        case KeyCode::EXE:
        case KeyCode::DEL:
        case KeyCode::F1:
        case KeyCode::F2:
        case KeyCode::F3:
        case KeyCode::F4:
        case KeyCode::F5:
            return true;
        default:
            return false;
    }
}

const char* keyActionName(KeyAction action) {
    switch (action) {
        case KeyAction::PRESS:   return "PRESS";
        case KeyAction::RELEASE: return "RELEASE";
        case KeyAction::REPEAT:  return "REPEAT";
        default:                 return "?";
    }
}

} // namespace

SerialBridge::SerialBridge()
    : _head(0), _tail(0)
{
    memset(_buf, 0, sizeof(_buf));
}

void SerialBridge::begin() {
    Serial.println("[SerialBridge] PC keyboard bridge active.");
    Serial.println("┌───────────────────────────────────────────────┐");
    Serial.println("│         NumOS Serial Keyboard Map             │");
    Serial.println("├──────────────┬────────────────────────────────┤");
    Serial.println("[│  0-9 . + - * /  │  Digits & operators        │");
    Serial.println("│  = or u           │  EQUALS symbol (not EXE)  │");
    Serial.println("│  Enter (\\r/\\n)  │  EXE / OK                 │");
    Serial.println("│  Backspace/Del   │  DEL (erase char)         │");
    Serial.println("│  c  or  Esc      │  AC  (all clear)          │");
    Serial.println("│  p  or  ^        │  POW (exponent)           │");
    Serial.println("│  x               │  VAR_X                    │");
    Serial.println("│  y               │  VAR_Y                    │");
    Serial.println("│  f               │  FRAC (fraction)          │");
    Serial.println("│  r               │  SQRT (square root)       │");
    Serial.println("│  R (shift+r)     │  nthROOT (SHIFT+SQRT)     │");
    Serial.println("│  n               │  STEPS (step-by-step)     │");
    Serial.println("│  ( )             │  Parentheses              │");
    Serial.println("│  w a s d         │  UP LEFT DOWN RIGHT       │");
    Serial.println("│  S (shift+s)     │  SHIFT modifier           │");
    Serial.println("│  A (shift+a)     │  ALPHA modifier           │");
    Serial.println("│  h               │  MODE / HOME              │");
    Serial.println("│  g               │  GRAPH                    │");
    Serial.println("│  t               │  SIN                      │");
    Serial.println("└──────────────┴────────────────────────────────┘");
    Serial.println("[SerialBridge] Type a key and press Enter.");
    Serial.printf("[SerialBridge] Momentary keys auto-release after %ums.\n",
                  static_cast<unsigned>(kPulseMs));
    Serial.println("[SerialBridge] Hold: '+KEY' (e.g. +D, +ENTER) | "
                   "Release: '-KEY' | 'RELEASE ALL'");
}

void SerialBridge::setLineHandler(LineHandler handler, void* context) {
    _lineHandler = handler;
    _lineHandlerContext = context;
}

// ── Circular buffer helpers ──

void SerialBridge::push(KeyCode code, const char* label,
                        KeyAction action, bool autoRelease) {
    KeyEvent ev;
    ev.code   = code;
    ev.action = action;
    ev.row    = -1;  // Virtual key (no physical row/col)
    ev.col    = -1;

    int next = (_head + 1) % BUF_SIZE;
    if (next == _tail) return;  // Buffer full, drop event

    _buf[_head] = ev;
    _head = next;

    // Remember what the bridge still owes a RELEASE for: a PRESS alone latches
    // in every edge-driven consumer (Game Boy core, pickers).
    if (action == KeyAction::PRESS) {
        if (!autoRelease) {
            trackHeld(code, 0);            // held until "-KEY" / "RELEASE ALL"
        } else if (isMomentary(code)) {
            trackHeld(code, millis() + kPulseMs);
        }
    }

    // Debug feedback
    Serial.printf("[Key] PC Input: '%s' (%s)\n", label, keyActionName(action));
}

bool SerialBridge::pop(KeyEvent &out) {
    if (_tail == _head) return false;
    out = _buf[_tail];
    _tail = (_tail + 1) % BUF_SIZE;
    return true;
}

// ── Held-key bookkeeping ──

void SerialBridge::trackHeld(KeyCode code, uint32_t dueMs) {
    for (int i = 0; i < _heldCount; ++i) {
        if (_held[i].code == code) {
            _held[i].dueMs = dueMs;  // re-press refreshes the hold window
            return;
        }
    }
    if (_heldCount >= MAX_HELD) return;  // full: drop the bookkeeping, not the key
    _held[_heldCount].code  = code;
    _held[_heldCount].dueMs = dueMs;
    ++_heldCount;
}

void SerialBridge::releaseCode(KeyCode code, const char* label) {
    for (int i = 0; i < _heldCount; ++i) {
        if (_held[i].code == code) {
            _held[i] = _held[_heldCount - 1];
            --_heldCount;
            break;
        }
    }
    push(code, label, KeyAction::RELEASE, /*autoRelease=*/false);
}

void SerialBridge::releaseAllHeld() {
    for (int i = 0; i < _heldCount; ++i) {
        push(_held[i].code, "(release all)", KeyAction::RELEASE,
             /*autoRelease=*/false);
    }
    _heldCount = 0;
    Serial.println("[SB] released all held keys");
}

void SerialBridge::servicePendingReleases() {
    const uint32_t now = millis();
    for (int i = 0; i < _heldCount; ) {
        if (_held[i].dueMs != 0 &&
            static_cast<int32_t>(now - _held[i].dueMs) >= 0) {
            push(_held[i].code, "(pulse)", KeyAction::RELEASE,
                 /*autoRelease=*/false);
            _held[i] = _held[_heldCount - 1];
            --_heldCount;
        } else {
            ++i;
        }
    }
}

// ── Main API ──

bool SerialBridge::pollEvent(KeyEvent &outEvent) {
    // 1. Read all available serial chars and convert to events
    while (Serial.available() > 0) {
        int ch = Serial.read();
        processChar(ch);
    }

    // 2. Turn elapsed hold windows into RELEASE events
    servicePendingReleases();

    // 3. Pop one event from queue
    return pop(outEvent);
}

// ── Character → KeyCode mapping ──

void SerialBridge::processChar(int ch) {
    // Use a std::string buffer to accumulate characters until newline.
    static std::string inputBuffer;
    static unsigned long lastEnterMs = 0;

    // Echo and accumulate printable characters
    if (ch >= 0x20 && ch <= 0x7E) {
        Serial.printf("[SB] RX: '%c' (0x%02X)\n", (char)ch, ch);
        if (inputBuffer.size() < 255) inputBuffer.push_back((char)ch);
    } else {
        Serial.printf("[SB] RX: 0x%02X\n", ch);
        // Backspace/delete should edit the current buffer if present
        if ((ch == 8 || ch == 127) && !inputBuffer.empty()) {
            inputBuffer.pop_back();
            Serial.println("[SB] (buffer) DEL");
        }
    }

    // Handle Enter / newline (debounce CR+LF pairs)
    if (ch == '\r' || ch == '\n') {
        unsigned long now = millis();
        if (now - lastEnterMs < 50) {
            // duplicate newline from CR/LF, ignore
            return;
        }
        lastEnterMs = now;

        // Trim leading/trailing whitespace
        auto lpos = inputBuffer.find_first_not_of(" \t\r\n");
        auto rpos = inputBuffer.find_last_not_of(" \t\r\n");
        std::string line;
        if (lpos == std::string::npos) {
            // Empty line -> treat as ENTER key
            inputBuffer.clear();
            push(KeyCode::ENTER, "ENTER (PC-Enter)");
            return;
        } else {
            line = inputBuffer.substr(lpos, rpos - lpos + 1);
        }

        if (_lineHandler &&
            _lineHandler(line.c_str(), _lineHandlerContext)) {
            inputBuffer.clear();
            return;
        }

        // If line starts with ':' → Giac command
        if (!line.empty() && line[0] == ':') {
            String in(line.c_str());
            if (in.startsWith(":")) {
                in = in.substring(1);
            }
            in.trim();
            String out = solveWithGiac(in);
            Serial.print(": => ");
            Serial.println(out);
            inputBuffer.clear();
            return;
        }

        // Case-insensitive keyword checks. Named keys keep the PC bridge usable
        // for keys that have no unambiguous single-character representation.
        std::string upper = line;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c){ return std::toupper(c); });

        // "RELEASE ALL" — drop every key the bridge still holds down.
        if (upper == "RELEASE ALL" || upper == "RELALL") {
            releaseAllHeld();
            inputBuffer.clear();
            return;
        }

        // Hold / release forms: "+KEY" presses and holds, "-KEY" releases.
        // Needed for held input (the Game Boy core) because a plain press is
        // auto-released after kPulseMs.
        if (line.size() >= 2 && (line[0] == '+' || line[0] == '-')) {
            const std::string name = upper.substr(1);
            KeyCode code = KeyCode::NONE;
            bool known = name.size() == 1 && lookupArrowKey(name[0], code);
            if (!known) known = lookupNamedKey(name, code);
            if (!known) {
                Serial.printf("[SB] Unknown hold/release key: %s\n",
                              name.c_str());
                inputBuffer.clear();
                return;
            }
            if (line[0] == '+') {
                push(code, name.c_str(), KeyAction::PRESS,
                     /*autoRelease=*/false);
            } else {
                releaseCode(code, name.c_str());
            }
            inputBuffer.clear();
            return;
        }

        KeyCode named = KeyCode::NONE;
        if (lookupNamedKey(upper, named)) {
            push(named, upper.c_str());
            inputBuffer.clear();
            return;
        }

        // Single character line: map to key codes (preserve previous mappings)
        if (line.size() == 1) {
            char c = line[0];
            switch (c) {
                case 'w': case 'W': push(KeyCode::UP,    "UP");    break;
                case 'a':           push(KeyCode::LEFT,  "LEFT");  break;
                case 'd': case 'D': push(KeyCode::RIGHT, "RIGHT"); break;
                case 's':           push(KeyCode::DOWN,  "DOWN");  break;

                case 'S':  push(KeyCode::SHIFT, "SHIFT"); break;
                case 'A':  push(KeyCode::ALPHA, "ALPHA"); break;

                case 8: case 127: push(KeyCode::DEL, "DEL"); break;
                case 0x1B:      push(KeyCode::AC,    "AC");        break;
                case 'c':       push(KeyCode::AC,    "AC");        break;
                case 'h': case 'H': push(KeyCode::MODE,  "MODE/HOME"); break;
                case 'b': case 'B': push(KeyCode::AC,    "BACK (AC)"); break;

                case '0': push(KeyCode::NUM_0, "0"); break;
                case '1': push(KeyCode::NUM_1, "1"); break;
                case '2': push(KeyCode::NUM_2, "2"); break;
                case '3': push(KeyCode::NUM_3, "3"); break;
                case '4': push(KeyCode::NUM_4, "4"); break;
                case '5': push(KeyCode::NUM_5, "5"); break;
                case '6': push(KeyCode::NUM_6, "6"); break;
                case '7': push(KeyCode::NUM_7, "7"); break;
                case '8': push(KeyCode::NUM_8, "8"); break;
                case '9': push(KeyCode::NUM_9, "9"); break;

                case '+': push(KeyCode::ADD, "+"); break;
                case '-': push(KeyCode::SUB, "-"); break;
                case '*': push(KeyCode::MUL, "*"); break;
                case '/': push(KeyCode::DIV, "/"); break;
                case '.': push(KeyCode::DOT, "."); break;
                case '(' : push(KeyCode::LPAREN, "("); break;
                case ')' : push(KeyCode::RPAREN, ")"); break;

                case 'p': case '^': push(KeyCode::POW, "POW"); break;
                case '=': push(KeyCode::FREE_EQ, "="); break;
                case 'f': push(KeyCode::DIV, "DIV (FRAC)"); break;
                case 'x': push(KeyCode::VAR_X, "VAR_X"); break;
                case 'y': push(KeyCode::VAR_Y, "VAR_Y"); break;
                case 'g': push(KeyCode::GRAPH, "GRAPH"); break;
                case 't': push(KeyCode::SIN,   "SIN");   break;
                case 'r': push(KeyCode::SQRT,  "SQRT");  break;
                case 'R':  push(KeyCode::SHIFT, "SHIFT"); push(KeyCode::SQRT, "SQRT (=nthROOT)"); break;
                case 'n':  push(KeyCode::SHOW_STEPS, "STEPS"); break;
                case 'u':  push(KeyCode::FREE_EQ, "= (FREE_EQ)"); break;
                case 'C':  push(KeyCode::AC, "AC"); break;
                case '<':  push(KeyCode::EXE, "EXE"); break;
                case 'F':  push(KeyCode::F1, "F1"); break;
                case 'G':  push(KeyCode::F2, "F2"); break;
                default:
                    // Unrecognized single char: echo it
                    Serial.print("[SB] Unmapped char: ");
                    Serial.println(line.c_str());
                    break;
            }
            inputBuffer.clear();
            return;
        }

        // Unrecognized multi-char line: echo back and clear
        Serial.print("[SB] Unrecognized line: ");
        Serial.println(line.c_str());
        inputBuffer.clear();
        return;
    }
}
