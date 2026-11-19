/**
 * Pico8Runtime.h — one running PICO-8 cart on the vendored fake-08 VM.
 *
 * Ownership: PicoRam (64 KiB), Audio (constructed, never filled), Host, Vm.
 * The frontend drives it: load() → step() per loop → numos_pico8::frame565().
 */

#pragma once

#include <cstdint>
#include <string>

class Host;

class Pico8Runtime {
public:
    ~Pico8Runtime();

    /// Read the cart from the filesystem and boot it. false = could not load.
    bool load(const std::string& path);

    /// One emulated frame. Cheap no-op when not ready.
    void step();

    /// Held button bits (fake-08 P8_KEY_* masks).
    void setButtons(uint8_t held);

    bool ready() const { return _ready; }
    const char* error() const { return _error[0] ? _error : nullptr; }
    uint32_t frames() const { return _frames; }
    void end();

private:
    Host*    _host = nullptr;
    void*    _mem = nullptr;    ///< PicoRam*
    void*    _vm = nullptr;     ///< Vm*
    void*    _audio = nullptr;  ///< Audio*
    bool     _ready = false;
    bool     _booted = false;
    uint32_t _frames = 0;
    char     _error[96] = {0};
};
