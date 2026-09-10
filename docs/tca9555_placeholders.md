# TCA9555 I2C Keypad Matrix Implementation - Placeholders

This document lists all placeholder values used in the TCA9555 I2C keypad matrix implementation that need to be updated by a human to match the design constraints.

## Key Mapping Array Placeholders

The `_map[ROWS][COLS]` array in `src/drivers/Keyboard.cpp` contains a temporary 6×9 mapping that serves as a placeholder. The original key mapping was designed for a different matrix layout and needs to be updated to match the physical 6×9 keypad layout.

Current placeholder mapping (lines 226-236 in Keyboard.cpp):

```cpp
const KeyCode Keyboard::_map[Keyboard::ROWS][Keyboard::COLS] = {
    // C0          C1          C2          C3          C4          C5
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
```

## Notes

- The original key mapping was designed for a 5×10 matrix layout
- The new implementation uses a 6×9 matrix layout (6 columns, 9 rows)
- The physical connections between the TCA9555 pins and the actual keypad buttons need to be verified and mapped correctly
- The current mapping assigns arbitrary keys to each position and should be replaced with the correct physical layout mapping
- All 54 positions (6×9) should eventually have meaningful KeyCode assignments, though some may legitimately remain as KeyCode::NONE if no button is physically present

## Action Required

A human developer needs to update the `_map` array to reflect the actual physical button layout and functionality of the 6×9 keypad matrix.