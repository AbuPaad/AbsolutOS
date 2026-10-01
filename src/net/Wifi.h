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
 * net/Wifi.h — Wi-Fi STA service for NumOS.
 *
 * Arduino-free by construction: it names no Arduino, WiFi, or NVS type, so the
 * implementation lives entirely in Wifi.cpp (the same split hal/FileSystem.h
 * uses). Nothing above this header needs to know the radio exists.
 *
 * LIFETIME (device-networking.md §1.1): the radio is a SYSTEM service owned by
 * main.cpp, never by an app. AiApp comes and goes on every launcher
 * round-trip; the link and its TLS state must survive that. So: begin() once at
 * the END of setup() (after the display, LVGL, and the DMA draw buffer), tick()
 * from loop(), and a Settings toggle that is persistent intent, not a live call.
 *
 * THREADING: the event callbacks this registers run on their OWN FreeRTOS task.
 * Every flag they touch is a plain atomic scalar — never a std::string, never an
 * app object, never LVGL.
 */

#pragma once

#include <cstdint>
#include <string>

namespace net {

struct WifiState {
    bool        connected        = false;
    int         rssi             = 0;   ///< dBm, valid only while connected
    std::string ip;
    std::string ssid;
    int         disconnectReason = 0;   ///< wifi_err_reason_t from the last disconnect
};

class Wifi {
public:
    /**
     * Start the radio and associate, if intent says so.
     * Returns false WITHOUT touching the driver when the toggle is off or no
     * credentials are stored — a user who never provisions never pays for the
     * radio (no esp_wifi_init, no RAM, no idle current).
     */
    static bool begin();

    /// Explicit one-shot join (the portal's "test before save" uses this).
    static bool connect(const std::string& ssid, const std::string& pass, int timeoutMs);

    /**
     * Real teardown, in the documented order: retry timer → disconnect(eraseap)
     * → WIFI_OFF. `WIFI_OFF` is what actually powers the radio down and returns
     * ~30-60 KB of internal RAM; disconnect() alone leaves the driver running.
     */
    static void disconnect(bool eraseCreds);

    /// Snapshot for UI/diagnostics. Safe to call from any task.
    static WifiState state();

    /// false = modem sleep ON (the power-saving default).
    static void setPowerSave(bool on);

    /**
     * Coalesced retry with backoff, driven from loop(). Never call this from an
     * event callback: at most one automatic retry per user action is the rule
     * (§15 — no retry storms).
     */
    static void tick(uint32_t nowMs);

    /// AP+STA so the provisioning access point exists while STA keeps trying.
    static bool startProvisioningAp(const char* ssid, const char* pass);
    static void stopProvisioningAp();

    // ── Credentials and intent, in our own NVS namespace ──────────────────
    // We keep the network list ourselves rather than leaning on the driver's
    // one-shot store: it gives priority ordering and "forget", and lets
    // WiFi.persistent(false) stop the driver writing flash on every connect.
    static bool saveCredentials(const std::string& ssid, const std::string& pass);
    static bool loadCredentials(std::string& ssid, std::string& pass);
    static bool provisioned();
    static bool enabled();          ///< persistent intent; default = provisioned
    static void setEnabled(bool on);
};

}  // namespace net
