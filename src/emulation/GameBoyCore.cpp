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
 * GameBoyCore.cpp — the ONLY translation unit that includes walnut_cgb.h.
 *
 * Compile-time configuration for the vendored core:
 *   · ENABLE_SOUND 0 — Walnut-CGB bundles no APU; leaving it at the default (1)
 *     makes the linker look for audio_read()/audio_write(). Audio is out of
 *     scope for this app, so it stays off on every target.
 *   · WALNUT_GB_16BIT_DMA / WALNUT_GB_32BIT_DMA — the ESP32-S3-friendly DMA
 *     paths. Upstream's SDL2 example requires both OFF on a desktop build, so
 *     this file defaults them to 0 for every non-Arduino target; a firmware
 *     environment can still override them with -D flags. (The vendored header
 *     has these #ifndef-guarded for exactly that reason — see platformio.ini.)
 *   · WALNUT_GB_RGB565_BIGENDIAN stays 0: this project's panels take
 *     little-endian RGB565.
 *
 * The CGB colour path uses the core's own native RGB565 palette table
 * (gb->cgb.fixPalette, BG palettes then OBJ palettes) rather than any SGB
 * approximation database — that is the same source the vendor's SDL2 example
 * reads from.
 */

#include "GameBoyCore.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifndef ENABLE_SOUND
#define ENABLE_SOUND 0
#endif

#ifndef ARDUINO
#ifndef WALNUT_GB_16BIT_DMA
#define WALNUT_GB_16BIT_DMA 0
#endif
#ifndef WALNUT_GB_32BIT_DMA
#define WALNUT_GB_32BIT_DMA 0
#endif
#endif

#include "walnut_cgb.h"

namespace numos {
namespace emulation {

namespace {

/**
 * DMG palettes: 12 entries — BG(4), OBJ0(4), OBJ1(4) — as RGB565.
 *
 * Walnut-CGB defaults to WALNUT_GB_12_COLOUR=1, so a DMG frame arrives as
 * 2-bit shade | 2-bit layer and is indexed with ((p & 18) >> 1) | (p & 3),
 * exactly as upstream's SDL2 example does. Layer bits are what let a front-end
 * give background and both sprite palettes different colours, the way a Game
 * Boy Color colourises a DMG game.
 *
 * The shade values are the ones dmg-acid2 documents for a correct DMG
 * emulator — 8-bit $00 / $55 / $AA / $FF, i.e. RGB565 0x0000 / 0x52AA / 0xAD55
 * / 0xFFFF — so the fixture golden can be compared against the test ROM's
 * published reference image instead of an arbitrary palette. Lightest shade
 * first, as the core reports shade 0 = lightest.
 *
 * All three palettes are greyscale; changing OBJ0/OBJ1 to a distinct colour is
 * the intended way to give sprites their own tint.
 */
constexpr uint16_t kDmgPalette[12] = {
    0xFFFF, 0xAD55, 0x52AA, 0x0000,   // BG
    0xFFFF, 0xAD55, 0x52AA, 0x0000,   // OBJ0
    0xFFFF, 0xAD55, 0x52AA, 0x0000,   // OBJ1
};

/** One JOYPAD_* bit per GbButton, in enum order. Active-low in the core. */
constexpr uint8_t kButtonBit[static_cast<size_t>(GbButton::COUNT)] = {
    JOYPAD_A, JOYPAD_B, JOYPAD_SELECT, JOYPAD_START,
    JOYPAD_RIGHT, JOYPAD_LEFT, JOYPAD_UP, JOYPAD_DOWN
};

}  // namespace

struct GameBoyCore::Impl {
    std::vector<uint8_t> rom;
    std::vector<uint8_t> cartRam;
    uint16_t             fb[kHeight][kWidth] = {};
    gb_s                 gb;
    bool                 loaded       = false;
    bool                 saveDirty    = false;
    int                  initError    = 0;
    size_t               saveSize     = 0;
    uint64_t             frames       = 0;
    char                 title[17]    = {0};
};

// ── Core callbacks (the core holds Impl* in gb->direct.priv) ─────────────────

namespace {

GameBoyCore::Impl* implOf(gb_s* gb) {
    return static_cast<GameBoyCore::Impl*>(gb->direct.priv);
}

uint8_t romRead8(gb_s* gb, const uint_fast32_t addr) {
    const std::vector<uint8_t>& rom = implOf(gb)->rom;
    return addr < rom.size() ? rom[addr] : 0xFF;
}

uint16_t romRead16(gb_s* gb, const uint_fast32_t addr) {
    return static_cast<uint16_t>(romRead8(gb, addr) |
                                 (static_cast<uint16_t>(romRead8(gb, addr + 1)) << 8));
}

uint32_t romRead32(gb_s* gb, const uint_fast32_t addr) {
    return static_cast<uint32_t>(romRead16(gb, addr)) |
           (static_cast<uint32_t>(romRead16(gb, addr + 2)) << 16);
}

uint8_t cartRamRead(gb_s* gb, const uint_fast32_t addr) {
    const std::vector<uint8_t>& ram = implOf(gb)->cartRam;
    return addr < ram.size() ? ram[addr] : 0xFF;
}

void cartRamWrite(gb_s* gb, const uint_fast32_t addr, const uint8_t val) {
    GameBoyCore::Impl* impl = implOf(gb);
    if (addr < impl->cartRam.size()) {
        if (impl->cartRam[addr] != val) impl->saveDirty = true;
        impl->cartRam[addr] = val;
    }
}

void onCoreError(gb_s* gb, const enum gb_error_e error, const uint16_t addr) {
    (void)gb;
    /* The core reports a bad opcode / bad read-write here and keeps running;
       a Game Boy has no "crash screen", so neither do we. Surface it on the
       console for bring-up, never as UI noise. */
    std::printf("[GB] core error %d at 0x%04X\n", static_cast<int>(error), addr);
}

void lcdDrawLine(gb_s* gb, const uint8_t* pixels, const uint_fast8_t line) {
    GameBoyCore::Impl* impl = implOf(gb);
    if (line >= static_cast<uint_fast8_t>(GameBoyCore::kHeight)) return;   // 0..144 documented

    if (gb->cgb.cgbMode) {
        for (int x = 0; x < GameBoyCore::kWidth; ++x)
            impl->fb[line][x] = gb->cgb.fixPalette[pixels[x]];
    } else {
        for (int x = 0; x < GameBoyCore::kWidth; ++x)
            impl->fb[line][x] = kDmgPalette[((pixels[x] & 18) >> 1) | (pixels[x] & 3)];
    }
}

}  // namespace

// ── Lifecycle ───────────────────────────────────────────────────────────────

GameBoyCore::GameBoyCore() : _impl(new Impl()) {}
GameBoyCore::~GameBoyCore() { delete _impl; }

bool GameBoyCore::loadRom(const uint8_t* data, size_t size) {
    unload();
    if (!data || size < 0x150) return false;   // header must be present

    _impl->rom.assign(data, data + size);
    std::memset(&_impl->gb, 0, sizeof(_impl->gb));
    std::memset(_impl->fb, 0, sizeof(_impl->fb));

    const enum gb_init_error_e err = gb_init(&_impl->gb, romRead8, romRead16, romRead32,
                                            cartRamRead, cartRamWrite, onCoreError, _impl);
    _impl->initError = static_cast<int>(err);
    if (err != GB_INIT_NO_ERROR) {
        _impl->rom.clear();
        return false;
    }

    gb_init_lcd(&_impl->gb, lcdDrawLine);
    _impl->gb.direct.joypad = 0xFF;            // nothing pressed (active-low)

    _impl->saveSize = 0;
    if (gb_get_save_size_s(&_impl->gb, &_impl->saveSize) != 0) _impl->saveSize = 0;
    _impl->cartRam.assign(_impl->saveSize, 0);
    _impl->saveDirty = false;
    _impl->frames    = 0;

    _impl->title[0] = '\0';
    gb_get_rom_name(&_impl->gb, _impl->title);

    _impl->loaded = true;
    return true;
}

void GameBoyCore::unload() {
    if (!_impl) return;
    _impl->loaded = false;
    _impl->rom.clear();
    _impl->rom.shrink_to_fit();
    _impl->cartRam.clear();
    _impl->cartRam.shrink_to_fit();
    _impl->saveSize = 0;
    _impl->frames   = 0;
    _impl->title[0] = '\0';
    std::memset(&_impl->gb, 0, sizeof(_impl->gb));
    std::memset(_impl->fb, 0, sizeof(_impl->fb));
}

bool GameBoyCore::ready() const { return _impl && _impl->loaded; }

const char* GameBoyCore::title() const { return _impl ? _impl->title : ""; }

bool GameBoyCore::isCgb() const { return ready() && _impl->gb.cgb.cgbMode != 0; }

size_t GameBoyCore::saveSize() const { return _impl ? _impl->saveSize : 0; }

int GameBoyCore::lastInitError() const { return _impl ? _impl->initError : -1; }

const char* GameBoyCore::initErrorText(int code) {
    switch (static_cast<enum gb_init_error_e>(code)) {
        case GB_INIT_NO_ERROR:              return "ok";
        case GB_INIT_CARTRIDGE_UNSUPPORTED: return "unsupported cartridge";
        case GB_INIT_INVALID_CHECKSUM:      return "invalid header checksum";
        default:                            return "unknown error";
    }
}

// ── Execution / video ───────────────────────────────────────────────────────

void GameBoyCore::stepFrame() {
    if (!ready()) return;
    gb_run_frame_dualfetch(&_impl->gb);
    ++_impl->frames;
}

uint64_t GameBoyCore::framesRun() const { return _impl ? _impl->frames : 0; }

const uint16_t* GameBoyCore::framebuffer() const {
    return _impl ? &_impl->fb[0][0] : nullptr;
}

// ── Input ───────────────────────────────────────────────────────────────────

void GameBoyCore::setButton(GbButton button, bool pressed) {
    if (!ready()) return;
    if (button >= GbButton::COUNT) return;
    const uint8_t bit = kButtonBit[static_cast<size_t>(button)];
    /* Active-low: a set bit means released. */
    if (pressed) _impl->gb.direct.joypad &= static_cast<uint8_t>(~bit);
    else         _impl->gb.direct.joypad |= bit;
}

void GameBoyCore::releaseAllButtons() {
    if (!ready()) return;
    _impl->gb.direct.joypad = 0xFF;
}

// ── Save data ───────────────────────────────────────────────────────────────

const uint8_t* GameBoyCore::cartRam() const {
    return (_impl && !_impl->cartRam.empty()) ? _impl->cartRam.data() : nullptr;
}

uint8_t* GameBoyCore::cartRam() {
    return (_impl && !_impl->cartRam.empty()) ? _impl->cartRam.data() : nullptr;
}

bool GameBoyCore::loadCartRam(const uint8_t* data, size_t len) {
    if (!_impl) return false;
    if (len != _impl->saveSize) return false;          // never partially apply
    if (len == 0) return true;
    if (!data) return false;
    std::memcpy(_impl->cartRam.data(), data, len);
    _impl->saveDirty = false;
    return true;
}

bool GameBoyCore::saveDirty() const { return _impl && _impl->saveDirty; }

void GameBoyCore::clearSaveDirty() {
    if (_impl) _impl->saveDirty = false;
}

}  // namespace emulation
}  // namespace numos
