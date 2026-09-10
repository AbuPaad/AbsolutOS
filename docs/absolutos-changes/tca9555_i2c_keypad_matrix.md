# High-Level Technical Overview: NumOS Input Architecture & TCA9555 Keypad Migration

## 1. Executive Summary

This document provides a broad high-level technical overview of the input subsystem in NumOS based on `codemix/bigass-inputthing.xml`. It analyzes how namespaces are structured, documents public vs. private functions across all input components, details how to migrate from GPIO matrix polling to a TCA9555 I2C interrupt-driven keypad matrix, and explains how to modify physical keypad mappings (such as converting to a 6x9 key matrix).

---

## 2. Namespace & Architectural Hierarchy

The input subsystem is partitioned into distinct logical layers and namespaces:

```
+-----------------------------------------------------------------------------------+
|                                 Applications / UI                                 |
+------------------------------------------+----------------------------------------+
                                           |
                                           v
+-----------------------------------------------------------------------------------+
|                              numos::input Namespace                              |
|  - KeySemanticResolver : Maps KeyCode + Modifiers + Context -> ResolvedKey        |
|  - ProductionKeypadScanner : 2D Debounce state machine, event generator          |
+------------------------------------------+----------------------------------------+
                                           |
                                           v
+------------------------------------------+----------------------------------------+
|                                vpam Namespace                                     |
|  - KeyboardManager : Singleton tracking SHIFT, ALPHA, STORE state machines        |
+------------------------------------------+----------------------------------------+
                                           |
                                           v
+-----------------------------------------------------------------------------------+
|                                Global Namespace                                   |
|  - KeyCodes.h       : KeyCode enum, KeyAction enum, KeyEvent struct               |
|  - KeyMatrix        : Direct hardware matrix scanner & GPIO driver                |
|  - LvglKeypad       : Keypad input driver integration for LVGL 9.x              |
|  - SerialBridge     : PC Serial Monitor keyboard bridge                           |
+-----------------------------------------------------------------------------------+
```

### Namespace Breakdown:

1. **Global Namespace**:
   - `KeyCode` / `KeyAction` / `KeyEvent` ([KeyCodes.h](file:///home/fih/musings/AbsolutOS/src/input/KeyCodes.h)): Core primitive types representing physical and logical keys, key states (Press, Release, Hold), and packed event payloads.
   - `KeyMatrix` ([KeyMatrix.h](file:///home/fih/musings/AbsolutOS/src/input/KeyMatrix.h)): Hardware driver responsible for low-level matrix pin control, column-by-column scanning, and event queuing.
   - `LvglKeypad` ([LvglKeypad.h](file:///home/fih/musings/AbsolutOS/src/input/LvglKeypad.h)): Enqueues keypad events into an LVGL input device (`lv_indev_t`) and translates `KeyCode`s into `LV_KEY_*` values for menu navigation.
   - `SerialBridge` ([SerialBridge.h](file:///home/fih/musings/AbsolutOS/src/input/SerialBridge.h)): Translates ASCII characters from PC serial monitor inputs into `KeyEvent` structures.

2. **`vpam` Namespace**:
   - `vpam::KeyboardManager` ([KeyboardManager.h](file:///home/fih/musings/AbsolutOS/src/input/KeyboardManager.h)): Singleton managing 3-state modifier state machines (`Off`, `OneShot`, `Locked`) for `SHIFT`, `ALPHA`, and `STORE`. Handles plane consumption logic.

3. **`numos::input` Namespace**:
   - `numos::input::ProductionKeypadScanner` ([ProductionKeypadScanner.h](file:///home/fih/musings/AbsolutOS/src/input/ProductionKeypadScanner.h)): Advanced debouncing matrix engine operating on row bitmasks. Supports holding, repeating, queue overflow protection, and transition state boundary resets.
   - `numos::input::KeySemanticResolver` ([KeySemanticResolver.h](file:///home/fih/musings/AbsolutOS/src/input/KeySemanticResolver.h)): High-level context-aware translation engine. Resolves physical `KeyCode` + `vpam::KeyboardManager` state + `InputContext` into a `ResolvedKey` containing semantic IDs (e.g. `SIN`, `OFF`, `CONST_PI`).

---

## 3. Public vs. Private API Specification

Below is the complete breakdown of functions in each class, indicating which functions are intended for external application use and which are private implementation details.

### 3.1 `KeyMatrix` ([KeyMatrix.h](file:///home/fih/musings/AbsolutOS/src/input/KeyMatrix.h))
- **Public API (External Use)**:
  - `void begin()`: Initializes GPIO modes (Rows as `INPUT_PULLUP`, Columns as `OUTPUT`).
  - `void update()`: Scans matrix columns and debounces raw key states. Called continuously in the main loop.
  - `bool pollEvent(KeyEvent &outEvent)`: Pops the oldest pending `KeyEvent` from the queue. Returns `true` if an event was popped.
- **Private Functions / Internal Helpers**:
  - `void pushEvent(KeyEvent ev)`: Pushes a newly generated event into internal ring buffer `_queue[16]`.

### 3.2 `numos::input::ProductionKeypadScanner` ([ProductionKeypadScanner.h](file:///home/fih/musings/AbsolutOS/src/input/ProductionKeypadScanner.h))
- **Public API (External Use)**:
  - `explicit ProductionKeypadScanner(...)`: Configures scanner parameters (debounce ms, hold delay, repeat rate).
  - `void ingestRow(uint8_t row, uint16_t pressedColumns, uint32_t nowMs)`: Ingests 10-sample bitmask for a row.
  - `bool pollEvent(KeyEvent& event)`: Dequeues next processed key event.
  - `void forceReleaseAll(uint32_t nowMs)`: Immediately releases all held keys (used during application/context transitions).
  - `void reset()`: Flushes queues and resets state.
  - State queries: `state(row, col)`, `activeColumns(row)`, `overflowCount()`, `queuedEventCount()`, `config()`.
- **Private Functions / Internal Helpers**:
  - `constexpr std::size_t index(row, col)`: Calculates 1D index from 2D coordinates.
  - `void transition(keyIndex, pressed, nowMs)`: Updates state machine for individual key switch.
  - `bool pushEvent(keyIndex, action)`: Pushes press/release/repeat event into buffer.
  - Queue management helpers: `eraseQueueIndex()`, `removeQueuedPress()`, `removeOldestRepeat()`, `removeUndispatchedPress()`.

### 3.3 `vpam::KeyboardManager` ([KeyboardManager.h](file:///home/fih/musings/AbsolutOS/src/input/KeyboardManager.h))
- **Public API (External Use)**:
  - `static KeyboardManager& instance()`: Retrieves global singleton instance.
  - Modifier triggers: `pressShift()`, `longPressShift()`, `pressAlpha()`, `longPressAlpha()`, `pressStore()`.
  - Modifier consumption: `consumeModifier()`, `consumeForPlane(usesShift, usesAlpha)`.
  - Status & queries: `state()`, `shiftPhase()`, `alphaPhase()`, `isShift()`, `isAlpha()`, `isLocked()`, `isStore()`, `indicatorText()`.
  - `void reset()`: Resets all modifiers to `Off`.
- **Private Implementation**:
  - Helpers in anonymous namespace ([KeyboardManager.cpp](file:///home/fih/musings/AbsolutOS/src/input/KeyboardManager.cpp)): `nextShortPress()`, `nextLongPress()`.

### 3.4 `numos::input::KeySemanticResolver` ([KeySemanticResolver.h](file:///home/fih/musings/AbsolutOS/src/input/KeySemanticResolver.h))
- **Public API (External Use)**:
  - `static ResolvedKey resolve(KeyCode physicalCode, InputContext context, const vpam::KeyboardManager& modifiers)`: Translates physical key code into semantic key operation according to active plane.
  - `static void reset()`: Resets resolver context.
- **Private Functions / Internal Helpers**:
  - `static std::size_t mappingIndex(KeyCode physicalCode)`: Maps `KeyCode` to definition array index.
  - `static const KeyPlaneDefinition& definition(...)`: Retrieves plane mapping table.
  - Internal helpers in anonymous namespace ([KeySemanticResolver.cpp](file:///home/fih/musings/AbsolutOS/src/input/KeySemanticResolver.cpp)): `activePlane()`, `clearsModifiers()`.

### 3.5 `LvglKeypad` ([LvglKeypad.h](file:///home/fih/musings/AbsolutOS/src/input/LvglKeypad.h))
- **Public API (External Use)**:
  - `static void init()`: Registers `lv_indev_t` input device with LVGL 9.x.
  - `static void pushKey(KeyCode code, bool pressed)`: Enqueues key event for LVGL consumption.
  - `static void forceReleaseAll()`: Flushes queue and pushes release state.
  - `static lv_indev_t* indev()`: Returns pointer to LVGL input device handle for group binding.
- **Private Functions / Internal Helpers**:
  - `static void readCb(lv_indev_t* indev, lv_indev_data_t* data)`: LVGL timer callback invoked every tick to dequeue keys.
  - `static uint32_t toLvKey(KeyCode code)`: Maps physical `KeyCode` to `LV_KEY_*` navigation codes.

### 3.6 `SerialBridge` ([SerialBridge.h](file:///home/fih/musings/AbsolutOS/src/input/SerialBridge.h))
- **Public API (External Use)**:
  - `void begin()`: Initializes serial communication bridge.
  - `bool pollEvent(KeyEvent &outEvent)`: Dequeues key events generated from Serial input.
  - `void setLineHandler(LineHandler handler, void* context)`: Custom handler for GIAC commands (`:` prefix).
- **Private Functions / Internal Helpers**:
  - `void push(KeyCode code, const char* label)` / `bool pop(KeyEvent &out)`: Ring buffer management.
  - `void processChar(int ch)`: Character/string accumulator and parsing logic.

---

## 4. Adapting Architecture for TCA9555 I2C Keypad Matrix (Interrupt-Driven)

### 4.1 Overview of TCA9555
The **TCA9555** (or PCA9555) is a 16-bit I2C GPIO expander with two 8-bit ports (Port 0: `P00–P07`, Port 1: `P10–P17`). Key characteristics:
- Features an active-LOW open-drain **Interrupt Output Pin (`INT`)**.
- `INT` fires automatically whenever any pin configured as an input changes state relative to its input port register.
- Reading Port 0 or Port 1 registers over I2C clears the interrupt output.

As demonstrated in [RobTillaart's TCA9555 interrupt example](https://github.com/RobTillaart/TCA9555/blob/master/examples/TCA9555_interrupt/TCA9555_interrupt.ino), using `INT` avoids continuous I2C polling over bus operations when no key state changes occur.

### 4.2 Hardware Pin Assignment Strategy (e.g. 6 Rows x 9 Columns = 15 Pins)
With 16 total GPIO pins available on TCA9555:
- **Rows (6 Pins)**: `P00` to `P05` configured as **Inputs with internal pull-up resistors**.
- **Columns (9 Pins)**: `P06`, `P07` (Port 0) and `P10` to `P16` (Port 1) configured as **Outputs**.
- **ESP32 Connection**:
  - `SDA` -> ESP32 SDA (e.g., GPIO 21)
   - `SCL` -> ESP32 SCL (e.g., GPIO 22)
   - `INT` -> Dedicated ESP32 GPIO (e.g., GPIO 4) configured with `INPUT_PULLUP` and a falling-edge ISR.

### 4.3 ESP-IDF Driver Implementation Guidelines (`esp-idf-lib / tca95x5`)

According to the official [esp-idf-lib `tca95x5` driver documentation](https://esp-idf-lib.readthedocs.io/en/latest/groups/tca95x5.html), Espressif/ESP-IDF implementations manage the TCA9555 via thread-safe 16-bit port-wide transactions using the `i2cdev` master layer:

1. **Device Descriptor Initialization**:
   - Initialize device handle `i2c_dev_t dev` using `tca95x5_init_desc(&dev, addr, port, sda_gpio, scl_gpio)` (base address `TCA95X5_I2C_ADDR_BASE` = `0x20`). Default I2C clock frequency is 400kHz.
2. **16-Bit Pin Mode Configuration (`tca95x5_port_set_mode`)**:
   - Mode is a 16-bit bitmask (`0` = output, `1` = input).
   - For a 6x9 matrix (6 rows = `P0.0–P0.5` inputs, 9 columns = `P0.6–P0.7` and `P1.0–P1.6` outputs):
     - Rows bitmask: `0x003F` (bits 0–5 = `1`).
     - Columns bitmask: `0xFFC0` (bits 6–15 = `0`).
     - Execution: `tca95x5_port_set_mode(&dev, 0x003F);` sets mode for all 16 pins in a single I2C transaction.
3. **Port-Wide Read / Write (`tca95x5_port_read` / `tca95x5_port_write`)**:
   - `tca95x5_port_read(&dev, &val)` reads all 16 pins simultaneously into a `uint16_t` buffer. This is significantly faster and more efficient than individual pin reads (`tca95x5_get_level`).
   - `tca95x5_port_write(&dev, val)` updates all column output states in one 2-byte transaction.
4. **ESP-IDF / FreeRTOS ISR & Task Synchronization**:
   - In ESP-IDF, I2C transactions (`i2cdev`) take mutexes and must NOT be executed inside a raw GPIO hardware ISR.
   - The GPIO ISR on the `INT` pin notifies a background FreeRTOS keypad task using `vTaskNotifyGiveFromISR()`:
     ```c
     static TaskHandle_t s_keypadTaskHandle = NULL;

     static void IRAM_ATTR gpio_isr_handler(void* arg) {
         BaseType_t xHigherPriorityTaskWoken = pdFALSE;
         vTaskNotifyGiveFromISR(s_keypadTaskHandle, &xHigherPriorityTaskWoken);
         portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
     }
     ```

---

### 4.4 Software Implementation Blueprint for `KeyMatrix`

Instead of polling GPIO pins directly in `KeyMatrix::update()`, update the driver flow as follows:

1. **ISR & Volatile Flag**:
   ```cpp
   static volatile bool g_keypadIntTriggered = false;

   void IRAM_ATTR keypadIsr() {
       g_keypadIntTriggered = true;
   }
   ```

2. **`KeyMatrix::begin()` Setup**:
   - Initialize I2C bus (`Wire.begin()` or `tca95x5_init_desc(&dev, ...)`).
   - Set pins `P00–P05` as inputs with pull-up enable (`tca95x5_port_set_mode(&dev, 0x003F)`).
   - Set pins `P06–P07`, `P10–P16` as outputs, driving all column pins **LOW** in the idle state (`tca95x5_port_write(&dev, 0x0000)`).
   - Attach interrupt to ESP32 pin:
     ```cpp
     pinMode(TCA_INT_PIN, INPUT_PULLUP);
     attachInterrupt(digitalPinToInterrupt(TCA_INT_PIN), keypadIsr, FALLING);
     ```
   - *Why drive all columns LOW in idle state?* When any switch is pressed, it connects a row input (`P00-P05`) to a LOW column pin, pulling that row LOW and immediately triggering `INT`!

3. **`KeyMatrix::update()` Execution Flow**:
   ```cpp
   void KeyMatrix::update() {
       // Only run I2C transactions if INT triggered or active debouncing/holding is underway
       if (!g_keypadIntTriggered && !hasActiveState()) {
           return;
       }
       g_keypadIntTriggered = false;

       // 1. Scan across 9 columns (P06-P07, P10-P16)
       for (uint8_t col = 0; col < 9; ++col) {
           // Drive target column LOW, drive all other columns HIGH using tca95x5_port_write
           uint16_t colMask = getColumnScanMask(col);
           tca95x5_port_write(&dev, colMask);

           // 2. Read 16-bit port value over I2C in a single transaction via tca95x5_port_read
           uint16_t portVal = 0;
           tca95x5_port_read(&dev, &portVal);
           uint8_t rowBits = portVal & 0x3F; // Pins P00-P05

           // 3. Process bitmask via scanner or matrix map
           for (uint8_t row = 0; row < 6; ++row) {
               bool isPressed = !(rowBits & (1 << row)); // LOW = pressed
               debounceAndQueue(row, col, isPressed);
           }
       }

       // 4. Reset idle column state (all columns LOW) so next keypress triggers INT
       tca95x5_port_write(&dev, 0x0000);

       // 5. Read TCA9555 port registers once more to clear and reset interrupt line
       uint16_t dummy = 0;
       tca95x5_port_read(&dev, &dummy);
   }
   ```


---

## 5. How to Modify Keypad Mappings (e.g. 6x9 Matrix Layout)

To change physical keypad layout or dimensions (e.g. from 6x8 to 6x9 layout with custom key assignments):

### Step 1: Update Physical Key Enums in `KeyCodes.h`
If the 6x9 layout introduces new physical keys (e.g., extra function key `F5` or custom buttons), append them to `KeyCode` in [KeyCodes.h](file:///home/fih/musings/AbsolutOS/src/input/KeyCodes.h):
```cpp
enum class KeyCode : uint8_t {
    // ... existing keycodes ...
    F5,
    CUSTOM_KEY_1,
    // ...
};
```

### Step 2: Adjust Matrix Dimensions in `KeyMatrix.h`
Modify row and column counts in [KeyMatrix.h](file:///home/fih/musings/AbsolutOS/src/input/KeyMatrix.h):
```cpp
static constexpr uint8_t MATRIX_ROWS = 6;
static constexpr uint8_t MATRIX_COLS = 9;

private:
    KeyCode _matrix[MATRIX_ROWS][MATRIX_COLS];
```

### Step 3: Define 6x9 Layout Table in `KeyMatrix.cpp`
Update layout initializer in `KeyMatrix::KeyMatrix()` in [KeyMatrix.cpp](file:///home/fih/musings/AbsolutOS/src/input/KeyMatrix.cpp):
```cpp
// Row 0
_matrix[0][0] = KeyCode::SHIFT;  _matrix[0][1] = KeyCode::ALPHA; _matrix[0][2] = KeyCode::MODE;
_matrix[0][3] = KeyCode::SETUP;  _matrix[0][4] = KeyCode::F1;    _matrix[0][5] = KeyCode::F2;
_matrix[0][6] = KeyCode::F3;     _matrix[0][7] = KeyCode::F4;    _matrix[0][8] = KeyCode::F5;

// Row 1 ... Row 5 mapped up to column index 8 (9 columns total)
```

### Step 4: Update `ProductionKeypadScanner` Bounds (if using Production Scanner)
The `ProductionKeypadScanner` receives row ingestion via `ingestRow(row, pressedColumns, nowMs)`.
Because `pressedColumns` is passed as a `uint16_t` bitmask, it already supports up to 16 columns natively!
Simply pass bit 0 to bit 8 corresponding to columns 0 to 8:
```cpp
// Column 0 = Bit 0 ... Column 8 = Bit 8
uint16_t rowMask = 0;
for (int c = 0; c < 9; ++c) {
    if (isColPressed(c)) rowMask |= (1 << c);
}
scanner.ingestRow(row, rowMask, millis());
```

### Step 5: Update Semantic Mappings & LVGL Navigation (if needed)
- **`KeySemanticResolver.cpp`**: If new key codes need semantic plane behavior (SHIFT / ALPHA variations), add entries to the semantic table index `mappingIndex(physicalCode)`.
- **`LvglKeypad.cpp`**: If new physical keys act as menu navigation (e.g. TAB, OK, CANCEL), update `LvglKeypad::toLvKey(code)` to map them to LVGL controls (`LV_KEY_NEXT`, `LV_KEY_PREV`, `LV_KEY_ENTER`, `LV_KEY_ESC`).
