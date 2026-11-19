#include "ProductionSafeStartup.h"

#if defined(ARDUINO) && defined(NUMOS_BOARD_PROD_WROOM1U_N16R8) && \
    NUMOS_BOARD_PROD_WROOM1U_N16R8

#include <Arduino.h>
#include "BoardProfile.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_periph.h"

namespace numos::hardware {

void applyProductionSafeStartup() {
    constexpr auto& display = kProductionBoard.display;

    // The display uses the JTAG pins (GPIO39-42). After reset these pins are
    // in JTAG function, so reconfigure them to GPIO before any pinMode/
    // analogWrite touches them. Non-destructive; no EFUSE burn required.
    for (const int gpio : {39, 40, 41, 42}) {
        if (gpio >= 0 && gpio < SOC_GPIO_PIN_COUNT) {
            PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
        }
    }

    // WHY: Assert inactive levels before switching to OUTPUT so CS and the
    // transistor-driven backlight cannot glitch during framework startup.
    digitalWrite(display.chipSelect.gpio, HIGH);
    pinMode(display.chipSelect.gpio, OUTPUT);

    // Backlight is on a fixed rail: no GPIO is allocated for it (-1).
    if (display.backlight.gpio >= 0) {
        digitalWrite(display.backlight.gpio, LOW);
        pinMode(display.backlight.gpio, OUTPUT);
    }

    digitalWrite(display.reset.gpio, HIGH);
    pinMode(display.reset.gpio, OUTPUT);

    // ILI9341 SPI mode 0 idles SCLK low. MOSI/DC remain untouched until the
    // hardware SPI/display driver takes ownership.
    digitalWrite(display.clock.gpio, LOW);
    pinMode(display.clock.gpio, OUTPUT);
}

} // namespace numos::hardware

#endif
