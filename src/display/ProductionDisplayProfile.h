#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace numos::display {

inline constexpr uint32_t kMinimumSpiHz = 1'000'000U;
inline constexpr uint32_t kValidatedMaximumSpiHz = 40'000'000U;
#if defined(NUMOS_PRODUCTION_BRINGUP_SPI_EXPERIMENT_MAX_HZ)
inline constexpr uint32_t kMaximumSpiHz =
    NUMOS_PRODUCTION_BRINGUP_SPI_EXPERIMENT_MAX_HZ;
static_assert(kMaximumSpiHz > kValidatedMaximumSpiHz &&
              kMaximumSpiHz <= 80'000'000U,
              "Bring-up SPI experiment must stay within 40..80 MHz");
#else
inline constexpr uint32_t kMaximumSpiHz = kValidatedMaximumSpiHz;
#endif
// Offsets are the flush shift that positions the logical canvas on the physical
// panel.  The fitted canvas sits LOW in the fx-82 cut-out (measured top bar
// 54 px, bottom bar 30 px), so the positive direction needs much more room than
// the negative one.  The physical limit for a 156-row canvas is +84 (240-156);
// the cap is 64 — enough headroom to tune the measured value, still a hard stop
// against a nonsensical shift.
inline constexpr int16_t kMinimumOffset = -32;
inline constexpr int16_t kMaximumOffset = 64;
// Settings never offers a black-screen value. The existing SAFE low level is
// retained as the recovery value for legacy/corrupt records that contain zero.
inline constexpr uint8_t kMinimumPersistedBacklight = 1;
inline constexpr uint8_t kZeroBrightnessFallbackBacklight = 32;
inline constexpr uint8_t kMaximumBacklight = 192;
inline constexpr uint8_t kMadctlBgr = 0x08;
// Landscape axis bits for THIS glass, taken from the bench ILI9341 bring-up:
// MADCTL is the vendor's 0x08 (BGR) base with only MY/MX/MV toggled.  This does
// NOT match TFT_eSPI's built-in ILI9341 rotation table, whose rotation 1 = 0x20
// omits MX and mirrors the image horizontally on this panel.
inline constexpr uint8_t kMadctlRotation1 = 0x60;  // MX | MV
inline constexpr uint8_t kMadctlRotation3 = 0xA0;  // MY | MV
// Physical glass (rotation 1): exactly what TFT_eSPI's own rotation table
// reports, and what the controller's frame memory holds.
inline constexpr uint16_t kPanelWidth = 320;
inline constexpr uint16_t kPanelHeight = 240;

// Logical canvas LVGL is created at.  The fx-82 shell exposes only the cut-out
// area of the panel's 240 rows, so the canvas IS the usable area and the flush
// is offset by the bar height instead of the bars being drawn in app code;
// apps lay out inside 320x156 and never see the letterbox.
// MEASURED (fit rig, 2026-10-02): 320 px wide x 156 px tall, top bar 54 px.
// KEEP IN STEP with kProductionBoard.display.logicalWidth/Height (BoardProfile.h),
// SCREEN_WIDTH/HEIGHT (Config.h) and SCREEN_W/H (hal/NativeHal.cpp); the
// static_assert in DisplayDriver.cpp fails the build if they drift.
inline constexpr uint16_t kLogicalDisplayWidth = 320;
inline constexpr uint16_t kLogicalDisplayHeight = 156;
// Top flush offset.  This is a MEASURED constant, NOT the centring formula:
// the cut-out is not vertically centred on the 240-row panel.  (240-156)/2 = 42
// would be wrong by 12 px; the rig measured 54.  A centring offset can never be
// used for a canvas below 176 rows anyway — it would exceed even the widened cap.
inline constexpr int16_t kLogicalDisplayOffsetY = 54;
static_assert(kLogicalDisplayOffsetY >= kMinimumOffset &&
                  kLogicalDisplayOffsetY <= kMaximumOffset,
              "Centring offset must fit the profile offset bounds");
// A 156-row canvas plus its 54 px top offset must still land inside the panel.
static_assert(kLogicalDisplayOffsetY + kLogicalDisplayHeight <= kPanelHeight,
              "Canvas plus offset must fit inside the physical panel");

enum class ProfileId : uint8_t {
    Safe = 0,
    Rotate3Bgr = 1,
    Rotate1Rgb = 2,
    Rotate1BgrInverted = 3,
    Custom = 0x7F
};

enum class ColorOrder : uint8_t {
    Rgb = 0,
    Bgr = 1
};

struct ProductionDisplayProfile {
    ProfileId identifier;
    uint8_t rotation;
    ColorOrder colorOrder;
    bool inverted;
    int16_t xOffset;
    int16_t yOffset;
    uint32_t writeSpiHz;
    uint32_t readSpiHz;
    uint16_t resetLowMs;
    uint16_t resetRecoveryMs;
    uint8_t initialBacklight;
    uint8_t maximumBacklight;
};

inline constexpr ProductionDisplayProfile kSafeDisplayProfile = {
    ProfileId::Safe,
    1,
    ColorOrder::Bgr,
    false,
    0,
    kLogicalDisplayOffsetY,
    40'000'000U,
    10'000'000U,
    10,
    120,
    96,
    192
};

static_assert(kMinimumPersistedBacklight > 0);
static_assert(kMinimumPersistedBacklight <=
              kSafeDisplayProfile.initialBacklight);
static_assert(kMinimumPersistedBacklight <=
              kZeroBrightnessFallbackBacklight);
static_assert(kZeroBrightnessFallbackBacklight <=
              kSafeDisplayProfile.initialBacklight);
static_assert(kSafeDisplayProfile.initialBacklight <= kMaximumBacklight);

inline constexpr std::array<ProductionDisplayProfile, 4>
    kProductionDisplayPresets = {{
        kSafeDisplayProfile,
        {
            ProfileId::Rotate3Bgr, 3, ColorOrder::Bgr, false,
            0, kLogicalDisplayOffsetY, 40'000'000U, 10'000'000U, 10, 120, 96, 192
        },
        {
            ProfileId::Rotate1Rgb, 1, ColorOrder::Rgb, false,
            0, kLogicalDisplayOffsetY, 40'000'000U, 10'000'000U, 10, 120, 96, 192
        },
        {
            ProfileId::Rotate1BgrInverted, 1, ColorOrder::Bgr, true,
            0, kLogicalDisplayOffsetY, 40'000'000U, 10'000'000U, 10, 120, 96, 192
        }
    }};

struct DisplayGeometry {
    uint16_t width;
    uint16_t height;
};

// Physical panel geometry for a rotation - NOT the logical canvas size.  The
// caller compares this against TFT_eSPI's rotation-derived width()/height(),
// which is always the 320x240 frame memory, so the logical 320x180 must never
// be used here.
constexpr DisplayGeometry panelDisplayGeometry(const uint8_t rotation) {
    return (rotation == 1 || rotation == 3)
        ? DisplayGeometry{kPanelWidth, kPanelHeight}
        : DisplayGeometry{0, 0};
}

constexpr uint8_t displayMadctl(const uint8_t rotation,
                                const ColorOrder colorOrder) {
    const uint8_t axisBits =
        rotation == 1 ? kMadctlRotation1 :
        rotation == 3 ? kMadctlRotation3 : 0;
    return static_cast<uint8_t>(
        axisBits | (colorOrder == ColorOrder::Bgr ? kMadctlBgr : 0));
}

constexpr uint8_t displayMadctl(const ProductionDisplayProfile& profile) {
    return displayMadctl(profile.rotation, profile.colorOrder);
}

constexpr bool usesUnvalidatedSpiRate(
    const ProductionDisplayProfile& profile) {
    return profile.writeSpiHz > kValidatedMaximumSpiHz ||
           profile.readSpiHz > kValidatedMaximumSpiHz;
}

bool decodeSupportedMadctl(uint8_t madctl, uint8_t& rotation,
                           ColorOrder& colorOrder);

enum class ProfileValidation : uint8_t {
    Ok,
    UnknownIdentifier,
    UnsupportedRotation,
    InvalidColorOrder,
    OffsetOutOfRange,
    SpiOutOfRange,
    SpiNotWholeMHz,
    ResetTimingOutOfRange,
    BacklightOutOfRange,
    PresetModified
};

const char* profileIdentifier(ProfileId identifier);
const ProductionDisplayProfile* findPreset(ProfileId identifier);
bool parseProfileIdentifier(const char* text, std::size_t length,
                            ProfileId& identifier);
ProfileValidation validateDisplayProfile(
    const ProductionDisplayProfile& profile);
const char* profileValidationName(ProfileValidation validation);
bool profilesEqual(const ProductionDisplayProfile& left,
                   const ProductionDisplayProfile& right);
void markProfileCustom(ProductionDisplayProfile& profile);

inline constexpr uint32_t kDisplayRecordMagic = 0x3250444EU; // "NDP2" LE
inline constexpr uint16_t kDisplayRecordVersion = 2;
inline constexpr uint32_t kDisplayProfileSchemaTag = 0x5A320002U;
inline constexpr std::size_t kDisplayRecordSize = 48;

struct DisplayProfileRecord {
    std::array<uint8_t, kDisplayRecordSize> bytes{};
};

uint32_t displayRecordChecksum(const uint8_t* data, std::size_t length);
DisplayProfileRecord encodeDisplayProfileRecord(
    const ProductionDisplayProfile& profile);
bool decodeDisplayProfileRecord(const DisplayProfileRecord& record,
                                ProductionDisplayProfile& profile);

enum class ProfileLoadDecision : uint8_t {
    Saved,
    SafeNoRecord,
    SafeInvalidRecord,
    SafeRollback
};

ProfileLoadDecision resolveDisplayProfileRecord(
    const DisplayProfileRecord* record,
    bool recordPresent,
    ProductionDisplayProfile& profile);
const char* profileLoadDecisionName(ProfileLoadDecision decision);

enum class DisplayCommandKind : uint8_t {
    Help,
    Info,
    Test,
    ProfileList,
    ProfileSet,
    Rotate,
    Bgr,
    Invert,
    Offset,
    Spi,
    Backlight,
    Save,
    Reset,
    Safe
};

struct DisplayCommand {
    DisplayCommandKind kind = DisplayCommandKind::Help;
    ProfileId profile = ProfileId::Safe;
    int32_t first = 0;
    int32_t second = 0;
    bool enabled = false;
};

enum class CommandParseResult : uint8_t {
    Ok,
    NotDisplayCommand,
    TooLong,
    TooManyTokens,
    MissingArgument,
    UnexpectedArgument,
    UnknownCommand,
    UnsupportedValue,
    InvalidInteger
};

inline constexpr std::size_t kMaximumDisplayCommandLength = 79;

CommandParseResult parseDisplayCommand(const char* text, std::size_t length,
                                       DisplayCommand& command);
const char* commandParseResultName(CommandParseResult result);

} // namespace numos::display
