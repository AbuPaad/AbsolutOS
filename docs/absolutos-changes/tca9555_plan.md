# TCA9555 I2C Keypad Matrix Implementation Plan

## 1. Executive Summary

This document provides a complete, line-by-line implementation specification to replace the existing GPIO-based keypad driver with a TCA9555 I2C GPIO expander driver for a 6×9 matrix configuration (6 columns as OUTPUTS, 9 rows as INPUTS). The implementation maintains full backward compatibility with the existing codebase while providing interrupt-driven efficiency improvements.

## 2. Input Stack Architecture Analysis

Based on `bigass-inputthing.xml`, the input flow is:

1. **Physical Matrix** → **Keyboard/KeyMatrix driver** (generates raw KeyEvent structs)
2. **KeyEvent** → **KeyboardManager** (handles SHIFT/ALPHA/STORE modifier state)
3. **KeyEvent + modifiers** → **KeySemanticResolver** (maps to semantic actions)
4. **Resolved events** → **LvglKeypad** (LVGL input device integration)
5. **LVGL** → **SystemApp** (application logic)

For production hardware, **ProductionKeypadScanner** replaces basic debouncing with advanced 2D debounce, hold detection, and repeat logic while maintaining the same KeyEvent interface.

## 3. Corrected TCA9555 Pin Assignments

### I2C Bus & Interrupt
- **SDA**: IO47 (ESP32-S3 pin for I2C data)
- **SCL**: IO6 (ESP32-S3 pin for I2C clock)
- **INT**: IO14 (ESP32-S3 interrupt input pin)

### TCA9555 Port Mapping (CORRECTED: Rows=INPUT, Columns=OUTPUT)
- **Port 0 - Mixed Configuration**:
  - P00-P05 = C1-C6 (**Columns = OUTPUTS**, active low)
  - P06 = R1 (**Row = INPUT** with internal pull-up)
  - P07 = NC (Not Connected, configure as INPUT)
- **Port 1 - Rows (INPUTS)**:
  - P10-P17 = R2-R9 (**Rows = INPUTS** with internal pull-up)

**Note**: Total matrix is 6 columns (OUTPUTS) × 9 rows (INPUTS) using 15 of 16 available TCA9555 pins.

## 4. Detailed Implementation Directives

### 4.1 Files to Modify
- **Primary**: `src/drivers/Keyboard.h` and `src/drivers/Keyboard.cpp`
- **Conditional**: Only modify the non-production path (`#else` section after `#if NUMOS_BOARD_PROD_WROOM1U_N16R8`)

### 4.2 Keyboard.h Modifications

**Replace lines 59-67** (current GPIO pin arrays and constants) with:

```cpp
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
```

### 4.3 Keyboard.cpp Modifications

#### 4.3.1 Add Required Includes
**After line 29** (`#include "Keyboard.h"`), add:
```cpp
#include "../lib/TCA9555/TCA9555.h"
```

#### 4.3.2 begin() Method Implementation
**Replace the entire non-production begin() method** with:

```cpp
void Keyboard::begin() {
    // Initialize I2C bus with specified pins
    Wire.begin(47, 6); // SDA=47, SCL=6
    
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
```

#### 4.3.3 doScan() Method Implementation
**Replace the entire doScan() method** with interrupt-compatible scanning:

```cpp
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
```

#### 4.3.4 update() Method for Interrupt Handling
**Modify the update() method** to support interrupt-driven operation:

```cpp
void Keyboard::update() {
    uint32_t now = millis();
    
    // For interrupt-driven mode, we would check a volatile flag here
    // But for compatibility, maintain polling with proper timing
    if ((now - _lastScanMs) >= SCAN_INTERVAL_MS) {
        doScan();
        _lastScanMs = now;
    }
}
```

**Note**: Full interrupt implementation requires ESP32-S3 GPIO interrupt setup which should be added in a separate phase. This maintains compatibility while preparing for future interrupt optimization.

#### 4.3.5 Error Handling Integration
Add error checking after every TCA9555 operation:

```cpp
// Example pattern to apply throughout:
_tca.write1(_colPins[c], LOW);
if (_tca.lastError() != TCA9555_OK) {
    // Log error but continue operation
    // Consider reinitializing TCA9555 on persistent errors
}
```

### 4.4 Key Mapping Array
The `_map[ROWS][COLS]` array must be updated to reflect the 6×9 physical layout. Each entry should map to the appropriate `KeyCode` enum value from `KeyCodes.h`.

## 5. Integration Requirements

### 5.1 API Compatibility Guarantees
- **No changes** to public methods: `begin()`, `update()`, `pollEvent()`, `setEnabled()`, etc.
- **Identical constants**: `ROWS=9`, `COLS=6`, `CONNECTED_COLS=6`
- **Same KeyEvent structure**: Row/column indices match physical positions
- **Preserve all timing**: Debounce, autorepeat, and scan intervals unchanged

### 5.2 Production vs Non-Production Code Paths
- **Only modify the `#else` section** in Keyboard.h/cpp (non-production path)
- **Leave `#if NUMOS_BOARD_PROD_WROOM1U_N16R8` section unchanged**
- Production hardware continues using `ProductionKeypadScanner` with GPIO

### 5.3 TCA9555 Library Usage Rules
- **Always check `_tca.lastError()`** after every library call
- **Use only documented API functions**: `pinMode1()`, `write1()`, `read1()`
- **Never perform I2C operations in ISR context**
- **Implement graceful degradation** on communication failures

## 6. Critical Implementation Notes

### 6.1 Matrix Scanning Logic
- **Columns are OUTPUTS**: Set LOW to activate, HIGH to deactivate
- **Rows are INPUTS**: Read LOW when key is pressed (pull-up resistor)
- **Scanning order**: Iterate through columns, read all rows for each column
- **Timing**: 10μs delay sufficient for ESP32-S3 signal settling

### 6.2 Memory Layout Preservation
- **State arrays**: Maintain identical `_rawState`, `_debState`, `_debTimer`, `_arTimer` structures
- **Event queue**: Preserve `QUEUE_SIZE=16` and ring buffer logic
- **Key mapping**: Update `_map` array dimensions to `[9][6]` but keep same access patterns

### 6.3 Error Recovery Strategy
- **I2C failures**: Continue operation with last known state
- **Device disconnect**: Return false from `enabled()` method
- **Bus recovery**: Reinitialize TCA9555 on persistent communication errors
- **Never crash**: All error conditions must be handled gracefully

## 7. Testing Validation Points

### 7.1 Functional Testing
- Verify all 54 keys (6×9) generate correct KeyEvent structures
- Confirm row/column indices match physical key positions
- Test modifier keys (SHIFT, ALPHA) function correctly
- Validate autorepeat and debouncing behavior

### 7.2 Integration Testing
- Ensure KeyboardManager receives identical KeyEvent structures
- Verify KeySemanticResolver produces correct semantic mappings
- Confirm LvglKeypad integration works without modification
- Test SystemApp compatibility with existing applications

### 7.3 Robustness Testing
- Simulate I2C communication failures and verify graceful degradation
- Test rapid key sequences and verify no missed events
- Validate memory usage remains within existing constraints
- Ensure no timing regressions in main application loop

## 8. Success Criteria

1. **Complete API Compatibility**: Zero breaking changes to any downstream components
2. **Correct Matrix Functionality**: All 54 keys work with proper row/column mapping
3. **Preserved Behavior**: Identical debouncing, autorepeat, and timing characteristics
4. **Robust Error Handling**: Graceful operation during I2C communication issues
5. **Code Quality**: Clean implementation following existing codebase patterns
6. **Future-Ready**: Architecture supports interrupt-driven optimization in subsequent phases
