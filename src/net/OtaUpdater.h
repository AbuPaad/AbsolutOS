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
 * net/OtaUpdater.h — over-the-air update from GitHub Releases.
 *
 * Arduino-free by construction, exactly like net/Wifi.h and net/HttpStream.h:
 * this header names no Arduino, esp_ota, HTTP or NVS type, so ui/ and apps/
 * code can call it. The implementation (OtaUpdater.cpp) is the only place that
 * knows the framework exists, and the non-Arduino build is a named stub so the
 * emulator and the host tests still link.
 *
 * FLOW (owner's call, 2026-10-05): the device asks
 * https://api.github.com/repos/<repo>/releases/latest for the newest STABLE
 * release, compares its tag to the NUMOS_VERSION baked at build time, and — only
 * if the release is newer — downloads the single release asset into the spare
 * OTA bank with esp_ota and reboots. HTTPS is validated with the same
 * esp_crt_bundle as the AI path; the clock must be set (TLS checks notBefore /
 * notAfter), so net::Clock is a hard prerequisite, not a nicety.
 *
 * ENDPOINT (2026-10-07): both requests go to NUMOS_OTA_BASE
 * ("https://fw.absolutcas.com" by default; see OtaUpdater.cpp), which is a
 * Cloudflare Redirect Rule that 302s to the GitHub URLs above. The firmware
 * therefore depends on OUR hostname, not on GitHub's, so a release can be moved
 * or re-hosted without reflashing devices. Consequence: every request here must
 * keep followRedirects = true — an unfollowed 302 is reported as an HTTP status,
 * not as a redirect, which reads like a network fault.
 *
 * THREADING: a check or a download blocks on the socket for seconds to minutes.
 * Both therefore run on their OWN FreeRTOS task (the shape net/DeviceTransport
 * already uses), while this object holds only atomics + a spinlocked status POD.
 * The loop task calls tick() and state(); it never blocks on the network.
 */

#pragma once

#include <cstdint>
#include <string>

namespace net {

/// Where an update attempt has got to. Reported to the UI.
enum class OtaPhase : uint8_t {
    Idle = 0,
    Checking,
    UpToDate,
    Available,
    Downloading,
    Rebooting,
    Failed,
};

struct OtaState {
    OtaPhase    phase         = OtaPhase::Idle;
    std::string runningVersion;  ///< NUMOS_VERSION baked into this image
    std::string latestVersion;   ///< release tag, without a leading 'v'
    std::string error;           ///< named reason when phase == Failed
    uint32_t    bytesReceived = 0;
    uint32_t    bytesTotal    = 0;  ///< 0 when the server sent no Content-Length
};

class OtaUpdater {
public:
    /// Version baked at build time (scripts/build_metadata.py). Never null.
    static const char* runningVersion();

    /**
     * Query GitHub Releases for the newest stable release. Asynchronous; returns
     * false (no-op) while another check/download is already in flight or the
     * worker task could not be created. Inspect state()/tick() for the result.
     */
    static bool check();

    /**
     * Download + flash the release found by the last successful check().
     * Asynchronous. On success the device reboots into the new image.
     */
    static bool install();

    /// Ask an in-flight operation to stop. Idempotent. Safe from any task.
    static void cancel();

    /**
     * Pumped from the main loop with millis(). Drives the one-shot auto-check
     * and reclaims a finished worker. Must be called from the loop task.
     */
    static void tick(uint32_t nowMs);

    /// Coherent snapshot for the UI. Cheap; safe from the loop task.
    static OtaState state();

    /// True while a check or download is running.
    static bool busy();

    /**
     * Set when this build cannot reach the network at all (host/emulator), so
     * the UI can say why instead of showing a dead row. nullptr on device.
     */
    static const char* unavailableReason();

    /**
     * Enable/disable the automatic check. When on, tick() fires ONE check per
     * boot, a few seconds after Wi-Fi associates AND the clock syncs. The manual
     * Settings row calls check() regardless of this flag.
     */
    static void setAutoCheck(bool on);
};

}  // namespace net
