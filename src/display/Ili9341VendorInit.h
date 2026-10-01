#pragma once
/* ============================================================================
   Ili9341VendorInit.h -- replay the OEM ILI9341 init sequence through TFT_eSPI.

   The product does not need the bench's SPI/G RAM driver: TFT_eSPI already owns
   the bus and provides the graphics layer.  The one asset worth transplanting
   is the vendor's signed-off init table (power/VCOM/gamma/frame-rate/display-
   function), which a generic library cannot tune for a specific piece of glass.

   Only the write path is re-targeted: TFT_eSPI's writecommand()/writedata()
   instead of the bench's raw:: layer, so there is exactly one owner of the
   SPIClass.  raw::, SPI.begin(), CS/DC handling and GPIO checks are deliberately
   NOT ported.
   ============================================================================ */

#include <stdint.h>

#if defined(ARDUINO)

class TFT_eSPI;

namespace numos::display {

/* Replays VENDOR_INIT through `tft`.  Must be called after the controller has
   been reset and initialised, and before setRotation()/the final MADCTL write,
   so the product's rotation/MADCTL decision remains the last writer.

   Returns the number of commands sent (VENDOR_INIT_N). */
uint16_t vendorPanelInit(TFT_eSPI& tft);

} // namespace numos::display

#endif
