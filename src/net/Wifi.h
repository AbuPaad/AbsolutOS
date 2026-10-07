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

/// One stored network. The password is kept because re-joining needs it; it is
/// NVS-only and never leaves the device except into a driver call.
struct WifiNetwork {
    std::string ssid;
    std::string pass;
};

/// One sighting from a scan. RSSI is the strongest observation of that SSID.
struct WifiScanEntry {
    std::string ssid;
    int         rssi = 0;      ///< dBm (negative; -100 is unusable, -50 is close)
    bool        open = false;  ///< no encryption (WPA2/WPA3 => false)
};

class Wifi {
public:
    /// Saved-network slots. Six is the owner's number: home, work, two phone
    /// hotspots and a couple of spares, each costing an NVS string pair.
    static constexpr size_t kMaxNetworks = 6;

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
     * Start associating with `ssid` and return immediately — the caller polls
     * state().connected (the portal's pre-scan "join a saved network before
     * raising the AP" uses this, on the main loop task, so it must never block).
     * Does NOT touch NVS: a failed pre-join must not reorder the saved list.
     */
    static bool connectAsync(const std::string& ssid, const std::string& pass);

    /**
     * Real teardown, in the documented order: retry timer → disconnect(eraseap)
     * → WIFI_OFF. `WIFI_OFF` is what actually powers the radio down and returns
     * ~30-60 KB of internal RAM; disconnect() alone leaves the driver running.
     * eraseCreds clears the WHOLE saved list, not just one entry.
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
     *
     * This also walks the SAVED LIST: after a few failed attempts on the current
     * network it moves to the next stored one and wraps, so a unit that leaves
     * home and comes back to a phone hotspot finds it without being told. It
     * also pumps any scan that is in flight.
     *
     * While a provisioning AP is up (startProvisioningAp .. stopProvisioningAp)
     * the STA's automatic retry/failover walk is SUSPENDED: every attempt (and
     * every scan) yanks the shared radio off the AP's channel and makes clients
     * stall at "authenticating". It resumes when the AP goes down.
     */
    static void tick(uint32_t nowMs);

    /**
     * AP+STA so the provisioning access point exists while STA keeps trying.
     * The AP is pinned to kApChannel and, for as long as it is up, owns the
     * radio (tick() stops auto-retrying the STA). Callers must have finished
     * any scan they care about first: a scan runs before the AP, never under it.
     */
    static bool startProvisioningAp(const char* ssid, const char* pass);
    static void stopProvisioningAp();

    // ── Saved networks, in priority order (index 0 is tried first) ──────────
    // We keep the list ourselves rather than leaning on the driver's one-shot
    // store: that is what buys priority ordering, failover and "forget", and it
    // lets WiFi.persistent(false) stop the driver writing flash on every
    // connect. Slot 0 mirrors the legacy single-network keys, so units flashed
    // before this list existed keep working with no migration step.
    static size_t networkCount();
    static bool   networkAt(size_t index, WifiNetwork& out);
    /// -1 when the SSID is not stored.
    static int    networkIndex(const std::string& ssid);
    /**
     * Add or update a network. `makePrimary` moves it to the front (what the
     * portal's "Save & join" and the on-device picker both do). Yields the
     * oldest non-primary entry when the list is full.
     */
    static bool   saveNetwork(const std::string& ssid, const std::string& pass, bool makePrimary);
    static bool   forgetNetwork(const std::string& ssid);
    /// Index the STA is on (or currently trying). -1 before begin().
    static int    activeNetworkIndex();

    // ── Asynchronous scan ──────────────────────────────────────────────────
    // Never blocking: the loop task calls startScan() and tick() collects the
    // result, so a 1-2 s scan cannot stall a frame or the keypad.
    static bool   startScan();          ///< false if a scan is already running
    static bool   scanRunning();
    static size_t scanCount();
    static bool   scanAt(size_t index, WifiScanEntry& out);
    /**
     * Signal for a stored network, for a UI that must show availability:
     * the driver's live RSSI when the STA is actually on it (a connected station
     * is not reliably reported in its own scan), else the strongest sighting from
     * the last scan. 0 means "not reachable / no scan yet" — dBm is negative, so
     * zero is unambiguous.
     */
    static int    signalFor(const std::string& ssid);

    // ── Credentials and intent, in our own NVS namespace ──────────────────
    static bool saveCredentials(const std::string& ssid, const std::string& pass);
    static bool loadCredentials(std::string& ssid, std::string& pass);
    static bool provisioned();
    static bool enabled();          ///< persistent intent; default = provisioned
    static void setEnabled(bool on);
};

}  // namespace net
