#pragma once
/* ============================================================================
   vendor_init.h -- the OEM panel init sequence, transcribed byte for byte.

   Transcribed verbatim from the bench project
   (/home/fih/musings/esp32s3-ili9341-bringup/src/vendor_init.h), which itself
   came from "ILI9341_HSD2.4_G2.2_(调试OK).txt" in the 2.4" OEM kit.  The bench
   pin config is the display source of truth; do not edit the bytes here to
   "clean up" the sequence, and diff any change against the OEM file.

   Fidelity notes:
     * Command order, parameters and comments are the vendor's.
     * The table encodes (cmd, nargs, args, delay_after_ms).
     * Vendor reset timing lives in the caller, not here:
         nRESET=1, 1 ms; nRESET=0, 10 ms ("necessary"); nRESET=1, 120 ms.
     * 0x11 (Exit Sleep) carries delay_after_ms = 120, matching "Delayms(120)".
     * The vendor's 0x3A=0x55 (DBI 16 bits/pixel) and 0x36=0x08 (BGR, no
       mirroring) are asserted in Ili9341VendorInit.cpp rather than trusted.
   ============================================================================ */

#include <stdint.h>
#include <stddef.h>

struct VendorInitStep {
  uint8_t        cmd;
  uint8_t        nargs;
  const uint8_t *args;
  uint16_t       delay_after_ms;
};

/* --- the vendor's parameter lists, in their original order ---------------- */
constexpr uint8_t VI_CF[] = { 0x00, 0xC1, 0x30 };
constexpr uint8_t VI_ED[] = { 0x64, 0x03, 0x12, 0x81 };
constexpr uint8_t VI_E8[] = { 0x85, 0x00, 0x79 };
constexpr uint8_t VI_CB[] = { 0x39, 0x2C, 0x00, 0x34, 0x02 };
constexpr uint8_t VI_F7[] = { 0x20 };
constexpr uint8_t VI_EA[] = { 0x00, 0x00 };
constexpr uint8_t VI_C0[] = { 0x1D };              /* Power control, VRH[5:0] */
constexpr uint8_t VI_C1[] = { 0x12 };              /* Power control, SAP/BT    */
constexpr uint8_t VI_C5[] = { 0x33, 0x3F };        /* VCM control              */
constexpr uint8_t VI_C7[] = { 0x92 };              /* VCM control              */
constexpr uint8_t VI_3A[] = { 0x55 };              /* DBI: 16 bits / pixel     */
constexpr uint8_t VI_36[] = { 0x08 };              /* MADCTL: BGR, portrait    */
constexpr uint8_t VI_B1[] = { 0x00, 0x12 };
constexpr uint8_t VI_B6[] = { 0x0A, 0xA2 };        /* Display Function Control */
constexpr uint8_t VI_44[] = { 0x02 };              /* Set Tear Scanline        */
constexpr uint8_t VI_F2[] = { 0x00 };              /* 3Gamma Function Disable  */
constexpr uint8_t VI_26[] = { 0x01 };              /* Gamma curve selected     */

constexpr uint8_t VI_E0[] = {                      /* Set Gamma (positive) */
  0x0F, 0x22, 0x1C, 0x1B, 0x08, 0x0F, 0x48, 0xB8,
  0x34, 0x05, 0x0C, 0x09, 0x0F, 0x07, 0x00
};
constexpr uint8_t VI_E1[] = {                      /* Set Gamma (negative) */
  0x00, 0x23, 0x24, 0x07, 0x10, 0x07, 0x38, 0x47,
  0x4B, 0x0A, 0x13, 0x06, 0x30, 0x38, 0x0F
};

/* --- the sequence -------------------------------------------------------- */
static const VendorInitStep VENDOR_INIT[] = {
  { 0xCF, 3, VI_CF,   0 },   /* Power control B (undocumented)   */
  { 0xED, 4, VI_ED,   0 },   /* Power on sequence control        */
  { 0xE8, 3, VI_E8,   0 },   /* Driver timing control A          */
  { 0xCB, 5, VI_CB,   0 },   /* Power control A                  */
  { 0xF7, 1, VI_F7,   0 },   /* Pump ratio control               */
  { 0xEA, 2, VI_EA,   0 },   /* Driver timing control B          */
  { 0xC0, 1, VI_C0,   0 },
  { 0xC1, 1, VI_C1,   0 },
  { 0xC5, 2, VI_C5,   0 },
  { 0xC7, 1, VI_C7,   0 },
  { 0x3A, 1, VI_3A,   0 },   /* 16-bit/pixel -- driver asserts this */
  { 0x36, 1, VI_36,   0 },   /* MADCTL       -- driver asserts this */
  { 0xB1, 2, VI_B1,   0 },
  { 0xB6, 2, VI_B6,   0 },
  { 0x44, 1, VI_44,   0 },
  { 0xF2, 1, VI_F2,   0 },
  { 0x26, 1, VI_26,   0 },
  { 0xE0, 15, VI_E0,  0 },
  { 0xE1, 15, VI_E1,  0 },
  { 0x11, 0, nullptr, 120 }, /* Exit Sleep + Delayms(120)          */
  { 0x29, 0, nullptr, 0 },   /* Display on                         */
};

static const size_t VENDOR_INIT_N = sizeof(VENDOR_INIT) / sizeof(VENDOR_INIT[0]);

/* The two values the pixel path depends on. */
static const uint8_t VENDOR_DBI_16BIT  = 0x55;
static const uint8_t VENDOR_MADCTL     = 0x08;
