/**
 * NumosHost.h — the ESP32/desktop host layer for the vendored fake-08 VM.
 *
 * fake-08 declares `Host` in source/host.h but leaves the platform half of it
 * undefined: every port supplies those `Host::` definitions in its own .cpp
 * (see platform/SDL2Common/source/sdl2basehost.cpp). This is that file for
 * NumOS. The rest of Host (palette, settings, cartdata) comes from the vendored
 * source/hostCommonFunctions.cpp.
 *
 * Frame path: fake-08 hands us 8-bit palette indices plus a 144-colour palette
 * map. We expand them through a 256-entry RGB565 LUT into a caller-owned
 * 128×128 buffer — the destination the LVGL image reads.
 */

#pragma once

#include <cstdint>

namespace numos_pico8 {

/// Held PICO-8 button bits, using fake_08's P8_KEY_* masks.
void setHeldButtons(uint8_t held);

/// Size of the RGB565 frame the host writes (128×128).
constexpr int kFrameW = 128;
constexpr int kFrameH = 128;

/// The last converted frame, RGB565. Valid after the first drawFrame().
const uint16_t* frame565();

/// True once the host has produced at least one frame.
bool hasFrame();

}  // namespace numos_pico8
