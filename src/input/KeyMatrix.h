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
 * KeyMatrix.h
 * Driver for 9x6 Keypad Matrix (6 columns driven, 9 rows sensed).
 * Rows are Inputs with Pull-Up. Columns are Outputs (Active Low scan).
 */

#pragma once

#include <Arduino.h>
#include "../Config.h"
#include "KeyCodes.h"   // KeyCode, KeyAction, KeyEvent

class KeyMatrix {
public:
    KeyMatrix();

    void begin();
    void update(); // Call this frequently (e.g. in loop)
    
    // Returns true if there was an event
    bool pollEvent(KeyEvent &outEvent);

private:
    // 9 Rows (Inputs)
    const int _rowPins[9] = { PIN_KEY_R0, PIN_KEY_R1, PIN_KEY_R2, PIN_KEY_R3, PIN_KEY_R4, PIN_KEY_R5, PIN_KEY_R6, PIN_KEY_R7, PIN_KEY_R8 };
    // 6 Cols (Outputs, active LOW)
    const int _colPins[6] = { PIN_KEY_C0, PIN_KEY_C1, PIN_KEY_C2, PIN_KEY_C3, PIN_KEY_C4, PIN_KEY_C5 };

    // State
    uint8_t _keyState[9][6];
    uint32_t _lastDebounceTime[9][6];
    
    // Mapping table
    KeyCode _map[9][6];

    // Event Buffer
    static const int EVENT_BUF_SIZE = 16;
    KeyEvent _eventBuf[EVENT_BUF_SIZE];
    int _head = 0;
    int _tail = 0;

    void pushEvent(KeyEvent ev);
};
