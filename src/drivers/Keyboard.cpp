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

    // The production matrix was wiped when the display moved onto the bench
    // pins (see BoardProfile.h).  No scanner GPIO is assigned, so do not arm
    // the scanner: an invalid GPIO would read LOW and look like a key hold.
    if (!matrix.logicalMappingReady) {
        _initialized = false;
        _enabled = false;
        return;
    }

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
// Key mapping for the 9×6 matrix, transcribed verbatim from docs/input/mappings
// (9 sensed rows × 6 driven columns, indexed [row][col]).
//
// TODO(human): the source table has gaps that look like typos — [6][2] and
// [6][3] are both NUM_6, and NUM_3 / NUM_4 are not assigned anywhere. Left
// exactly as supplied; fix the source table and re-transcribe if unintended.
const KeyCode Keyboard::_map[Keyboard::ROWS][Keyboard::COLS] = {
    // C0               C1                |C2               |C3              |C4                |C5
    { KeyCode::SHIFT,   KeyCode::ALPHA,   KeyCode::UP,      KeyCode::RIGHT,   KeyCode::MODE,    KeyCode::ON    },  // Row 0
    { KeyCode::NONE,    KeyCode::POW,     KeyCode::LEFT,    KeyCode::DOWN,    KeyCode::NONE,    KeyCode::NONE  },  // Row 1
    { KeyCode::DIV,     KeyCode::NONE,    KeyCode::NONE,    KeyCode::NONE,    KeyCode::NONE,    KeyCode::NONE  },  // Row 2
    { KeyCode::NONE,    KeyCode::NONE,    KeyCode::NONE,    KeyCode::SIN,     KeyCode::COS,     KeyCode::TAN   },  // Row 3
    { KeyCode::NONE,    KeyCode::NONE,    KeyCode::LPAREN,  KeyCode::RPAREN,  KeyCode::NONE,    KeyCode::NONE  },  // Row 4
    { KeyCode::NUM_7,   KeyCode::NUM_8,   KeyCode::NUM_9,   KeyCode::DEL,     KeyCode::AC,      KeyCode::NONE  },  // Row 5
    { KeyCode::NONE,    KeyCode::NUM_5,   KeyCode::NUM_6,   KeyCode::NUM_6,   KeyCode::DIV,     KeyCode::NONE  },  // Row 6
    { KeyCode::NUM_1,   KeyCode::NUM_2,   KeyCode::NONE,    KeyCode::ADD,     KeyCode::SUB,     KeyCode::NONE  },  // Row 7
    { KeyCode::NUM_0,   KeyCode::DOT,     KeyCode::NONE,    KeyCode::EXE,     KeyCode::ENTER,   KeyCode::NONE  },  // Row 8
};

namespace {

bool elapsedAtLeast(const uint32_t now, const uint32_t then,
                    const uint32_t duration) {
    return static_cast<uint32_t>(now - then) >= duration;
}

} // namespace

// ── begin() ──────────────────────────────────────────────────────────────────

void Keyboard::begin() {
    _initialized = false;
    _enabled = false;

    // Initialize the I2C bus with the bench-tested pins from Config.h.
    Wire.begin(KBD_I2C_SDA_PIN, KBD_I2C_SCL_PIN);
    // A short timeout: a wedged bus must not stall the whole main loop.
    Wire.setTimeOut(50);

    // Probe the TCA9555 and remember whether it answered.
    if (!_tca.begin()) {
        return;  // initialized() stays false.
    }

    // Direction registers, configured ONCE (never touched in the scan path):
    //   port 0 = 0xC0 : bits 0..5 = column OUTPUTs, bit 6 = row 0 INPUT,
    //                   bit 7 = unused INPUT (cannot drive anything).
    //   port 1 = 0xFF : all INPUTs (rows 1..8).
    _tca.pinMode16(0xFFC0);

    // /INT on GPIO14: pull-up input, FALLING edge. The ISR only sets a flag.
    pinMode(KBD_I2C_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(KBD_I2C_INT_PIN), kbdIsr, FALLING);

    memset(_rawState, 0, sizeof(_rawState));
    memset(_debState, 0, sizeof(_debState));
    memset(_repeatStarted, 0, sizeof(_repeatStarted));
    memset(_debTimer, 0, sizeof(_debTimer));
    memset(_arTimer, 0, sizeof(_arTimer));

    _initialized = true;
    _enabled = true;

    setIdleState();  // Columns LOW, clear the interrupt latch, enter IDLE.
}

// ── Interrupt ISR ────────────────────────────────────────────────────────────

void IRAM_ATTR Keyboard::kbdIsr() {
    s_intTriggered = true;
}

volatile bool Keyboard::s_intTriggered = false;

// ── setIdleState() ───────────────────────────────────────────────────────────

void Keyboard::setIdleState() {
    // Drive all six columns LOW so pressing any key ties a LOW column to a row
    // INPUT and asserts /INT. Reading both ports clears the TCA9555 interrupt
    // latch; then drop the software flag and go IDLE (zero I2C until /INT).
    _tca.write8(0, 0x00);
    _tca.read16();
    s_intTriggered = false;
    _scanMode = ScanMode::IDLE;
    _sweepDue = false;
}

// ── hasActiveKeys() ──────────────────────────────────────────────────────────

bool Keyboard::hasActiveKeys() const {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (_debState[r][c] || (_rawState[r][c] != _debState[r][c])) {
                return true;
            }
        }
    }
    return false;
}

// ── update() ─────────────────────────────────────────────────────────────────

void Keyboard::update() {
    if (!_initialized || !_enabled) return;

    const uint32_t now = millis();

    // IDLE: wait for /INT. Zero I2C transactions while no interrupt has fired.
    if (_scanMode == ScanMode::IDLE) {
        if (!s_intTriggered) return;
        s_intTriggered = false;
        _scanMode = ScanMode::SCANNING;
        _sweepDue = true;  // Explicit immediate sweep (never "0 == now").
    }

    // SCANNING: sweep on the shared scan interval.
    if (_scanMode == ScanMode::SCANNING) {
        const bool due = _sweepDue ||
            elapsedAtLeast(now, _lastScanMs, KEY_SCAN_INTERVAL_MS);
        if (!due) return;
        _sweepDue = false;
        _lastScanMs = now;
        doScan();

        // Nothing left held or bouncing -> park back in IDLE.
        if (!hasActiveKeys()) {
            setIdleState();
        }
    }
}

// ── pollEvent() ──────────────────────────────────────────────────────────────

bool Keyboard::pollEvent(KeyEvent& outEvent) {
    if (_qHead == _qTail) return false;   // Cola vacía
    outEvent = _queue[_qHead];
    _qHead = (_qHead + 1) & (QUEUE_SIZE - 1);
    return true;
}

// ── doScan() — núcleo del driver (port-level I/O) ───────────────────────────

void Keyboard::doScan() {
    const uint32_t now = millis();

    for (int c = 0; c < COLS; ++c) {
        // One port-level write: active column LOW, the other five columns HIGH.
        const uint8_t mask = static_cast<uint8_t>(0x3F & ~(1u << c));
        if (!_tca.write8(0, mask)) {
            onBusFailure();
            return;
        }

        // Let the driven column settle before sampling.
        delayMicroseconds(10);

        // One port-level read: both input ports, split below.
        const uint16_t raw16 = _tca.read16();
        if (_tca.lastError() != TCA9555_OK) {
            onBusFailure();
            return;
        }
        _consecutiveFailures = 0;  // Any successful transaction resets the count.

        const uint8_t port0 = static_cast<uint8_t>(raw16 & 0xFF);
        const uint8_t port1 = static_cast<uint8_t>((raw16 >> 8) & 0xFF);

        for (int r = 0; r < ROWS; ++r) {
            // LOW level == pressed (a LOW column tied to a pulled-up row input).
            const bool rawNow = (r == 0)
                ? ((port0 & (1u << 6)) == 0)          // row 0 = TCA_P06
                : ((port1 & (1u << (r - 1))) == 0);   // rows 1..8 = TCA_P10..P17

            // Debounce state machine per key.
            if (rawNow != _rawState[r][c]) {
                _rawState[r][c] = rawNow;
                _debTimer[r][c] = now;
            } else if (elapsedAtLeast(now, _debTimer[r][c], KEY_DEBOUNCE_MS)) {
                if (rawNow != _debState[r][c]) {
                    _debState[r][c] = rawNow;

                    const KeyCode kc = _map[r][c];
                    if (kc != KeyCode::NONE) {
                        // Deviation from the rig: the rig emits events even for
                        // KeyCode::NONE cells; this driver keeps NONE silent.
                        pushEvent({kc, rawNow ? KeyAction::PRESS
                                              : KeyAction::RELEASE, r, c});
                        if (rawNow) {
                            _arTimer[r][c] = now;
                            _repeatStarted[r][c] = false;
                        }
                    }
                }
            }

            // Autorepeat (only while the key is still debounced-held).
            if (_debState[r][c] && _rawState[r][c]) {
                const KeyCode kc = _map[r][c];
                if (kc != KeyCode::NONE) {
                    const uint32_t threshold = _repeatStarted[r][c]
                        ? KEY_AUTOREPEAT_RATE_MS
                        : KEY_AUTOREPEAT_DELAY_MS;
                    if (elapsedAtLeast(now, _arTimer[r][c], threshold)) {
                        pushEvent({kc, KeyAction::REPEAT, r, c});
                        _arTimer[r][c] = now;
                        _repeatStarted[r][c] = true;
                    }
                }
            }
        }
    }
}

// ── pushEvent() ──────────────────────────────────────────────────────────────

void Keyboard::pushEvent(const KeyEvent& ev) {
    const int nextTail = (_qTail + 1) & (QUEUE_SIZE - 1);
    if (nextTail == _qHead) {
        ++_overflowCount;  // A dropped event is now accounted for.
        return;
    }
    _queue[_qTail] = ev;
    _qTail = nextTail;
}

// ── setEnabled() ─────────────────────────────────────────────────────────────

void Keyboard::setEnabled(const bool enabled) {
    if (!_initialized || _enabled == enabled) return;

    if (!enabled) {
        // Release any held keys and reset per-key state, then drive all six
        // columns HIGH: no key can conduct, /INT stays quiet, zero traffic.
        forceReleaseAll();
        _tca.write8(0, 0x3F);
        _enabled = false;
        return;
    }

    _enabled = true;
    setIdleState();  // Back to columns-LOW idle and a cleared interrupt latch.
}

// ── forceReleaseAll() ────────────────────────────────────────────────────────

void Keyboard::forceReleaseAll() {
    // Discard any queued PRESS/REPEAT the application has not yet consumed,
    // then emit RELEASE only for keys that are still debounced-held. No
    // phantom RELEASE is synthesised for keys that were never down.
    _qHead = 0;
    _qTail = 0;

    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            if (_debState[r][c]) {
                const KeyCode kc = _map[r][c];
                if (kc != KeyCode::NONE) {
                    pushEvent({kc, KeyAction::RELEASE, r, c});
                }
            }
            _rawState[r][c] = false;
            _debState[r][c] = false;
            _repeatStarted[r][c] = false;
            _debTimer[r][c] = 0;
            _arTimer[r][c] = 0;
        }
    }

    setIdleState();
}

// ── onBusFailure() ────────────────────────────────────────────────────────────

void Keyboard::onBusFailure() {
    ++_consecutiveFailures;
    if (_consecutiveFailures >= 3) {
        // Three consecutive failed transactions -> treat the bus as wedged.
        // Drop held state so a dead bus cannot leave phantom held keys.
        _consecutiveFailures = 0;
        forceReleaseAll();
    }
}

// ── Getters ──────────────────────────────────────────────────────────────────

bool Keyboard::initialized() const { return _initialized; }
bool Keyboard::enabled() const { return _enabled; }
bool Keyboard::rowSelected() const {
    // This driver drives columns, not rows, so there is no row-select phase.
    // main.cpp only consults rowSelected() in the production build.
    return false;
}
uint32_t Keyboard::overflowCount() const { return _overflowCount; }

#endif
