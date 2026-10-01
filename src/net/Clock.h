/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * net/Clock.h — SNTP + POSIX TZ. A hard prerequisite for TLS, not a nicety
 * (device-networking.md §1.2): mbedTLS validates notBefore/notAfter, so with the
 * RTC still at the 1970 epoch every handshake fails as "certificate expired or
 * future" and the failure reads like a cert-bundle problem.
 */

#pragma once

namespace net {

/// True once the RTC is plausibly real (> ~2023-11-14).
bool timeSynced();

/**
 * configTzTime + a bounded wait. The POSIX TZ string beats a GMT-offset integer
 * because it survives DST. Returns false (never blocks forever) when NTP is
 * unreachable — the caller must then say "clock not set" rather than surface a
 * TLS error.
 */
bool syncTime(const char* tz = "AEST-10", const char* server = "pool.ntp.org");

}  // namespace net
