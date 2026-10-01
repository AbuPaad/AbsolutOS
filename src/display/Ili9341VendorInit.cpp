#include "Ili9341VendorInit.h"

#if defined(ARDUINO)

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "vendor_init.h"

/* The two values this project's pixel path depends on, locked to the table. */
static_assert(VI_36[0] == 0x08,
              "rotation-0 MADCTL base must equal the vendor init's 0x36 byte");
static_assert(VI_3A[0] == 0x55,
              "the vendor init must leave the panel in 16-bit/pixel DBI mode");

namespace numos::display {

uint16_t vendorPanelInit(TFT_eSPI& tft) {
    // One locked transaction for the whole sequence: keeps CS low between
    // commands and avoids per-byte begin/end overhead.  TFT_eSPI 2.5.43's
    // writecommand()/writedata() short-circuit their own begin when the
    // transaction is locked by startWrite().
    tft.startWrite();
    for (size_t i = 0; i < VENDOR_INIT_N; ++i) {
        const VendorInitStep& step = VENDOR_INIT[i];
        tft.writecommand(step.cmd);
        for (uint8_t k = 0; k < step.nargs; ++k) {
            tft.writedata(step.args[k]);
        }
        if (step.delay_after_ms != 0) {
            delay(step.delay_after_ms);
        }
    }
    tft.endWrite();
    return static_cast<uint16_t>(VENDOR_INIT_N);
}

} // namespace numos::display

#endif
