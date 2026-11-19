/**
 * NumosHost.cpp — NumOS implementation of fake-08's `Host` platform half.
 *
 * Scope: enough host for a cart to run with no audio.
 *   - carts are read by the caller (Pico8Runtime) and handed to Vm::LoadCart,
 *     so the Host file APIs stay at the vendored defaults where possible
 *   - audio is a stub: sfx()/music() run their state machine, no samples leave
 *   - drawFrame() converts palette indices to RGB565 into a static buffer
 */

#include "NumosHost.h"

#include "host.h"
#include "hostVmShared.h"
#include "nibblehelpers.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string g_logPrefix  = "./emulator_data/";
std::string g_cartDir    = "./emulator_data/roms/pico8/";
std::string g_biosLua    = "";
uint8_t     g_held       = 0;
uint8_t     g_prev       = 0;
int         g_targetFps  = 60;
bool        g_haveFrame  = false;
Audio*      g_audio      = nullptr;

uint16_t    g_frame565[numos_pico8::kFrameW * numos_pico8::kFrameH];
uint16_t    g_lut565[256];
bool        g_lutReady = false;

inline uint16_t to565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace

namespace numos_pico8 {

void setHeldButtons(uint8_t held) { g_held = held; }
const uint16_t* frame565() { return g_frame565; }
bool hasFrame() { return g_haveFrame; }

}  // namespace numos_pico8

// ── construction / platform params ─────────────────────────────────────────

Host::Host(int windowWidth, int windowHeight) {
    currKDown = 0;
    currKHeld = 0;
    currKBDown = false;
    lDown = false;
    rDown = false;
    stretchKeyPressed = false;
    stretch = StretchOption::PixelPerfectStretch;
    quit = 0;
    _logFilePrefix = g_logPrefix;
    _cartDirectory = g_cartDir;
    _customBiosLua = g_biosLua;
    loadSettingsIni();
    (void)windowWidth;
    (void)windowHeight;
}

void Host::setPlatformParams(int windowWidth, int windowHeight,
                             uint32_t sdlWindowFlags, uint32_t sdlRendererFlags,
                             uint32_t sdlPixelFormat, std::string logFilePrefix,
                             std::string customBiosLua, std::string cartDirectory) {
    (void)windowWidth; (void)windowHeight; (void)sdlWindowFlags;
    (void)sdlRendererFlags; (void)sdlPixelFormat;
    _logFilePrefix = logFilePrefix;
    _customBiosLua = customBiosLua;
    _cartDirectory = cartDirectory;
    g_logPrefix = logFilePrefix;
    g_cartDir = cartDirectory;
}

void Host::oneTimeSetup(Audio* audio) {
    g_audio = audio;
    setUpPaletteColors();
}

void Host::oneTimeCleanup() { g_audio = nullptr; }

// ── pacing ────────────────────────────────────────────────────────────────

void Host::setTargetFps(int targetFps) { g_targetFps = targetFps; }
void Host::waitForTargetFps() {}
bool Host::shouldRunMainLoop() { return false; }   // we drive Vm::Step ourselves
double Host::deltaTMs() { return (g_targetFps > 0) ? (1000.0 / g_targetFps) : 16.6; }

// ── stretch / display settings (no-ops: the canvas is ours) ───────────────

void Host::changeStretch() {}
void Host::forceStretch(StretchOption newStretch) { stretch = newStretch; }
bool Host::shouldQuit() { return quit != 0; }

// ── input ─────────────────────────────────────────────────────────────────

InputState_t Host::scanInput() {
    InputState_t s;
    const uint8_t held = g_held;
    s.KDown = static_cast<uint8_t>(held & ~g_prev);   // newly pressed this frame
    s.KHeld = held;
    g_prev = held;
    s.mouseX = 0;
    s.mouseY = 0;
    s.mouseBtnState = 0;
    s.KBdown = false;
    s.KBkey.clear();
    return s;
}

// ── frame ─────────────────────────────────────────────────────────────────

void Host::drawFrame(uint8_t* picoFb, uint8_t* screenPaletteMap, uint8_t drawMode) {
    (void)drawMode;
    if (!picoFb || !screenPaletteMap) return;

    if (!g_lutReady) {
        // Palette index -> RGB565, built from the active 144-colour table.
        const Color* pal = GetPaletteColors();
        for (int i = 0; i < 256; ++i) {
            const Color& c = pal[i & 0x8F];   // 144 valid entries (0..143)
            g_lut565[i] = to565(c.Red, c.Green, c.Blue);
        }
        g_lutReady = true;
    }

    // The frame buffer is nibble-packed (two pixels per byte), and a mapped
    // colour is masked with 0x8F exactly as the upstream SDL host does.
    for (int y = 0; y < numos_pico8::kFrameH; ++y) {
        for (int x = 0; x < numos_pico8::kFrameW; ++x) {
            const uint8_t c = getPixelNibble(x, y, picoFb);
            g_frame565[y * numos_pico8::kFrameW + x] =
                g_lut565[screenPaletteMap[c] & 0x8F];
        }
    }
    g_haveFrame = true;
}

// ── audio (silent) ────────────────────────────────────────────────────────

bool Host::shouldFillAudioBuff() { return false; }
void* Host::getAudioBufferPointer() { return nullptr; }
size_t Host::getAudioBufferSize() { return 0; }
void Host::playFilledAudioBuffer() {}

// ── filesystem surface the VM still asks for ──────────────────────────────

std::vector<std::string> Host::listcarts() {
    // Pico8Runtime owns cart discovery (/roms scan); keep this empty so the VM
    // never re-lists behind our back.
    return std::vector<std::string>();
}

std::vector<std::string> Host::listdirs() { return std::vector<std::string>(); }

const char* Host::logFilePrefix() { return _logFilePrefix.c_str(); }
void Host::overrideLogFilePrefix(const char* newPrefix) {
    _logFilePrefix = (newPrefix ? newPrefix : "");
}
std::string Host::customBiosLua() { return _customBiosLua; }
std::string Host::getCartDirectory() { return _cartDirectory; }
