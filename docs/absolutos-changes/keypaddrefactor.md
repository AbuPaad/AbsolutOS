# NumOS Keypad Driver Refactoring Plan (TCA9555 I2C Expander)

## 1. Executive Summary & Hardware Context

### Target Hardware
- **MCU**: ESP32-S3 (ESP32-S3-N16R8 / ESP32-S3-N16R8-CAM / ESP32-S3-N16R9)
- **I/O Expander**: TCA9555 16-bit I2C I/O Expander
- **Pin Definitions**:
  - **INT**: GPIO `22` (Active LOW interrupt pin from TCA9555)
  - **SDA**: GPIO `24`
  - **SCL**: GPIO `6`
  - **I2C Address**: Default `0x20` (or `0x21`–`0x27` configurable)
  - **I2C Frequency**: 400 kHz (Fast Mode)
- **External Library**: [`RobTillaart/TCA9555`](https://github.com/RobTillaart/TCA9555)

### Objectives
1. Implement a clean, high-performance, robust keypad driver utilizing the TCA9555 I2C expander library.
2. **Preserve exact function signatures and class interfaces** (`Keyboard` / `KeyMatrix` / `ProductionKeypadScanner` APIs) across the codebase to ensure 100% backward compatibility with `SystemApp`, `main.cpp`, `HardwareTest.cpp`, `LvglKeypad`, etc.
3. Support interrupt-driven (`INT: 22`) or non-blocking polling scan cycles with minimal I2C bus overhead.
4. Integrate debounce, auto-repeat, and multi-plane modifier resolution (`SHIFT`, `ALPHA`, `SHIFT+ALPHA`) seamlessly.

---

## 2. Analysis of Existing Input Architecture

Based on `@keypad.xml` and existing source files in `src/input/` and `src/drivers/`:

1. **`Keyboard` (`src/drivers/Keyboard.h`, `src/drivers/Keyboard.cpp`)**:
   - Primary driver instance (`g_keypad` in `main.cpp`, consumed by `SystemApp g_app(g_display, g_keypad)`).
   - Key Methods:
     - `void begin();`
     - `void update();`
     - `bool pollEvent(KeyEvent& outEvent);`
     - `void setEnabled(bool enabled);`
     - `void forceReleaseAll();`
     - `bool initialized() const;`
     - `bool enabled() const;`
     - `bool rowSelected() const;`
     - `uint32_t overflowCount() const;`
2. **`KeyMatrix` (`src/input/KeyMatrix.h`, `src/input/KeyMatrix.cpp`)**:
   - Matrix interface maintained for test utilities and backward compatibility.
   - Key Methods:
     - `KeyMatrix();`
     - `void begin();`
     - `void update();`
     - `bool pollEvent(KeyEvent& outEvent);`
     - `void pushEvent(KeyEvent ev);`
3. **`ProductionKeypadScanner` (`src/input/ProductionKeypadScanner.h`, `src/input/ProductionKeypadScanner.cpp`)**:
   - Integrator-based debouncer and state machine (handles Press, Release, Auto-repeat, Queue buffering of `KeyEvent`).
   - Core ingest method: `ingestRow(uint8_t row, uint16_t pressedColumns, uint32_t nowMs)`.
4. **`KeyCodes.h` & `KeyEvent`**:
   - `struct KeyEvent { KeyCode code; KeyAction action; int row; int col; ... };`
   - `enum class KeyAction { NONE, PRESS, RELEASE, REPEAT };`

---

## 3. Hardware Interfacing with TCA9555

### TCA9555 Pin Allocation (16 GPIOs: Port 0 [P00-P07] & Port 1 [P10-P17])
The TCA9555 provides 16 I/O pins configured as:
- **Rows (Outputs / Active LOW)**: 5 to 6 rows (e.g., P00–P04 or P00–P05). Driven LOW one row at a time while scanning (or all LOW when idling to wait for INT pin trigger).
- **Columns (Inputs with Pull-ups)**: 8 to 10 columns (e.g., P05–P07 and P10–P16 / P17). Read to detect which key switch is closed.
- **Hardware Interrupt (`INT` on GPIO 22)**:
  - When all output rows are held LOW in idle mode, pressing any key pulls a column line LOW, triggering the TCA9555 open-drain `INT` pin.
  - ESP32-S3 detects FALLING edge on GPIO 22 to wake/schedule scanning, eliminating unnecessary I2C polling traffic when idle.

### RobTillaart/TCA9555 Library API Integration
- `TCA9555 TCA(address, &Wire);`
- `Wire.begin(24, 6, 400000);` (SDA: 24, SCL: 6, 400kHz Fast Mode I2C)
- `TCA.begin();`
- `TCA.pinMode16(mask);` / `TCA.pinMode(pin, mode);`
- `TCA.write16(mask);` / `TCA.write(pin, value);`
- `uint16_t input = TCA.read16();` / `TCA.read(pin);`

---

## 4. Implementation Steps & Architectural Design

### Step 1: Add Dependency to `platformio.ini`
Add `robtillaart/TCA9555` to the `lib_deps` section of `platformio.ini` for the ESP32 environments:
```ini
lib_deps =
    bodmer/TFT_eSPI
    lvgl/lvgl@^9.2.0
    robtillaart/TCA9555@^0.4.0
    file://lib/giac
    file://lib/libtommath
```

### Step 2: Configuration & Pin Definitions (`src/Config.h`)
Define the I2C and Interrupt pins in `src/Config.h`:
```cpp
// ── TCA9555 I2C Keypad Expander Pinout ──
#define TCA9555_I2C_SDA_PIN    24
#define TCA9555_I2C_SCL_PIN     6
#define TCA9555_INT_PIN        22
#define TCA9555_I2C_ADDR     0x20
#define TCA9555_I2C_FREQ   400000U // 400 kHz Fast-Mode
```

### Step 3: Implement `TCA9555KeypadDriver` / Refactor `Keyboard` & `KeyMatrix`
Maintain identical public methods to preserve system-wide compatibility.

#### `Keyboard` class implementation details:
- **Public API (Strictly Unchanged)**:
  ```cpp
  class Keyboard {
  public:
      Keyboard();
      void begin();
      void update();
      bool pollEvent(KeyEvent& outEvent);
      void setEnabled(bool enabled);
      void forceReleaseAll();
      bool initialized() const;
      bool enabled() const;
      bool rowSelected() const;
      uint32_t overflowCount() const;
      ...
  ```
- **Internal Mechanisms**:
  1. `begin()`:
     - Initialize `Wire.begin(TCA9555_I2C_SDA_PIN, TCA9555_I2C_SCL_PIN, TCA9555_I2C_FREQ)`.
     - Initialize `TCA.begin()`.
     - Configure row pins as OUTPUT (`HIGH` inactive), column pins as `INPUT` (with external or internal pull-ups).
     - Configure GPIO 22 (`INT`) as `pinMode(TCA9555_INT_PIN, INPUT_PULLUP)`. Attach ISR or flag for interrupt change notification.
  2. `update()`:
     - Check if INT is asserted (`digitalRead(TCA9555_INT_PIN) == LOW`) or if active keys are currently in debounce/repeat state.
     - Scan active rows: Drive row $r$ LOW, read columns via `TCA.read16()` or port read, restore row $r$ HIGH.
     - Feed sample to debouncer / `ProductionKeypadScanner::ingestRow(row, pressedColumns, millis())`.
     - Handle key repeat timing (`KEY_AUTOREPEAT_DELAY_MS = 500`, `KEY_AUTOREPEAT_RATE_MS = 80`).
  3. `pollEvent(KeyEvent& outEvent)`:
     - Pop next debounced `KeyEvent` from the internal event buffer / `ProductionKeypadScanner`.

### Step 4: Native Simulator Compatibility (`src/hal/NativeHal.cpp` & `env:emulator_pc`)
- Provide conditional `#ifdef ARDUINO` compilation or mock I2C layer for `NATIVE_SIM` so PC builds compile without physical I2C dependencies.

---

## 5. Verification & Testing Strategy

1. **Compilation Validation**:
   - Run `pio run -e esp32s3_n16r8` / `pio run -e numos-esp32-s3-wroom-1u-n16r8` to ensure clean compilation.
   - Run `pio run -e emulator_pc` to ensure PC build remains unbroken.
2. **I2C Bus & Pin Verification**:
   - Verify I2C communication with TCA9555 at address `0x20` on SDA `24` / SCL `6`.
   - Test interrupt assertion on GPIO `22` on button down/up.
3. **Keymap & Debounce Verification**:
   - Validate matrix coordinates `[row][col]` to `KeyCode` mapping against `kProductionKeypadMap` / `_map`.
   - Verify proper event generation: `PRESS`, `RELEASE`, `REPEAT`.
   - Verify modifier chords (`SHIFT`, `ALPHA`, `SHIFT+ALPHA`) and numeric entry in `CalculationApp` and `MainMenu`.
