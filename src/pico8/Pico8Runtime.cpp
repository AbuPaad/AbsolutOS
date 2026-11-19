/**
 * Pico8Runtime.cpp — boot and step one cart on fake-08.
 */

#include "Pico8Runtime.h"
#include "NumosHost.h"

#include "Audio.h"
#include "PicoRam.h"
#include "host.h"
#include "vm.h"
#include "nibblehelpers.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifndef ARDUINO
  #include <cstdio>
  #include "hal/FileSystem.h"
#else
  #include <LittleFS.h>
#endif

Pico8Runtime::~Pico8Runtime() { end(); }

bool Pico8Runtime::load(const std::string& path) {
    end();

    File f = LittleFS.open(path.c_str(), "r");
    if (!f) {
        std::snprintf(_error, sizeof(_error), "open failed");
        return false;
    }
    const size_t size = f.size();
    if (size == 0 || size > 0x10000) {   // a cart is ~32 KB; refuse nonsense
        std::snprintf(_error, sizeof(_error), "bad size %u", static_cast<unsigned>(size));
        return false;
    }

    std::vector<unsigned char> bytes(size);
    const size_t got = f.read(bytes.data(), size);
    if (got != size) {
        std::snprintf(_error, sizeof(_error), "short read");
        return false;
    }

    _host  = new Host(0, 0);
    _mem   = new PicoRam();
    static_cast<PicoRam*>(_mem)->Reset();
    _audio = new Audio(static_cast<PicoRam*>(_mem));
    _vm    = new Vm(_host, static_cast<PicoRam*>(_mem), nullptr, nullptr,
                    static_cast<Audio*>(_audio));

    Vm* vm = static_cast<Vm*>(_vm);
    _host->setUpPaletteColors();
    _host->setTargetFps(60);

    if (!vm->LoadCart(bytes.data(), bytes.size(), false)) {
        std::snprintf(_error, sizeof(_error), "LoadCart failed");
        end();
        return false;
    }

    vm->vm_run();      // boot the Lua side once; Step() drives _update/_draw
    _booted = true;
    _ready = true;
    _frames = 0;
    std::printf("[P8] loaded %s (%u bytes)\n", path.c_str(), static_cast<unsigned>(size));
    return true;
}

void Pico8Runtime::step() {
    if (!_ready) return;
    Vm* vm = static_cast<Vm*>(_vm);
    vm->Step();
    uint8_t* fb = vm->GetPicoInteralFb();
    _host->drawFrame(fb, vm->GetScreenPaletteMap(), 0);
    ++_frames;
}

void Pico8Runtime::setButtons(uint8_t held) { numos_pico8::setHeldButtons(held); }

void Pico8Runtime::end() {
    if (_vm) {
        static_cast<Vm*>(_vm)->CloseCart();
        delete static_cast<Vm*>(_vm);
        _vm = nullptr;
    }
    // Vm::~Vm() frees the Audio and PicoRam it was handed whenever it had to
    // create any dependency itself (it sets one _cleanupDeps flag for all four).
    // Deleting them here too was the double free that crashed mid-session
    // teardown; the host is ours and only the host is ours.
    _audio = nullptr;
    _mem = nullptr;
    if (_host)  { delete _host; _host = nullptr; }
    _ready = false;
    _booted = false;
    _frames = 0;
}
