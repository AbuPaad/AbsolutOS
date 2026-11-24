#pragma once

namespace numos::games {

/**
 * Write the game images embedded in the app binary (Celeste, Tetris) into
 * LittleFS if they are not already there. No-op on host builds, which read
 * ./emulator_data/roms instead.
 *
 * Call before the /roms scan.
 */
void materializeBuiltinRoms();

}  // namespace numos::games
