/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * GameBoyCore.h — host-side wrapper around the vendored Walnut-CGB core.
 *
 * WHY THIS EXISTS
 *   walnut_cgb.h is a C single-header emulator. Including it from an app TU
 *   would drag 280 KB of C into the app, force its compile-time macros on every
 *   translation unit, and bake C-only constructs into the LVGL side of things.
 *   This wrapper keeps the header in ONE .cpp (GameBoyCore.cpp) and exposes a
 *   small C++ surface: load a ROM, step one frame, read the RGB565 framebuffer,
 *   read/write cartridge RAM, set buttons.
 *
 * RENDERING MODEL
 *   Walnut-CGB draws the LCD line by line via a callback, not as a framebuffer
 *   push. That suits this project: the callback writes one 160-pixel RGB565 row
 *   into our own 160x144 buffer, which the app then blits with lv_draw_image()
 *   (the same pattern ParticleLabApp uses for its 160x120 canvas).
 *
 * PORTABILITY NOTE
 *   The whole ROM image is held in RAM and the callbacks index straight into it.
 *   That is fine on a PC and on the ESP32-S3 with PSRAM; the on-device port that
 *   streams ROMs from SD will need to swap these callbacks for paged reads. The
 *   callback seam is kept precisely so that change stays local to this file.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace numos {
namespace emulation {

/** Game Boy buttons, in the core's JOYPAD_* bit order. */
enum class GbButton : uint8_t {
    A = 0,
    B,
    Select,
    Start,
    Right,
    Left,
    Up,
    Down,
    COUNT
};

class GameBoyCore {
public:
    static constexpr int kWidth  = 160;
    static constexpr int kHeight = 144;

    GameBoyCore();
    ~GameBoyCore();

    GameBoyCore(const GameBoyCore&)            = delete;
    GameBoyCore& operator=(const GameBoyCore&) = delete;

    // ── ROM ──────────────────────────────────────────────────────────────
    /**
     * Initialise the core from an in-memory ROM image.
     *
     * The ROM is copied; the caller keeps ownership of @p data. A cartridge
     * RAM buffer of the size the cartridge declares is allocated here, so
     * saveSize() is valid immediately after a successful load.
     *
     * @param data  ROM bytes (>= 0x150 — the header must be present).
     * @param size  ROM size in bytes.
     * @return true on success; false leaves the core unloaded and
     *         lastInitError() holds the core's error code.
     */
    bool loadRom(const uint8_t* data, size_t size);

    /** Tear down the core and free the ROM / cartridge RAM buffers. */
    void unload();

    /** True when a ROM is loaded and the core is initialised. */
    bool ready() const;

    /** ROM header title, padded to 16 chars ("" when not loaded). */
    const char* title() const;

    /** True when the cartridge requests Game Boy Color mode. */
    bool isCgb() const;

    /** Cartridge RAM size declared by the cartridge (0 = no save data). */
    size_t saveSize() const;

    /** Core init error code from the last loadRom() (0 = GB_INIT_NO_ERROR). */
    int lastInitError() const;

    /** Human-readable form of an init error code (static, for UI messages). */
    static const char* initErrorText(int code);

    // ── Execution ────────────────────────────────────────────────────────
    /** Run exactly one emulated frame (~1/59.7 s of game time). */
    void stepFrame();

    /** Emulated frames completed since loadRom(). */
    uint64_t framesRun() const;

    // ── Video ────────────────────────────────────────────────────────────
    /**
     * RGB565 framebuffer, kHeight rows of kWidth pixels, row-major, valid until
     * the next stepFrame()/unload(). Little-endian RGB565 (the project's panel
     * order; the core's big-endian mode stays off).
     */
    const uint16_t* framebuffer() const;

    /** Monotonic frame counter the framebuffer was last written on. */
    // (kept internal: the app compares framebuffers by invalidating every frame)

    // ── Input ────────────────────────────────────────────────────────────
    /** Press (true) or release (false) one button. */
    void setButton(GbButton button, bool pressed);

    /** Release every button (used on app exit and on bus/transition events). */
    void releaseAllButtons();

    // ── Save data ────────────────────────────────────────────────────────
    /** Cartridge RAM, or nullptr when the cartridge has none. */
    const uint8_t* cartRam() const;
    uint8_t*       cartRam();

    /**
     * Copy @p len bytes of cartridge RAM into the core.
     * A length that does not exactly match saveSize() is refused: a truncated
     * or oversized save file must never be partially applied.
     */
    bool loadCartRam(const uint8_t* data, size_t len);

    /** True when cartridge RAM has been modified since the last clearDirty(). */
    bool saveDirty() const;

    /** Mark cartridge RAM as written out. */
    void clearSaveDirty();

private:
    /**
     * Opaque implementation state (ROM bytes, cartridge RAM, framebuffer, and
     * the core's gb_s). Defined in GameBoyCore.cpp.
     *
     * Deliberately declared PUBLIC as a *name* only: the core's C callbacks are
     * free functions in that .cpp and must be able to name the type they cast
     * gb->direct.priv to. The definition never appears in this header, so
     * nothing about the emulator internals is reachable from consumer TUs.
     */
public:
    struct Impl;
private:
    Impl* _impl = nullptr;
};

}  // namespace emulation
}  // namespace numos
