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
 * net/Wifi.cpp — the Arduino half of net/Wifi.h.
 *
 * Only the ARDUINO build links this. The non-Arduino branch exists so the
 * translation unit is never empty and a native build that globs src/net/ can
 * still link it.
 *
 * Facts that decide this file (device-networking.md §1.1, all verified against
 * arduino-esp32 3.2.0 in ~/.platformio/packages/framework-arduinoespressif32):
 *  - event ids are the v3 `ARDUINO_EVENT_WIFI_STA_*` ones; the 2.x
 *    `SYSTEM_EVENT_*` spelling does not compile;
 *  - the callback runs on another task, so it only ever writes volatile
 *    scalars (no String, no std::string, no LVGL);
 *  - WiFi.onEvent / removeEvent are not thread-safe and are never called from
 *    inside a callback;
 *  - ARDUINO_EVENT_WIFI_STA_DISCONNECTED fires spuriously in the field, so
 *    nothing is driven directly off it — tick() re-checks isConnected();
 *  - setHostname() must precede begin()/mode() or it silently does nothing.
 */

#include "net/Wifi.h"

#if defined(ARDUINO)

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

#include <cstdio>

namespace net {
namespace {

constexpr const char* kHostname     = "numos";
constexpr const char* kNvsNamespace = "numos_net";
constexpr const char* kKeySsid      = "ssid";
constexpr const char* kKeyPass      = "pass";
constexpr const char* kKeyEnabled   = "wifi.enabled";

constexpr uint32_t kRetryMinMs = 5000;    ///< never tighter than 5 s
constexpr uint32_t kRetryMaxMs = 60000;

// ── state written by the Wi-Fi event task, read by the main loop ─────────────
volatile bool s_gotIp        = false;
volatile bool s_disconnected = false;
volatile int  s_reason       = 0;

// ── state owned by the main loop ────────────────────────────────────────────
bool              s_begun        = false;
uint32_t          s_lastRetryMs  = 0;
uint32_t          s_retryDelayMs = kRetryMinMs;
bool              s_evtsUp       = false;
wifi_event_id_t   s_evtGotIp     = 0;
wifi_event_id_t   s_evtDown      = 0;

/**
 * The pointer form of the callback (WiFiEventSysCb) is the only one that carries
 * event_info, which is where the disconnect reason lives. WiFiEvent_t in this
 * core is an id enum, not a pointer — using the id overload here would compile
 * and then lose the reason silently.
 */
void onGotIp(arduino_event_t*) {
    s_gotIp        = true;
    s_disconnected = false;
    s_retryDelayMs = kRetryMinMs;
}

void onDisconnected(arduino_event_t* e) {
    s_disconnected = true;
    s_reason = e ? (int)e->event_info.wifi_sta_disconnected.reason : 0;
}

void registerEvents() {
    if (s_evtsUp) return;
    s_evtGotIp = WiFi.onEvent(&onGotIp,        ARDUINO_EVENT_WIFI_STA_GOT_IP);
    s_evtDown  = WiFi.onEvent(&onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    s_evtsUp   = true;
}

void unregisterEvents() {
    if (!s_evtsUp) return;
    WiFi.removeEvent(s_evtGotIp);
    WiFi.removeEvent(s_evtDown);
    s_evtsUp = false;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════

bool Wifi::begin() {
    if (s_begun) return true;
    if (!enabled())     return false;   // radio never initialised at all
    if (!provisioned()) return false;   // the portal is a later phase

    // persistent(false): we own the credential store, so the driver must not
    // write flash on every connect.
    WiFi.persistent(false);
    WiFi.setHostname(kHostname);        // BEFORE mode()/begin() or it is ignored
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    registerEvents();

    std::string ssid, pass;
    if (!loadCredentials(ssid, pass)) return false;

    // begin() returns immediately; association completes via the events above.
    WiFi.begin(ssid.c_str(), pass.c_str());
    s_begun        = true;
    s_lastRetryMs  = millis();
    s_retryDelayMs = kRetryMinMs;
    return true;
}

bool Wifi::connect(const std::string& ssid, const std::string& pass, int timeoutMs) {
    if (!s_begun) {
        WiFi.persistent(false);
        WiFi.setHostname(kHostname);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        registerEvents();
        s_begun = true;
    }
    WiFi.begin(ssid.c_str(), pass.c_str());

    const uint32_t t0 = millis();
    while ((int32_t)(millis() - t0) < timeoutMs) {
        if (WiFi.isConnected()) return true;
        delay(50);
    }
    return WiFi.isConnected();
}

void Wifi::disconnect(bool eraseCreds) {
    unregisterEvents();
    WiFi.disconnect(/*wifioff=*/true, /*eraseap=*/eraseCreds);
    WiFi.mode(WIFI_OFF);        // actually powers the radio down
    s_begun        = false;
    s_gotIp        = false;
    s_disconnected = false;
    s_reason       = 0;
    s_retryDelayMs = kRetryMinMs;

    if (eraseCreds) {
        Preferences p;
        if (p.begin(kNvsNamespace, /*readOnly=*/false)) {
            p.remove(kKeySsid);
            p.remove(kKeyPass);
            p.end();
        }
    }
}

WifiState Wifi::state() {
    WifiState s;
    s.connected = s_begun && WiFi.isConnected();
    if (s.connected) {
        s.rssi = (int)WiFi.RSSI();
        s.ssid = WiFi.SSID().c_str();
        const IPAddress ip = WiFi.localIP();
        char buf[20];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
        s.ip = buf;
    }
    s.disconnectReason = s_reason;
    return s;
}

void Wifi::setPowerSave(bool on) {
    // setSleep(false) = modem sleep OFF: DTIM sleep adds hundreds of ms of
    // latency to a streamed response, so it is disabled while a request is in
    // flight and re-enabled when idle.
    WiFi.setSleep(!on);
}

void Wifi::tick(uint32_t nowMs) {
    if (!s_begun) return;

    // Re-check rather than trusting the event: STA_DISCONNECTED is noisy.
    if (WiFi.isConnected()) {
        s_retryDelayMs = kRetryMinMs;
        return;
    }

    const uint32_t jitter = nowMs % 977u;   // spread the retries of many units
    if ((nowMs - s_lastRetryMs) < (s_retryDelayMs + jitter)) return;

    s_lastRetryMs = nowMs;
    s_retryDelayMs = (s_retryDelayMs * 2u > kRetryMaxMs) ? kRetryMaxMs : s_retryDelayMs * 2u;
    WiFi.reconnect();
}

bool Wifi::startProvisioningAp(const char* ssid, const char* pass) {
    // AP_STA so the AP exists while STA keeps trying to associate (the portal's
    // "test before save" needs exactly this shape).
    if (!WiFi.mode(WIFI_AP_STA)) return false;
    registerEvents();
    return WiFi.softAP(ssid, pass);
}

void Wifi::stopProvisioningAp() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

bool Wifi::saveCredentials(const std::string& ssid, const std::string& pass) {
    Preferences p;
    if (!p.begin(kNvsNamespace, false)) return false;
    p.putString(kKeySsid, ssid.c_str());
    p.putString(kKeyPass, pass.c_str());
    p.putBool(kKeyEnabled, true);       // provisioning implies "radio wanted"
    p.end();
    return true;
}

bool Wifi::loadCredentials(std::string& ssid, std::string& pass) {
    Preferences p;
    if (!p.begin(kNvsNamespace, true)) return false;
    ssid = p.getString(kKeySsid, "").c_str();
    pass = p.getString(kKeyPass, "").c_str();
    p.end();
    return !ssid.empty();
}

bool Wifi::provisioned() {
    Preferences p;
    if (!p.begin(kNvsNamespace, true)) return false;
    const bool ok = p.isKey(kKeySsid);
    p.end();
    return ok;
}

bool Wifi::enabled() {
    Preferences p;
    if (!p.begin(kNvsNamespace, true)) return false;
    const bool hasCreds = p.isKey(kKeySsid);
    const bool intent   = p.getBool(kKeyEnabled, hasCreds);
    p.end();
    return intent;
}

void Wifi::setEnabled(bool on) {
    Preferences p;
    if (p.begin(kNvsNamespace, false)) {
        p.putBool(kKeyEnabled, on);
        p.end();
    }
    if (!on) disconnect(/*eraseCreds=*/false);
}

}  // namespace net

#else   // ── non-Arduino: never linked by the firmware, kept linkable ──────────

namespace net {

bool      Wifi::begin()                                                            { return false; }
bool      Wifi::connect(const std::string&, const std::string&, int)               { return false; }
void      Wifi::disconnect(bool)                                                   {}
WifiState Wifi::state()                                                            { return {}; }
void      Wifi::setPowerSave(bool)                                                 {}
void      Wifi::tick(uint32_t)                                                     {}
bool      Wifi::startProvisioningAp(const char*, const char*)                       { return false; }
void      Wifi::stopProvisioningAp()                                               {}
bool      Wifi::saveCredentials(const std::string&, const std::string&)             { return false; }
bool      Wifi::loadCredentials(std::string&, std::string&)                         { return false; }
bool      Wifi::provisioned()                                                      { return false; }
bool      Wifi::enabled()                                                          { return false; }
void      Wifi::setEnabled(bool)                                                   {}

}  // namespace net

#endif  // ARDUINO
