#include "ProductionDisplayRuntime.h"
#include "ProductionDisplayRuntimeConfig.h"

#if defined(ARDUINO)

#include <Preferences.h>
#include <esp_attr.h>
#include <esp_system.h>

#if NUMOS_PRODUCTION_DEMO_PROFILE
#include "../demo/DemoBootHealth.h"
#endif

namespace numos::display {

namespace {

constexpr const char* kPreferencesNamespace = "numosdisp";
constexpr const char* kProfileKey = "profile";
constexpr const char* kFailureCountKey = "failures";
constexpr uint32_t kRtcAttemptMagic = 0x44535041U; // "APSD"

struct DisplayRtcAttempt {
    uint32_t magic;
    uint32_t recordIdentity;
    uint32_t recordIdentityInverse;
};

RTC_NOINIT_ATTR DisplayRtcAttempt g_rtcAttempt;

ProductionDisplayProfile g_activeProfile = kSafeDisplayProfile;
ProfileLoadDecision g_loadDecision = ProfileLoadDecision::SafeNoRecord;
DisplayResetClass g_resetClass = DisplayResetClass::Unknown;
uint8_t g_failureCount = 0;
bool g_prepared = false;

void publishBusFrequencies() {
    numos_display_write_spi_hz = g_activeProfile.writeSpiHz;
    numos_display_read_spi_hz = g_activeProfile.readSpiHz;
}

DisplayResetClass classifyReset(const esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
        case ESP_RST_BROWNOUT:
            return DisplayResetClass::PowerInterruption;
        case ESP_RST_EXT:
        case ESP_RST_SW:
        case ESP_RST_DEEPSLEEP:
        case ESP_RST_SDIO:
            return DisplayResetClass::CleanReset;
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return DisplayResetClass::GenuineFailure;
        case ESP_RST_UNKNOWN:
            return DisplayResetClass::Unknown;
    }
    return DisplayResetClass::Unknown;
}

bool rtcAttemptMatches(const uint32_t recordIdentity) {
    return g_rtcAttempt.magic == kRtcAttemptMagic &&
           g_rtcAttempt.recordIdentity == recordIdentity &&
           g_rtcAttempt.recordIdentityInverse == ~recordIdentity;
}

void clearRtcAttempt() {
    g_rtcAttempt = {};
}

void armRtcAttempt(const uint32_t recordIdentity) {
    g_rtcAttempt = {
        kRtcAttemptMagic,
        recordIdentity,
        ~recordIdentity
    };
}

} // namespace

void prepareProductionDisplayBootProfile() {
    if (g_prepared) return;
    g_prepared = true;

    // Display fit (offsets/rotations) is intentionally ephemeral: the firmware
    // always boots from the hardcoded safe profile below. Runtime adjustments
    // made through the bring-up console or the Settings screen fit live only in
    // RAM for the current session, so a sudden power loss can never leave a
    // corrupt display profile in NVS that would white-screen the next boot.
    g_resetClass = classifyReset(esp_reset_reason());
    g_activeProfile = kSafeDisplayProfile;
    g_loadDecision = ProfileLoadDecision::SafeNoRecord;
    g_failureCount = 0;
    clearRtcAttempt();
    publishBusFrequencies();
}

const ProductionDisplayProfile& activeProductionDisplayProfile() {
    return g_activeProfile;
}

ProfileLoadDecision productionDisplayLoadDecision() {
    return g_loadDecision;
}

uint8_t productionDisplayFailureCount() {
    return g_failureCount;
}

DisplayResetClass productionDisplayResetClass() {
    return g_resetClass;
}

bool setActiveProductionDisplayProfile(
    const ProductionDisplayProfile& profile) {
    if (validateDisplayProfile(profile) != ProfileValidation::Ok) {
        return false;
    }
    g_activeProfile = profile;
    publishBusFrequencies();
    return true;
}

void restoreSafeProductionDisplayProfile() {
    g_activeProfile = kSafeDisplayProfile;
    publishBusFrequencies();
}

bool saveActiveProductionDisplayProfile() {
    // Display fit is ephemeral by design: never persist to NVS, so a power
    // loss cannot corrupt a stored profile. Runtime changes live in RAM only.
    return false;
}

void markProductionDisplayBootUsable() {
    clearRtcAttempt();
    if (g_loadDecision != ProfileLoadDecision::Saved ||
        g_failureCount == 0) {
        return;
    }
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) return;
    preferences.putUChar(kFailureCountKey, 0);
    preferences.end();
    g_failureCount = 0;
}

} // namespace numos::display

#elif !defined(ARDUINO)

namespace numos::display {

void prepareProductionDisplayBootProfile() {}
const ProductionDisplayProfile& activeProductionDisplayProfile() {
    return kSafeDisplayProfile;
}
ProfileLoadDecision productionDisplayLoadDecision() {
    return ProfileLoadDecision::SafeNoRecord;
}
uint8_t productionDisplayFailureCount() { return 0; }
DisplayResetClass productionDisplayResetClass() {
    return DisplayResetClass::Unknown;
}
bool setActiveProductionDisplayProfile(
    const ProductionDisplayProfile& profile) {
    return validateDisplayProfile(profile) == ProfileValidation::Ok;
}
void restoreSafeProductionDisplayProfile() {}
bool saveActiveProductionDisplayProfile() { return false; }
void markProductionDisplayBootUsable() {}

} // namespace numos::display

#endif
