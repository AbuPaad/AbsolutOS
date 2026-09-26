/*
 * Host unit test for GameBoyCore — runs the emulator core with NO LVGL, NO SDL
 * and NO emulator harness involved.
 *
 * Build (from the repository root):
 *   g++ -std=gnu++17 -O2 -Wall -Wextra -I src -I lib/WalnutCGB \
 *       -DWALNUT_GB_16BIT_DMA=0 -DWALNUT_GB_32BIT_DMA=0 \
 *       tests/host/gb_core_test.cpp src/emulation/GameBoyCore.cpp -o gb_core_test
 *   ./gb_core_test tests/emulator/fs/roms/dmg-acid2.gb
 *
 * Follows the existing host-test precedent (tests/host/keycode_digit_test.cpp):
 * cheap, no hardware, no simulator, exits non-zero on failure.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "emulation/GameBoyCore.h"

using numos::emulation::GameBoyCore;
using numos::emulation::GbButton;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " PASS" : " FAIL", what);
    if (!ok) ++g_failures;
}

bool loadFile(const char* path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

uint64_t hashFramebuffer(const GameBoyCore& core) {
    uint64_t h = 1469598103934665603ULL;   // FNV-1a 64
    const uint16_t* fb = core.framebuffer();
    const size_t words = static_cast<size_t>(GameBoyCore::kWidth) * GameBoyCore::kHeight;
    for (size_t i = 0; i < words; ++i) {
        h ^= fb[i];
        h *= 1099511628211ULL;
    }
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    const char* romPath = (argc > 1) ? argv[1] : "tests/emulator/fs/roms/dmg-acid2.gb";

    std::vector<uint8_t> rom;
    if (!loadFile(romPath, rom)) {
        std::printf("[ FAIL] cannot read ROM: %s\n", romPath);
        return 1;
    }
    std::printf("[INFO] ROM %s (%zu bytes)\n", romPath, rom.size());

    GameBoyCore core;
    check(!core.ready(), "a fresh core is not ready");

    // A too-short image must be refused (no header).
    const uint8_t tiny[8] = {0};
    check(!core.loadRom(tiny, sizeof(tiny)), "loadRom refuses an image without a header");

    check(core.loadRom(rom.data(), rom.size()), "loadRom accepts a real ROM");
    check(core.ready(), "core reports ready after loadRom");
    std::printf("[INFO] title='%s' cgb=%d saveSize=%zu initError=%d\n",
                core.title(), core.isCgb() ? 1 : 0, core.saveSize(), core.lastInitError());
    check(std::strncmp(core.title(), "DMG-ACID2", 9) == 0, "ROM header title reads back");
    check(!core.isCgb(), "dmg-acid2 is a DMG (non-CGB) cartridge");

    check(core.framesRun() == 0, "frame counter starts at zero");
    for (int i = 0; i < 120; ++i) core.stepFrame();
    check(core.framesRun() == 120, "frame counter tracks stepFrame() calls");
    const uint64_t hashA = hashFramebuffer(core);

    // Buttons must be accepted in every state and never crash.
    core.setButton(GbButton::A, true);
    core.setButton(GbButton::Up, true);
    core.stepFrame();
    core.setButton(GbButton::A, false);
    core.setButton(GbButton::Up, false);
    core.releaseAllButtons();
    check(core.framesRun() == 121, "stepping with buttons held works");

    // Determinism: the same ROM and frame count must produce the same picture.
    GameBoyCore second;
    check(second.loadRom(rom.data(), rom.size()), "second core loads the same ROM");
    for (int i = 0; i < 120; ++i) second.stepFrame();
    const uint64_t hashB = hashFramebuffer(second);
    std::printf("[INFO] fb hash A=%016llx B=%016llx\n",
                static_cast<unsigned long long>(hashA),
                static_cast<unsigned long long>(hashB));
    check(hashA == hashB, "two cores on the same ROM are frame-deterministic");

    // Save data: a cartridge with no RAM must refuse a non-empty save.
    check(core.saveSize() == 0, "dmg-acid2 declares no cartridge RAM");
    const uint8_t junk[4] = {1, 2, 3, 4};
    check(!core.loadCartRam(junk, sizeof(junk)), "a mismatched save length is refused");
    check(core.loadCartRam(nullptr, 0), "a zero-length save is accepted as a no-op");
    check(core.cartRam() == nullptr, "cartRam() is null when the cartridge has none");

    core.unload();
    check(!core.ready(), "unload() clears the ready state");

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
