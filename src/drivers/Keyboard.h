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
    TCA9555 _tca{0x20}; // Default I2C address
    
    // Matrix dimensions for 6x9 configuration
    static constexpr int ROWS = 9;
    static constexpr int COLS = 6;
    static constexpr int CONNECTED_COLS = 6; // All 6 columns connected
    
    // TCA9555 pin mappings (matching assignment specifications)
    const uint8_t _colPins[COLS] = {TCA_P00, TCA_P01, TCA_P02, TCA_P03, TCA_P04, TCA_P05}; // C1-C6
    const uint8_t _rowPins[ROWS] = {TCA_P06, TCA_P10, TCA_P11, TCA_P12, TCA_P13, TCA_P14, TCA_P15, TCA_P16, TCA_P17}; // R1-R9
    
    // Timing constants (preserve existing values)
    static constexpr uint16_t SCAN_INTERVAL_MS = 5;
    static constexpr uint16_t DEBOUNCE_MS = 20;
    static constexpr uint16_t AUTOREPEAT_DELAY_MS = 500;
    static constexpr uint16_t AUTOREPEAT_RATE_MS = 80;
    static const KeyCode _map[ROWS][COLS];
    
    // State tracking arrays
    bool _rawState[ROWS][COLS]{};
    bool _debState[ROWS][COLS]{};
    uint32_t _debTimer[ROWS][COLS]{};
    uint32_t _arTimer[ROWS][COLS]{};
    uint32_t _lastScanMs = 0;
    
    // Event queue
    static constexpr int QUEUE_SIZE = 16;
    KeyEvent _queue[QUEUE_SIZE]{};
    int _qHead = 0;
    int _qTail = 0;
    
    void doScan();
    void pushEvent(const KeyEvent& ev);
#endif
};
