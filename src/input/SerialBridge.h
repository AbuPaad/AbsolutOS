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
 * SerialBridge.h
 * PC → NumOS keyboard bridge via Serial Monitor.
 *
 * Reads characters from Serial.read() (non-blocking) and
 * translates them into KeyEvent structs that SystemApp understands.
 *
 * Every key is a LINE: type the key, terminate with Enter. A bare Enter line is
 * the ENTER key. Named words exist for codes with no unambiguous single char.
 *
 * Momentary keys emit a PRESS *and* a RELEASE: a lone PRESS latches inside every
 * consumer that reads edges — the Game Boy core holds the button down until it
 * sees KeyAction::RELEASE.
 *   w/s/a/d          → UP / DOWN / LEFT / RIGHT
 *   Enter / z        → ENTER (OK/EXE)
 *   Backspace / x    → DEL
 *   Escape / h       → MODE (HOME)
 *   c                → AC   (Clear All)
 *   0–9              → NUM_0..NUM_9
 *   + - * /          → ADD SUB MUL DIV
 *   .                → DOT
 *   ^                → POW
 *   (                → LPAREN
 *   )                → RPAREN
 *   f                → SHIFT+DIV (Fraction)
 *   S                → SHIFT
 *   g                → GRAPH
 *   t                → SIN  (trig shortcut)
 *
 * Hold / release forms (for held game-style input):
 *   +KEY             → PRESS and stay down (e.g. "+D", "+ENTER")
 *   -KEY             → RELEASE that key  (e.g. "-D")
 *   RELEASE ALL      → release everything the bridge still holds
 */

#pragma once

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include "hal/ArduinoCompat.h"
#endif
#include "input/KeyMatrix.h"   // For KeyEvent, KeyAction, KeyCode

class SerialBridge {
public:
    using LineHandler = bool (*)(const char* line, void* context);

    /// How long a pulsed momentary key stays down before its automatic RELEASE
    /// is queued (~7 frames at 60 fps): long enough to register as a tap, short
    /// enough not to feel stuck. Use "+KEY"/"-KEY" when you need a real hold.
    static constexpr uint32_t kPulseMs = 120;

    SerialBridge();

    /// Call once in setup() after Serial.begin()
    void begin();

    /// Non-blocking: reads available serial chars, pushes events.
    /// Returns true if at least one event was generated.
    bool pollEvent(KeyEvent &outEvent);
    void setLineHandler(LineHandler handler, void* context);

private:
    // Small circular buffer for generated events
    static const int BUF_SIZE = 32;
    KeyEvent _buf[BUF_SIZE];
    int _head;
    int _tail;
    LineHandler _lineHandler = nullptr;
    void* _lineHandlerContext = nullptr;

    // Keys the bridge has pressed and still owes a RELEASE for.
    // dueMs == 0 → held until an explicit "-KEY" or "RELEASE ALL".
    struct HeldKey {
        KeyCode code;
        uint32_t dueMs;
    };
    static const int MAX_HELD = 8;
    HeldKey _held[MAX_HELD] = {};
    int _heldCount = 0;

    void push(KeyCode code, const char* label,
              KeyAction action = KeyAction::PRESS, bool autoRelease = true);
    bool pop(KeyEvent &out);
    void processChar(int ch);

    void trackHeld(KeyCode code, uint32_t dueMs);
    void releaseCode(KeyCode code, const char* label);
    void releaseAllHeld();
    /// Queue RELEASE for every pulsed key whose hold time has elapsed.
    void servicePendingReleases();
};
