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
 * src/drivers/Keyboard.cpp
 * ──────────────────────────────────────────────────────────────────────────────
 * Implementación del driver de matriz 5×10 para ESP32-S3.
 *
 * CÓMO EXTENDER EL KEYMAP:
 *   1. Aumenta CONNECTED_COLS en Keyboard.h (máx. 10).
 *   2. Rellena las posiciones NONE del array _map con los KeyCodes correctos.
 *   3. Reconecta los nuevos pines en el PCB.  No hay que tocar nada más.
 *
 * ──────────────────────────────────────────────────────────────────────────────
 */

#include "Keyboard.h"

#if NUMOS_BOARD_PROD_WROOM1U_N16R8

#include "../hardware/BoardProfile.h"
#include "../input/KeySemanticResolver.h"

#if !defined(NUMOS_PRODUCTION_KEYPAD_MAPPING_READY) || \
    !NUMOS_PRODUCTION_KEYPAD_MAPPING_READY
#error "Production keyboard requires the generated and validated mapping"
#endif

namespace {

bool elapsedAtLeast(const uint32_t now,
                    const uint32_t then,
                    const uint32_t duration) {
    return static_cast<uint32_t>(now - then) >= duration;
}

bool deadlineReached(const uint32_t now, const uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

int levelFor(const numos::hardware::ActiveLevel level) {
    return level == numos::hardware::ActiveLevel::Low ? LOW : HIGH;
}

} // namespace

void Keyboard::driveAllRowsInactive() {
    const auto& matrix =
        numos::hardware::kProductionBoard.electricalMatrix;
    const int inactive = levelFor(matrix.inactiveRowLevel);
    for (const int gpio : matrix.rowOutputs) {
        digitalWrite(gpio, inactive);
    }
}

void Keyboard::begin() {
    const auto& matrix =
        numos::hardware::kProductionBoard.electricalMatrix;
    const int inactive = levelFor(matrix.inactiveRowLevel);

    // WHY: preload every output latch before output enable so no row can emit
    // an active-low glitch while GPIO ownership transfers to the scanner.
    for (const int gpio : matrix.rowOutputs) {
        digitalWrite(gpio, inactive);
    }
    for (const int gpio : matrix.rowOutputs) {
        pinMode(gpio, OUTPUT);
        digitalWrite(gpio, inactive);
    }
    for (const int gpio : matrix.columnInputs) {
        pinMode(gpio, INPUT_PULLUP);
    }

    _productionScanner.reset();
    _currentRow = 0;
    _scanPhase = ScanPhase::WaitingToSelect;
    _nextSelectUs = micros();
    _scanStartedUs = _nextSelectUs;
    _phaseStartedUs = _nextSelectUs;
    _initialized = true;
    _enabled = true;
}

void Keyboard::selectCurrentRow() {
    const auto& matrix =
        numos::hardware::kProductionBoard.electricalMatrix;
    driveAllRowsInactive();
    const uint8_t pinIndex = matrix.rowOrder[_currentRow];
    digitalWrite(matrix.rowOutputs[pinIndex],
                 levelFor(matrix.selectedRowLevel));
}

uint16_t Keyboard::sampleColumns() const {
    const auto& matrix =
        numos::hardware::kProductionBoard.electricalMatrix;
    const int pressed = levelFor(matrix.pressedColumnLevel);
    uint16_t mask = 0;
    for (uint8_t logicalColumn = 0; logicalColumn < COLS; ++logicalColumn) {
        const uint8_t pinIndex = matrix.columnOrder[logicalColumn];
        if (digitalRead(matrix.columnInputs[pinIndex]) == pressed) {
            mask |= static_cast<uint16_t>(1U << logicalColumn);
        }
    }
    return mask;
}

void Keyboard::update() {
    if (!_initialized || !_enabled) return;
    const auto& matrix =
        numos::hardware::kProductionBoard.electricalMatrix;
    const uint32_t nowUs = micros();

    if (_scanPhase == ScanPhase::WaitingToSelect) {
        if (!deadlineReached(nowUs, _nextSelectUs)) return;
        if (_currentRow == 0) _scanStartedUs = nowUs;
        selectCurrentRow();
        _phaseStartedUs = nowUs;
        _scanPhase = ScanPhase::Settling;
        return;
    }

    if (!elapsedAtLeast(nowUs, _phaseStartedUs,
                        matrix.settlingDurationUs)) {
        return;
    }

    const uint16_t pressedColumns = sampleColumns();
    driveAllRowsInactive();
    _productionScanner.ingestRow(
        _currentRow, pressedColumns, millis());

    ++_currentRow;
    if (_currentRow == ROWS) {
        _currentRow = 0;
        _nextSelectUs = _scanStartedUs + matrix.fullScanIntervalUs;
        if (deadlineReached(nowUs, _nextSelectUs)) {
            _nextSelectUs = nowUs;
        }
    } else {
        _nextSelectUs = nowUs;
    }
    _scanPhase = ScanPhase::WaitingToSelect;
}

bool Keyboard::pollEvent(KeyEvent& event) {
    return _productionScanner.pollEvent(event);
}

void Keyboard::setEnabled(const bool enabled) {
    if (!_initialized || _enabled == enabled) return;
    if (!enabled) {
        driveAllRowsInactive();
        _productionScanner.forceReleaseAll(millis());
        numos::input::KeySemanticResolver::reset();
        _enabled = false;
        return;
    }
    driveAllRowsInactive();
    _currentRow = 0;
    _scanPhase = ScanPhase::WaitingToSelect;
    _nextSelectUs = micros();
    _enabled = true;
}

void Keyboard::forceReleaseAll() {
    if (!_initialized) return;
    driveAllRowsInactive();
    _productionScanner.forceReleaseAll(millis());
    numos::input::KeySemanticResolver::reset();
}

bool Keyboard::initialized() const { return _initialized; }
bool Keyboard::enabled() const { return _enabled; }
bool Keyboard::rowSelected() const {
    return _initialized && _enabled && _scanPhase == ScanPhase::Settling;
}
uint32_t Keyboard::overflowCount() const {
    return _productionScanner.overflowCount();
}

const numos::input::ProductionKeyState& Keyboard::diagnosticState(
    const uint8_t row,
    const uint8_t column) const {
    return _productionScanner.state(row, column);
}

uint16_t Keyboard::diagnosticActiveColumns(const uint8_t row) const {
    return _productionScanner.activeColumns(row);
}

#else

#include "../lib/TCA9555/TCA9555.h"

// ── Keymap 5×10 ───────────────────────────────────────────────────────────────
//
// Diseño visual de las 15 teclas ACTUALMENTE CABLEADAS (cols 0-2):
// (C0/C1 reasignados de GPIO 4/5 → GPIO 6/7 para evitar conflicto con TFT_DC/RST)
//
//  Col →   C0 (GPIO 6)  C1 (GPIO 7)  C2 (GPIO 8)  C3…C9 (no cableadas)
//  R0 (GPIO  1)   7           8           9
//  R1 (GPIO  2)   4           5           6
//  R2 (GPIO 41)   1           2           3
//  R3 (GPIO 42)   0          AC         ENTER
//  R4 (GPIO 40)   +           -           ×
//
// Full 5×10 planned layout (C3-C9):
//   R0 top row:  SHIFT ALPHA MODE SETUP F1 F2 F3 F4 F5 EXE
//   Top row function keys F1-F5 mapped to C4-C8.
//   Physical '<' key → EXE (Execute/Solve) at C9.
//   Physical Enter → ENTER (Place/Select) remains at R3C2.
//
// Placeholder key mapping for 6x9 matrix - TO BE UPDATED BY HUMAN LATER
// Current mapping uses placeholder values since the original layout doesn't match the 6x9 configuration
const KeyCode Keyboard::_map[Keyboard::ROWS][Keyboard::COLS] = {
    // C0               C1                |C2               |C3              |C4                |C5
    { KeyCode::SHIFT,   KeyCode::ALPHA,   KeyCode::MODE,    KeyCode::SETUP,   KeyCode::F1,      KeyCode::F2    },  // Row 0
    { KeyCode::F3,      KeyCode::F4,      KeyCode::F5,      KeyCode::EXE,     KeyCode::ON,      KeyCode::AC    },  // Row 1
    { KeyCode::DEL,     KeyCode::FREE_EQ, KeyCode::LEFT,    KeyCode::UP,      KeyCode::DOWN,    KeyCode::RIGHT },  // Row 2
    { KeyCode::VAR_X,   KeyCode::VAR_Y,   KeyCode::TABLE,   KeyCode::GRAPH,   KeyCode::ZOOM,    KeyCode::TRACE },  // Row 3
    { KeyCode::SHOW_STEPS, KeyCode::SOLVE, KeyCode::NUM_7,  KeyCode::NUM_8,   KeyCode::NUM_9,   KeyCode::LPAREN},  // Row 4
    { KeyCode::RPAREN,  KeyCode::DIV,     KeyCode::POW,     KeyCode::SQRT,    KeyCode::NUM_4,   KeyCode::NUM_5 },  // Row 5
    { KeyCode::NUM_6,   KeyCode::MUL,     KeyCode::SUB,     KeyCode::SIN,     KeyCode::COS,     KeyCode::TAN   },  // Row 6
    { KeyCode::NUM_1,   KeyCode::NUM_2,   KeyCode::NUM_3,   KeyCode::ADD,     KeyCode::NEG,     KeyCode::NUM_0 },  // Row 7
    { KeyCode::DOT,     KeyCode::ENTER,   KeyCode::NONE,    KeyCode::NONE,    KeyCode::NONE,    KeyCode::NONE  },  // Row 8
};

// ── begin() ──────────────────────────────────────────────────────────────────

void Keyboard::begin() {
    // Initialize I2C bus with specified pins from Config.h
    Wire.begin(KBD_I2C_SDA_PIN, KBD_I2C_SCL_PIN);
    
    // Initialize TCA9555
    if (!_tca.begin()) {
        // Handle initialization failure - set enabled to false
        return;
    }
    
    // Configure column pins as OUTPUTS (active low scanning)
    for (int c = 0; c < COLS; c++) {
        _tca.pinMode1(_colPins[c], OUTPUT);
        _tca.write1(_colPins[c], HIGH); // Start inactive (HIGH)
    }
    
    // Configure row pins as INPUTS with internal pull-ups
    for (int r = 0; r < ROWS; r++) {
        _tca.pinMode1(_rowPins[r], INPUT);
    }
    
    // Configure unused pin as INPUT to prevent floating
    _tca.pinMode1(TCA_P07, INPUT);
    
    // Initialize state arrays
    memset(_rawState, 0, sizeof(_rawState));
    memset(_debState, 0, sizeof(_debState));
    memset(_debTimer, 0, sizeof(_debTimer));
    memset(_arTimer, 0, sizeof(_arTimer));
    
    _lastScanMs = millis();
}

// ── update() ─────────────────────────────────────────────────────────────────

void Keyboard::update() {
    uint32_t now = millis();
    
    // For interrupt-driven mode, we would check a volatile flag here
    // But for compatibility, maintain polling with proper timing
    if ((now - _lastScanMs) >= SCAN_INTERVAL_MS) {
        doScan();
        _lastScanMs = now;
    }
}

// ── pollEvent() ──────────────────────────────────────────────────────────────

bool Keyboard::pollEvent(KeyEvent& outEvent) {
    if (_qHead == _qTail) return false;   // Cola vacía
    outEvent = _queue[_qHead];
    _qHead = (_qHead + 1) & (QUEUE_SIZE - 1);
    return true;
}

// ── doScan() — núcleo del driver ─────────────────────────────────────────────

void Keyboard::doScan() {
    uint32_t now = millis();
    
    // Scan each column (OUTPUT)
    for (int c = 0; c < COLS; ++c) {
        // Activate column by setting it LOW (active)
        _tca.write1(_colPins[c], LOW);
        
        // Small delay for signal settling (microseconds sufficient for ESP32-S3)
        delayMicroseconds(10);
        
        // Read all row inputs (INPUT)
        for (int r = 0; r < ROWS; ++r) {
            bool rawNow = (_tca.read1(_rowPins[r]) == LOW); // LOW = pressed (due to pull-up)
            
            // Debounce state machine per key
            if (rawNow != _rawState[r][c]) {
                // Raw state changed - reset timer
                _rawState[r][c] = rawNow;
                _debTimer[r][c] = now;
            } else if ((now - _debTimer[r][c]) >= DEBOUNCE_MS) {
                // Stable state confirmed
                if (rawNow != _debState[r][c]) {
                    _debState[r][c] = rawNow;
                    
                    KeyCode kc = _map[r][c];
                    if (kc != KeyCode::NONE) {
                        KeyAction action = rawNow ? KeyAction::PRESS : KeyAction::RELEASE;
                        pushEvent({ kc, action, r, c });
                        
                        if (rawNow) {
                            // Start autorepeat timer
                            _arTimer[r][c] = now;
                        }
                    }
                }
            }
            
            // Autorepeat logic (only if key is still pressed)
            if (_debState[r][c] && _rawState[r][c]) {
                uint32_t elapsed = now - _arTimer[r][c];
                uint32_t threshold = (_arTimer[r][c] == _debTimer[r][c]) 
                                   ? AUTOREPEAT_DELAY_MS 
                                   : AUTOREPEAT_RATE_MS;
                
                if (elapsed >= threshold) {
                    KeyCode kc = _map[r][c];
                    if (kc != KeyCode::NONE) {
                        pushEvent({ kc, KeyAction::REPEAT, r, c });
                    }
                    _arTimer[r][c] = now;
                }
            }
        }
        
        // Deactivate column by setting it HIGH
        _tca.write1(_colPins[c], HIGH);
    }
}

// ── pushEvent() ──────────────────────────────────────────────────────────────

void Keyboard::pushEvent(const KeyEvent& ev) {
    int nextTail = (_qTail + 1) & (QUEUE_SIZE - 1);
    if (nextTail == _qHead) return;   // Cola llena: descarta el evento silenciosamente.
    _queue[_qTail] = ev;
    _qTail = nextTail;
}

void Keyboard::setEnabled(bool) {}
void Keyboard::forceReleaseAll() {}
bool Keyboard::initialized() const { return true; }
bool Keyboard::enabled() const { return CONNECTED_COLS > 0; }
bool Keyboard::rowSelected() const { return false; }
uint32_t Keyboard::overflowCount() const { return 0; }

#endif
