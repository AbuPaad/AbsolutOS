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
 * KeyMatrix.cpp
 */

#include "KeyMatrix.h"

KeyMatrix::KeyMatrix() {
    // Reset state
    for(int r=0; r<9; r++) {
        for(int c=0; c<6; c++) {
            _keyState[r][c] = 0;
            _lastDebounceTime[r][c] = 0;
            _map[r][c] = KeyCode::NONE;
        }
    }

    // --- Define Layout Map ---
    // 9 sensed rows x 6 driven columns. Kept in sync with the production
    // TCA9555 layout in drivers/Keyboard.cpp (Keyboard::_map) so both drivers
    // agree on which physical key sits at each (row, col).
    _map[0][0] = KeyCode::SHIFT;      _map[0][1] = KeyCode::ALPHA;      _map[0][2] = KeyCode::MODE;   _map[0][3] = KeyCode::SETUP;  _map[0][4] = KeyCode::F1;    _map[0][5] = KeyCode::F2;
    _map[1][0] = KeyCode::F3;         _map[1][1] = KeyCode::F4;         _map[1][2] = KeyCode::F5;     _map[1][3] = KeyCode::EXE;    _map[1][4] = KeyCode::ON;    _map[1][5] = KeyCode::AC;
    _map[2][0] = KeyCode::DEL;        _map[2][1] = KeyCode::FREE_EQ;    _map[2][2] = KeyCode::LEFT;   _map[2][3] = KeyCode::UP;     _map[2][4] = KeyCode::DOWN;  _map[2][5] = KeyCode::RIGHT;
    _map[3][0] = KeyCode::VAR_X;      _map[3][1] = KeyCode::VAR_Y;      _map[3][2] = KeyCode::TABLE;  _map[3][3] = KeyCode::GRAPH;  _map[3][4] = KeyCode::ZOOM;  _map[3][5] = KeyCode::TRACE;
    _map[4][0] = KeyCode::SHOW_STEPS; _map[4][1] = KeyCode::SOLVE;      _map[4][2] = KeyCode::NUM_7;  _map[4][3] = KeyCode::NUM_8;  _map[4][4] = KeyCode::NUM_9; _map[4][5] = KeyCode::LPAREN;
    _map[5][0] = KeyCode::RPAREN;     _map[5][1] = KeyCode::DIV;        _map[5][2] = KeyCode::POW;    _map[5][3] = KeyCode::SQRT;   _map[5][4] = KeyCode::NUM_4; _map[5][5] = KeyCode::NUM_5;
    _map[6][0] = KeyCode::NUM_6;      _map[6][1] = KeyCode::MUL;        _map[6][2] = KeyCode::SUB;    _map[6][3] = KeyCode::SIN;    _map[6][4] = KeyCode::COS;   _map[6][5] = KeyCode::TAN;
    _map[7][0] = KeyCode::NUM_1;      _map[7][1] = KeyCode::NUM_2;      _map[7][2] = KeyCode::NUM_3;  _map[7][3] = KeyCode::ADD;    _map[7][4] = KeyCode::NEG;   _map[7][5] = KeyCode::NUM_0;
    // Row 8: only two keys populated; the rest stay KeyCode::NONE from the reset above.
    _map[8][0] = KeyCode::DOT;        _map[8][1] = KeyCode::ENTER;
}

void KeyMatrix::begin() {
    // Rows as Inputs with Pullup
    for (int i = 0; i < 9; i++) {
        pinMode(_rowPins[i], INPUT_PULLUP);
    }
    // Cols as Outputs, default HIGH (inactive)
    for (int i = 0; i < 6; i++) {
        pinMode(_colPins[i], OUTPUT);
        digitalWrite(_colPins[i], HIGH);
    }
}

void KeyMatrix::update() {
    uint32_t now = millis();

    // Iterate Columns (6 driven columns)
    for (int c = 0; c < 6; c++) {
        // Activate Column (LOW)
        digitalWrite(_colPins[c], LOW);
        // Short delay for signal settling? usually not needed on ESP32 ~microsecs
        // delayMicroseconds(2); 

        // Read Rows (9 sensed rows)
        for (int r = 0; r < 9; r++) {
            // LOW means pressed because of Pull-Up
            bool pressed = (digitalRead(_rowPins[r]) == LOW);
            
            // Debounce
            if (pressed != (_keyState[r][c] == 1)) {
                if (now - _lastDebounceTime[r][c] > KEY_DEBOUNCE_MS) {
                    _lastDebounceTime[r][c] = now;
                    _keyState[r][c] = pressed ? 1 : 0;
                    
                    KeyEvent ev;
                    ev.code = _map[r][c];
                    ev.row = r;
                    ev.col = c;
                    ev.action = pressed ? KeyAction::PRESS : KeyAction::RELEASE;
                    
                    if (ev.code != KeyCode::NONE) {
                        pushEvent(ev);
                    }
                }
            }
        }
        
        // Deactivate Column (HIGH)
        digitalWrite(_colPins[c], HIGH);
    }
}

bool KeyMatrix::pollEvent(KeyEvent &outEvent) {
    if (_head == _tail) return false; // Empty
    
    outEvent = _eventBuf[_tail];
    _tail = (_tail + 1) % EVENT_BUF_SIZE;
    return true;
}

void KeyMatrix::pushEvent(KeyEvent ev) {
    int next = (_head + 1) % EVENT_BUF_SIZE;
    if (next != _tail) { // Not full
        _eventBuf[_head] = ev;
        _head = next;
    }
}
