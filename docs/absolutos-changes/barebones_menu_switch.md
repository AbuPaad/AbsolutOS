# Barebones Casio UI Switch Mechanism Plan for MainMenu

## 1. Executive Summary & Objectives

This document details the architectural plan to add a dual-UI theme switch mechanism to `MainMenu` (`src/ui/MainMenu.h` and `src/ui/MainMenu.cpp`, packed in `codemix/mainmenugui.xml`).

### Key Principles
- **Purely Additive Code**: All changes extend the existing `MainMenu` class cleanly without removing existing structures, API contracts, or lifecycle flows.
- **Shared App Registry**: Parses the pre-existing `APPS[]` table defined in `MainMenu.cpp` to populate both UI modes dynamically.
- **Identical Lifecycle**: Opens, loads, destroys, and dispatches callbacks (`_launchCb`, `create()`, `load()`, `group()`, `screen()`) identically regardless of the active theme mode.
- **Boolean Theme State**: Simple `bool _barebonesTheme = false;` member in `MainMenu`.

### UI Mode Characteristics

| Feature | Standard High-Fidelity Mode | Barebones Casio LCD Mode |
| :--- | :--- | :--- |
| **Aesthetic** | Modern 3-column card grid with geometric vector icons | Classic monochrome LCD plaintext app listing |
| **Item Layout** | White cards, shadows, borders | Pure text rows formatted as `(1): Calculation` |
| **Typography** | Default anti-aliased font (`LV_FONT_DEFAULT` / Montserrat) | Custom LVGL bitmap font (`casio_matrix_font`) |
| **Background** | Dot-grid pattern `#D1D1D1` on `#F5F5F5` | Solid Casio Green (`#3A740C`) |
| **Animations / Highlight**| 150 ms card border & focus scale overshoot | No card highlighting, no borders, no animations |
| **Navigation** | 3-column flex grid scrollable vertically | Vertical plaintext list scrollable page-by-page |

---

## 2. Technical Context & MainMenu Analysis

From inspecting `codemix/mainmenugui.xml`:
1. `MainMenu` owns:
   - `_screen`: Root LVGL screen object.
   - `_grid`: Main container object handling layout and scrolling.
   - `_group`: LVGL input group (`lv_group_t*`) handling keypad focus.
   - `_launchCb`: Callback `std::function<void(int)>` triggered when an app is launched.
   - `APPS[]` table: Defines all system apps (`{id, name, color, colorLight}`).
2. **Lifecycle Flow**:
   - `create()` initializes group, screen, status bar, and grid layout.
   - `load()` loads `_screen` into LVGL display.
   - Event callbacks (`onCardEvent`) process input events and dispatch `_launchCb(app_id)`.
3. **Additive Integration Strategy**:
   - Use `bool _barebonesTheme = false;` in `MainMenu.h`.
   - Branch `buildGrid()` inside `create()` to construct either the standard card grid or the barebones list based on `_barebonesTheme`.

---

## 3. LVGL Bitmap Font Integration & File Location

LVGL uses binary/C bitmap fonts described by `lv_font_t` structs (generated via LVGL Font Converter).

### 1. Font Source & Generation
- **Source**: Casio 5x7/7x9 calculator matrix font converted via LVGL Online Font Converter (`https://lvgl.io/tools/fontconverter`).
- **Configuration**:
  - Size: `9 px` or `11 px`
  - BPP: `1 bit-per-pixel` (Monochrome bitmap)
  - Range: `0x20-0x7E` (Standard ASCII)
- **Output C File Path**: `src/ui/fonts/casio_matrix_font.c`
- **Output Header Path**: `src/ui/fonts/casio_matrix_font.h`

### 2. Build Integration & Retrieval
- **Build System**: PlatformIO automatically compiles all `.c` files placed under `src/ui/fonts/`.
- **In-Code Declaration**:
  In `src/ui/fonts/casio_matrix_font.h` (or directly in `MainMenu.cpp` via LVGL macro):
  ```cpp
  #include "lvgl.h"
  LV_FONT_DECLARE(casio_matrix_font);
  ```
- **Font Application in `MainMenu.cpp`**:
  ```cpp
  lv_obj_set_style_text_font(label, &casio_matrix_font, LV_PART_MAIN);
  ```

---

## 4. Keycode Handling via LVGL Event Callbacks (`AC && Alpha`)

Instead of inventing non-LVGL input functions, key events are handled strictly using **LVGL's native `LV_EVENT_KEY` callback mechanism** already present in `MainMenu`.

### 1. LVGL Key Event Flow
1. `LvglKeypad` / input driver pushes key events into LVGL's input device (`lv_indev_t`).
2. Key events (e.g. `LV_KEY_CUSTOM_AC_ALPHA` or `AC`+`Alpha` chord key code) are dispatched to focused objects or `_screen` via `LV_EVENT_KEY`.
3. `MainMenu` registers an event handler on `_screen` / `_grid` for `LV_EVENT_KEY`.

### 2. Event Handler Implementation (`onScreenEvent` / `onCardEvent`)

In `MainMenu.cpp`:

```cpp
void MainMenu::onScreenEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        MainMenu* self = static_cast<MainMenu*>(lv_event_get_user_data(e));
        
        // Match keycode for AC + Alpha key chord
        if (key == LV_KEY_CUSTOM_AC_ALPHA && self) {
            self->toggleTheme();
        }
    }
}
```

### 3. Theme Toggle & Re-render Method
```cpp
void MainMenu::toggleTheme() {
    _barebonesTheme = !_barebonesTheme;
    create(); // Re-create menu UI cleanly using existing lifecycle
}
```

---

## 5. Additive Code Modifications Plan

### Step 1: Extensions in `src/ui/MainMenu.h`

Add `bool _barebonesTheme` and helper methods to `MainMenu`:

```cpp
public:
    /** Toggle barebones Casio LCD mode */
    void setBarebonesTheme(bool enabled) { _barebonesTheme = enabled; create(); }
    bool barebonesTheme() const { return _barebonesTheme; }
    void toggleTheme();

private:
    bool _barebonesTheme = false;  ///< false = NumWorks HD grid, true = Barebones Casio LCD list

    /** LVGL screen-level key event callback */
    static void onScreenEvent(lv_event_t* e);

    /** Build plaintext Casio calculator list layout */
    void buildBarebonesGrid();

    /** Create a single barebones plaintext entry line */
    lv_obj_t* buildBarebonesItem(lv_obj_t* parent, int num, const AppEntry& app);
```

---

### Step 2: Extensions in `src/ui/MainMenu.cpp`

#### 1. Color and Font Declarations
```cpp
// Casio LCD Green Background (#3A740C)
static constexpr uint32_t CASIO_BG_COLOR = 0x3A740C;

// Font retrieved from src/ui/fonts/casio_matrix_font.c
LV_FONT_DECLARE(casio_matrix_font);
```

#### 2. Update `create()` Lifecycle Method
```cpp
void MainMenu::create() {
    // Standard cleanup of previous LVGL objects...

    // Listen for LV_EVENT_KEY on root screen for AC + Alpha chord
    lv_obj_add_event_cb(_screen, onScreenEvent, LV_EVENT_KEY, this);

    if (_barebonesTheme) {
        // Set solid Casio green background on root screen (#3A740C)
        lv_obj_set_style_bg_color(_screen, lv_color_hex(CASIO_BG_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, LV_PART_MAIN);
        
        buildStatusBar();
        buildBarebonesGrid();
    } else {
        // Standard NumWorks HD rendering
        buildStatusBar();
        buildGrid();
    }
}
```

#### 3. Implement `buildBarebonesGrid()`
Construct a lightweight container with vertical flex scrolling:

```cpp
void MainMenu::buildBarebonesGrid() {
    _grid = lv_obj_create(_screen);
    lv_obj_set_size(_grid, SCREEN_W, GRID_H);
    lv_obj_set_pos(_grid, 0, STATUS_BAR_H);
    
    // Background styling (#3A740C, no dot pattern)
    lv_obj_set_style_bg_color(_grid, lv_color_hex(CASIO_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_grid, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_grid, 4, LV_PART_MAIN);

    // Flex vertical layout for plaintext list
    lv_obj_set_flex_flow(_grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_grid, LV_SCROLLBAR_MODE_AUTO);

    _firstCard = nullptr;

    // Parse existing APPS[] registry table
    for (int i = 0; i < APP_COUNT; ++i) {
        lv_obj_t* item = buildBarebonesItem(_grid, i + 1, APPS[i]);
        if (!_firstCard) _firstCard = item;
    }
}
```

#### 4. Implement `buildBarebonesItem()`
Create plaintext items formatted as `(number): AppName` without cards, shadows, icons, or scale/overshoot animations:

```cpp
lv_obj_t* MainMenu::buildBarebonesItem(lv_obj_t* parent, int num, const AppEntry& app) {
    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, 20); // Compact single-row height
    
    // Completely strip standard card borders, backgrounds, shadows
    lv_obj_remove_style_all(item);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_PART_MAIN);

    // Plaintext Label: "(number): AppName"
    char labelBuf[48];
    snprintf(labelBuf, sizeof(labelBuf), "(%d): %s", num, app.name);

    lv_obj_t* label = lv_label_create(item);
    lv_label_set_text(label, labelBuf);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    
    // Apply LVGL custom bitmap font from src/ui/fonts/casio_matrix_font.c
    lv_obj_set_style_text_font(label, &casio_matrix_font, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);

    // Bind event callback to existing app launcher handler
    lv_obj_add_event_cb(item, onCardEvent, LV_EVENT_CLICKED, reinterpret_cast<void*>(app.id));
    lv_group_add_obj(_group, item);

    return item;
}
```

#### 5. Bypass Dot-Grid Pattern in `onGridDraw()`
```cpp
void MainMenu::onGridDraw(lv_event_t* e) {
    MainMenu* self = static_cast<MainMenu*>(lv_event_get_user_data(e));
    if (self && self->_barebonesTheme) {
        return; // Do not render dot-grid on Casio green background
    }
    // Existing dot-grid drawing logic...
}
```

---

## 6. Verification & Validation Plan

1. **Compilation & Additive Integrity**:
   - Ensure existing header files and signature expectations remain 100% compliant.
   - Verify code compiles cleanly with no missing symbols.
2. **UI & Theme Toggle Verification**:
   - Send `AC + ALPHA` key event (`LV_EVENT_KEY`) while in `MainMenu`.
   - Confirm immediate switch between standard NumWorks HD card grid and Barebones Casio LCD mode.
3. **Visual & Formatting Checks**:
   - Background changes strictly to `#3A740C`.
   - Text entries render using the bitmap font from `src/ui/fonts/casio_matrix_font.c`.
   - App list displays plaintext lines formatted as `(1): Calculation`, `(2): Grapher`, etc.
   - Zero card containers, white boxes, icons, shadows, scale/overshoot focus animations, or card highlighting are drawn.
4. **App Launch & Navigation Verification**:
   - Scroll through list page-by-page.
   - Press `ENTER` / key number to launch targeted app. Confirm `_launchCb` executes normally.
