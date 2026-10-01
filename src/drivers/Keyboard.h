#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../hal/ArduinoCompat.h"
#endif

#include "../Config.h"
#include "../input/KeyCodes.h"

#if NUMOS_BOARD_PROD_WROOM1U_N16R8
#include "../input/ProductionKeypadScanner.h"
#else
// Include TCA9555 definitions for non-production build
#include "../lib/TCA9555/TCA9555.h"
// ("../Config.h" above already carries KBD_I2C_SDA_PIN / KBD_I2C_SCL_PIN; the
//  old `#include "./config.h"` here was a typo and never resolved.)
#endif

class Keyboard {
public:
    Keyboard() = default;

    void begin();
    void update();
    bool pollEvent(KeyEvent& outEvent);

    void setEnabled(bool enabled);
    void forceReleaseAll();
    bool initialized() const;
    bool enabled() const;
    bool rowSelected() const;
    uint32_t overflowCount() const;

#if NUMOS_BOARD_PROD_WROOM1U_N16R8
    static constexpr int ROWS = 5;
    static constexpr int COLS = 10;
    static constexpr int CONNECTED_COLS = 0; // Legacy CAM hardware remains off.
    
    const numos::input::ProductionKeyState& diagnosticState(
        uint8_t row, uint8_t column) const;
    uint16_t diagnosticActiveColumns(uint8_t row) const;

private:
    enum class ScanPhase : uint8_t {
        WaitingToSelect,
        Settling,
    };

    void driveAllRowsInactive();
    void selectCurrentRow();
    uint16_t sampleColumns() const;

    numos::input::ProductionKeypadScanner _productionScanner;
    ScanPhase _scanPhase = ScanPhase::WaitingToSelect;
    uint8_t _currentRow = 0;
    uint32_t _scanStartedUs = 0;
    uint32_t _phaseStartedUs = 0;
    uint32_t _nextSelectUs = 0;
    bool _initialized = false;
    bool _enabled = false;
#else
private:
    // TCA9555 instance and pin mapping
    TCA9555 _tca{0x20}; // I2C address 0x20

    // Matrix dimensions for the TCA9555 6x9 expander (6 driven columns x 9 sensed rows)
    static constexpr int ROWS = 9;
    static constexpr int COLS = 6;
    static constexpr int CONNECTED_COLS = 6; // All 6 columns connected

    // TCA9555 pin mappings (matching assignment specifications)
    //   Columns  = TCA port 0 bits 0..5 (driven outputs, active LOW).
    //   Row 0    = TCA_P06 (port 0 bit 6); rows 1..8 = TCA_P10..P17 (port 1 bits 0..7).
    //   TCA_P07  = unused, left INPUT so it cannot drive anything.
    const uint8_t _colPins[COLS] = {TCA_P00, TCA_P01, TCA_P02, TCA_P03, TCA_P04, TCA_P05};
    const uint8_t _rowPins[ROWS] = {TCA_P06, TCA_P10, TCA_P11, TCA_P12, TCA_P13, TCA_P14, TCA_P15, TCA_P16, TCA_P17};

    static const KeyCode _map[ROWS][COLS];

    // State tracking arrays
    bool _rawState[ROWS][COLS]{};
    bool _debState[ROWS][COLS]{};
    bool _repeatStarted[ROWS][COLS]{};
    uint32_t _debTimer[ROWS][COLS]{};
    uint32_t _arTimer[ROWS][COLS]{};

    // INT-gated scan state machine
    enum class ScanMode : uint8_t { IDLE, SCANNING };
    ScanMode _scanMode = ScanMode::IDLE;
    static volatile bool s_intTriggered;
    bool _sweepDue = false;
    uint32_t _lastScanMs = 0;

    // Health / enable / overflow accounting
    bool _initialized = false;
    bool _enabled = false;
    uint32_t _overflowCount = 0;
    uint8_t _consecutiveFailures = 0;

    // Event queue
    static constexpr int QUEUE_SIZE = 16;
    KeyEvent _queue[QUEUE_SIZE]{};
    int _qHead = 0;
    int _qTail = 0;

    static void IRAM_ATTR kbdIsr();

    void doScan();
    void pushEvent(const KeyEvent& ev);
    void setIdleState();
    bool hasActiveKeys() const;
    void onBusFailure();
#endif
};
