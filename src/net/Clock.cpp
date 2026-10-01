/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/** net/Clock.cpp — the Arduino half of net/Clock.h. */

#include "net/Clock.h"

#if defined(ARDUINO)

#include <Arduino.h>
#include <time.h>

#include <cstdint>

namespace net {
namespace {
constexpr long      kPlausibleEpoch = 1700000000L;   // 2023-11-14
constexpr uint32_t  kSyncTimeoutMs  = 10000;
}  // namespace

bool timeSynced() {
    return (long)time(nullptr) > kPlausibleEpoch;
}

bool syncTime(const char* tz, const char* server) {
    configTzTime(tz ? tz : "AEST-10", server ? server : "pool.ntp.org");

    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < kSyncTimeoutMs) {
        if (timeSynced()) return true;
        delay(50);
    }
    return timeSynced();
}

}  // namespace net

#else

namespace net {
bool timeSynced() { return false; }
bool syncTime(const char*, const char*) { return false; }
}  // namespace net

#endif  // ARDUINO
