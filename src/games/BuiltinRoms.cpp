/**
 * BuiltinRoms — copy the embedded game images into LittleFS on first use.
 *
 * The firmware carries Celeste (.p8.png) and Tetris (.gb) in the app image
 * (src/games/BuiltinRoms.generated.h). GamesApp still scans /roms and the
 * loaders still read LittleFS files, so the only thing this adds is the write:
 * if the file is not there, create it from flash. Saves are written beside the
 * rom as before, and a unit that already has the file is left untouched — an
 * uploaded newer cart is never overwritten.
 *
 * Part of: NumOS — Games
 */

#include "BuiltinRoms.h"

#ifdef ARDUINO

#include "BuiltinRoms.generated.h"

#include <LittleFS.h>
#include <Arduino.h>

namespace numos::games {
namespace {

struct BuiltinRom {
    const char*          path;
    const unsigned char* data;
    size_t               bytes;
};

constexpr BuiltinRom kBuiltinRoms[] = {
    {builtin::kCelestePath, builtin::kCelesteImage, builtin::kCelesteBytes},
    {builtin::kTetrisPath,  builtin::kTetrisImage,  builtin::kTetrisBytes},
};

/// True when the file was written by this call.
bool ensureRom(const BuiltinRom& rom) {
    if (LittleFS.exists(rom.path)) return false;

    fs::File file = LittleFS.open(rom.path, "w");
    if (!file) {
        Serial.printf("[GAMES] builtin rom: cannot open %s for write\n", rom.path);
        return false;
    }
    const size_t written = file.write(rom.data, rom.bytes);
    file.close();
    if (written != rom.bytes) {
        // A short write leaves a truncated cart; drop it so the next load retries
        // instead of booting a corrupt rom.
        LittleFS.remove(rom.path);
        Serial.printf("[GAMES] builtin rom: short write %s (%u/%u bytes)\n",
                      rom.path, static_cast<unsigned>(written),
                      static_cast<unsigned>(rom.bytes));
        return false;
    }
    Serial.printf("[GAMES] builtin rom: wrote %s (%u bytes)\n",
                  rom.path, static_cast<unsigned>(rom.bytes));
    return true;
}

}  // namespace

void materializeBuiltinRoms() {
    // The FS mount happens before any app loads; once per boot is enough, and a
    // static guard keeps a repeated Games open from walking LittleFS again.
    static bool done = false;
    if (done) return;
    done = true;

    // Writing "/roms/pico8/x" needs both directories to exist.
    (void)LittleFS.mkdir("/roms");
    (void)LittleFS.mkdir("/roms/pico8");
    (void)LittleFS.mkdir("/roms/gb");

    size_t added = 0;
    for (const BuiltinRom& rom : kBuiltinRoms) {
        if (ensureRom(rom)) ++added;
    }
    Serial.printf("[GAMES] builtin roms: %u written, %u already present\n",
                  static_cast<unsigned>(added),
                  static_cast<unsigned>(sizeof(kBuiltinRoms) / sizeof(kBuiltinRoms[0]) - added));
}

}  // namespace numos::games

#else

namespace numos::games {
void materializeBuiltinRoms() {}
}  // namespace numos::games

#endif
