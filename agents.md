# AI Agent Guidelines & Technical Requirements: NumOS (AbsolutOS)

This document provides system specifications, framework details, dependency requirements, and mandatory code design limitations for AI assistants working on the **NumOS (AbsolutOS)** codebase.

---

## 1. Target Hardware & Physical Specifications

- **Target MCU**: ESP32-S3 (Xtensa® Dual-Core 32-bit LX7 @ 240 MHz).
- **Module**: `ESP32-S3-WROOM-1` / `ESP32-S3-WROOM-1U` (N16R8).
- **Flash Memory**: 16 MB QIO Flash @ 80 MHz (`flash_mode = dio` in boot header for ROM loader compatibility; 16 MB partition table `default_16MB.csv`).
- **PSRAM**: 8 MB Octal SPI (OPI) PSRAM @ 80 MHz (`-DBOARD_HAS_PSRAM`, `-mfix-esp32-psram-cache-issue`).
- **Display Driver**: ILI9341 (240x320, BGR color order) via FSPI port (`-DUSE_FSPI_PORT`).
- **USB / Serial Backend**: Native USB CDC / JTAG on GPIO19/20 (`-DARDUINO_USB_MODE=1`, `-DARDUINO_USB_CDC_ON_BOOT=1`, `-DNUMOS_SERIAL_BACKEND_USB_CDC=1`).
- **Filesystem**: LittleFS.

---

## 2. Frameworks & Build Environments

### 2.1 Core Frameworks & Standards
- **Firmware Framework**: `arduino` (ESP32 Arduino Core built on ESP-IDF).
- **C++ Standard**: C++17 (`-std=gnu++17`). C++11 (`-std=gnu++11`) is explicitly unflagged.
- **Exceptions & RTTI**:
  - `-fno-rtti` is set in build unflags.
  - `-fexceptions` is **explicitly re-enabled** in `build_flags` (required by Giac/KhiCAS).

### 2.2 Dual-Target Architecture
The codebase targets two distinct runtimes:
1. **ESP32-S3 Device Firmware** (`env:numos-esp32-s3-wroom-1u-n16r8`, `env:esp32s3_n16r8`, etc.): Executes on physical hardware.
2. **PC Native Emulator** (`env:emulator_pc`): Executes natively on x86/x64 host systems via SDL2 for rapid GUI and logic testing.

---

## 3. Libraries & Dependencies

| Library / Component | Version / Source | Purpose & Configuration |
| :--- | :--- | :--- |
| **LVGL** | `lvgl/lvgl@^9.2.0` | Primary GUI library. Uses simple include headers (`-DLV_CONF_INCLUDE_SIMPLE`). Frame refresh rate set to 16ms (`-DLV_DEF_REFR_PERIOD=16`). Assembly optimization disabled for cross-platform safety. |
| **TFT_eSPI** | `bodmer/TFT_eSPI` | Low-level SPI display driver configured for ILI9341 (`-DILI9341_DRIVER=1`). |
| **Giac / KhiCAS** | `file://lib/giac` (Vendored) | Embedded Computer Algebra System (CAS). Compiled with `-DNUMOS_USE_GIAC=1`, `-DGIAC_KHICAS`, `-DNO_GUI`, `-DGIAC_GENERIC`, `-DEMBEDDED`, `-DDOUBLEVAL`, `-DHAVE_CONFIG_H`. |
| **libtommath** | `file://lib/libtommath` (Vendored) | Multi-precision integer math support for Giac engine. |

---

## 4. PlatformIO Configuration Rules

- **Library Dependency Finder (LDF)**: `lib_ldf_mode = deep+` is required to resolve deep local dependencies in `lib/giac` and `lib/libtommath`.
- **Library Archiving**: `lib_archive = no` is set to ensure local headers and template instantiations link cleanly.
- **Stack Allocation**: Arduino main loop stack size is expanded to 64 KB (`-DARDUINO_LOOP_STACK_SIZE=65536`) to accommodate deep CAS/Giac evaluation trees and LVGL rendering.
- **Pre/Post Build Scripts**:
  - `scripts/build_metadata.py`: Generates build time & version metadata.
  - `scripts/display_perf_lvgl_o2.py`: Applies `-O2` compiler optimizations to LVGL rendering files.
  - `scripts/production_factory_image.py`: Packages production factory images.
  - `scripts/sdl2_env.py`: Injects native host SDL2 environment flags for PC emulation.

---

## 5. Constraints & Guidelines for AI-Generated Code

When generating or modifying code for this project, AI agents MUST follow these mandatory rules:

### 5.1 Language & Compiler Requirements
- **C++17 Compatibility**: Use C++17 features standardly. Do not use C++20 constructs.
- **Exception Safety**: Giac/CAS modules rely on C++ exceptions (`try`, `catch`, `throw`). Any C++ code interacting with Giac must handle exceptions gracefully to prevent micro-controller crashes or unhandled aborts.

### 5.2 Memory & Allocation Limitations
- **Internal SRAM vs. External PSRAM**:
  - Internal SRAM is scarce (~328 KB max static RAM). Avoid allocating large buffers, arrays, or cache tables on the stack or internal static heap.
  - Large data structures (e.g., render buffers, graph datasets, history caches, AST node pools) must be allocated in PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` or dedicated PSRAM wrappers).
- **Stack Conservation**: Despite the 64 KB loop stack (`ARDUINO_LOOP_STACK_SIZE=65536`), deep recursive function calls (such as in mathematical expression parsing or AST simplification) must be bounded or rewritten iteratively where possible.

### 5.3 Hardware Isolation & Portability Rules
- **Hardware Abstraction Layer (HAL)**: Hardware-specific APIs (`Arduino.h`, `Preferences.h`, `nvs_flash.h`, `driver/gpio.h`) **MUST NOT** be called directly inside generic UI components, mathematical engines, or application models.
- **Conditional Compilation**: Use `#ifdef ARDUINO` for hardware-only paths and `#else` / `hal/ArduinoCompat.h` for PC host emulator support.
- **Display Pins**: **NEVER** hardcode GPIO pin numbers for display or SPI peripherals inside C++ code files. Pins are defined centrally via `platformio.ini` macro flags or `display/ProductionDisplayRuntimeConfig.h`.

### 5.4 LVGL 9.x Development Rules
- **Version Specifics**: Use LVGL 9.x APIs only. Do not introduce deprecated LVGL 8 functions or macros.
- **Thread Safety**: All LVGL UI objects, styles, and event handlers must be modified exclusively on the main LVGL task thread context.
- **PC Emulator Allocation**: Native PC builds enforce standard system malloc (`-DLV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB`) because 64-bit host pointers exhaust LVGL's default 64 KB static pool. Ensure UI code does not assume fixed static pool size constraints.

### 5.5 Source File Registry Maintenance
- The PC native emulator environment (`[env:emulator_pc]`) uses explicit source filtering (`build_src_filter`).
- **Important**: When creating new application files (`src/apps/*.cpp`) or math components (`src/math/*.cpp`), the corresponding path **MUST** be added to `build_src_filter` in `platformio.ini` if it is intended to run on the emulator. Firmware environments compile via `+<*>`.
