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
 * net/Portal.h — the provisioning + file web portal.
 *
 * WHAT IT IS: the device raises its own WPA2 access point and serves a
 * self-contained web page at http://192.168.4.1 — no internet, no CDN, no
 * external asset. From a phone the user can (a) browse every file in LittleFS
 * (the whole root: /notes, /ai, /roms, …), (b) render + edit Markdown in the
 * browser, and (c) type the things that can never be compiled in: the provider
 * API key, the model, the base URL, and the Wi-Fi credentials.
 *
 * WHY IT EXISTS: on a blank unit there is no other way in. net/Wifi::begin()
 * refuses to touch the radio without stored credentials, and AiConfig resolves
 * its key from NVS -> /ai/config.json -> compiled defaults, where the compiled
 * default is "no key". Without this, the AI app fails with "Wi-Fi not
 * connected" and can never be fixed in the field.
 *
 * Arduino-free by construction, exactly like net/Wifi.h: this header names no
 * Arduino, WiFi, HTTP or NVS type, so ui/ and apps/ code can call it. The
 * implementation (Portal.cpp) is the only place that knows the framework
 * exists, and the non-Arduino build is a named stub so the emulator and the
 * host tests still link.
 *
 * LIFETIME: the owner raises it explicitly from Settings and it is meant to be
 * transient — the AP + server are stopped when the user turns it off, when the
 * Settings screen is left, or after kIdleStopMs with no request (so a forgotten
 * portal cannot sit there burning the battery). tick() drives that idle rule
 * and must be called from the main loop.
 *
 * SECURITY POSTURE (owner's call, 2026-10-01): no web login. The AP itself is
 * the gate — a per-unit WPA2 password derived from the chip id, shown on the
 * calculator screen, and the portal only runs while the user holds it open.
 * The key is never echoed back to the page (the config endpoint reports
 * "set"/"not set" only) and never logged. Note honestly: the API key travels
 * over plain HTTP *inside* the WPA2 link, so it is protected from the air but
 * not from anything already on that AP.
 */

#pragma once

#include <cstdint>
#include <string>

namespace net {

/**
 * Where the portal is in its start-up. The AP is not raised until the pre-scan
 * (and any auto-join) is done, so "not running" is no longer the same as "off".
 */
enum class PortalPhase : uint8_t {
    Off,         ///< AP down, nothing pending
    Preparing,   ///< scanning saved networks before deciding to raise the AP
    Joining,     ///< trying a reachable saved network; AP only if it fails
    Running,     ///< the provisioning AP is up and serving
    Failed,      ///< start() was refused; lastError says why
};

struct PortalState {
    bool        running  = false;
    PortalPhase phase    = PortalPhase::Off;
    /// Human status while Preparing/Joining ("scanning", "joining HomeWiFi").
    std::string activity;
    /// Networks seen by the last scan (the pre-scan's found-count).
    int         scannedNetworks = 0;
    std::string url;         ///< "http://192.168.4.1" while running
    std::string apSsid;      ///< per-unit AP name
    std::string apPass;      ///< per-unit WPA2 password (shown on screen)
    int         stations = 0;///< phones/joints currently on the AP
    uint32_t    requests = 0;///< requests served since start(), cheap liveness
    std::string lastError;   ///< named reason when running == false

    // STA side, so a UI can show whether the calculator actually joined the
    // user's network after provisioning (the other half of "why is the AI app
    // not answering").
    bool        staConnected = false;
    std::string staSsid;
    std::string staIp;
};

class Portal {
public:
    /// Raise the AP, start the file/config server and the captive DNS.
    static bool start();

    /// Tear the portal down. Honours Wi-Fi intent: if credentials are stored,
    /// the STA joins as the AP goes away.
    static void stop();

    static bool running();
    static PortalState state();

    /// Idle auto-stop + housekeeping. Call from the main loop with millis().
    static void tick(uint32_t nowMs);

    /// Set when this build cannot host a portal at all (host/emulator), so the
    /// UI can say why instead of showing a dead toggle.
    static const char* unavailableReason();

    /// How long the AP stays up with no request before stopping itself.
    static constexpr uint32_t kIdleStopMs = 15u * 60u * 1000u;
};

}  // namespace net
