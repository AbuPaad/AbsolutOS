// Simple test to check if TCA constants are properly defined
#define ARDUINO
#define NUMOS_BOARD_PROD_WROOM1U_N16R8 0

// Mock Arduino.h for test
namespace ArduinoMock {
    typedef unsigned char uint8_t;
    typedef unsigned short uint16_t;
    typedef unsigned int uint32_t;
    #define INPUT 0
    #define OUTPUT 1
    #define LOW 0
    #define HIGH 1
}

using namespace ArduinoMock;

// Mock Wire for I2C
struct TwoWire {};
extern TwoWire Wire;

// Include our header
#include "src/drivers/Keyboard.h"

// Try to use the constants to make sure they're defined
void test_constants() {
    uint8_t pin = TCA_P00;  // Should be defined
    pin = TCA_P01;
    pin = TCA_P02;
    pin = TCA_P03;
    pin = TCA_P04;
    pin = TCA_P05;
    pin = TCA_P06;
    pin = TCA_P10;
    pin = TCA_P11;
    pin = TCA_P12;
    pin = TCA_P13;
    pin = TCA_P14;
    pin = TCA_P15;
    pin = TCA_P16;
    pin = TCA_P17;
}