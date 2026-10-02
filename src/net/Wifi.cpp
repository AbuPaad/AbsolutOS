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
constexpr const char* kKeySsid      = "ssid";   ///< legacy single-network slot
constexpr const char* kKeyPass      = "pass";
constexpr const char* kKeyEnabled   = "wifi.enabled";
constexpr const char* kKeyCount     = "n";      ///< entries in the saved list

constexpr uint32_t kRetryMinMs = 5000;    ///< never tighter than 5 s
constexpr uint32_t kRetryMaxMs = 60000;

/// Attempts on ONE saved network before the list moves on (failover).
constexpr uint8_t kAttemptsPerNetwork = 3;

/// scanComplete()'s own sentinels (wifi_types.h), named here so this file does
/// not depend on which WiFi header happens to define the macros.
constexpr int16_t kScanRunning = -1;
constexpr int16_t kScanFailed  = -2;

/// Scan cache. POD on purpose: it is written by the loop task in tick() and
/// read by the portal's HTTP task, so it holds no std::string and no allocator
/// state — a torn read can only ever produce one stale row, never a crash.
constexpr size_t kMaxScanEntries = 24;
struct ScanSlot {
    char    ssid[33];
    int16_t rssi;
    uint8_t open;
};

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

int               s_activeIndex  = -1;   ///< saved-list slot the STA is on/trying
std::string       s_activeSsid;          ///< cached, so tick() does not hit NVS
uint8_t           s_attemptsOnCurrent = 0;

volatile bool s_scanRunning = false;
volatile bool s_scanFailed  = false;
volatile int  s_scanCount   = 0;
ScanSlot      s_scan[kMaxScanEntries] = {};

// ── saved-network list: NVS helpers (the shape Wifi.h documents) ────────────

void slotKeys(uint8_t index, char* ssidKey, size_t n, char* passKey, size_t m) {
    snprintf(ssidKey, n, "ssid%u", static_cast<unsigned>(index));
    snprintf(passKey, m, "pass%u", static_cast<unsigned>(index));
}

/**
 * Read the whole list in priority order.
 *
 * Legacy compatibility is a fallback, not a migration: a unit flashed before
 * the list existed has only "ssid"/"pass", and it is read as slot 0. The next
 * write mirrors slot 0 back into those keys, so old and new readers agree.
 */
size_t readNetworkList(WifiNetwork* out, size_t maxOut) {
    if (!out || maxOut == 0) return 0;
    Preferences p;
    if (!p.begin(kNvsNamespace, /*readOnly=*/true)) return 0;

    size_t n = 0;
    const uint8_t stored = p.getUChar(kKeyCount, 0);
    for (uint8_t i = 0; i < stored && n < maxOut; ++i) {
        char sk[12], pk[12];
        slotKeys(i, sk, sizeof(sk), pk, sizeof(pk));
        const String s = p.getString(sk, String());
        if (s.length() == 0) continue;
        out[n].ssid = s.c_str();
        out[n].pass = p.getString(pk, String()).c_str();
        ++n;
    }
    if (n == 0 && p.isKey(kKeySsid)) {
        out[0].ssid = p.getString(kKeySsid, String()).c_str();
        out[0].pass = p.getString(kKeyPass, String()).c_str();
        n = out[0].ssid.empty() ? 0 : 1;
    }
    p.end();
    return n;
}

bool writeNetworkList(const WifiNetwork* list, size_t n) {
    if (n > Wifi::kMaxNetworks) n = Wifi::kMaxNetworks;
    Preferences p;
    if (!p.begin(kNvsNamespace, /*readOnly=*/false)) return false;
    for (uint8_t i = 0; i < Wifi::kMaxNetworks; ++i) {
        char sk[12], pk[12];
        slotKeys(i, sk, sizeof(sk), pk, sizeof(pk));
        if (i < n) {
            p.putString(sk, list[i].ssid.c_str());
            p.putString(pk, list[i].pass.c_str());
        } else {
            p.remove(sk);
            p.remove(pk);
        }
    }
    p.putUChar(kKeyCount, static_cast<uint8_t>(n));
    if (n > 0) {
        p.putString(kKeySsid, list[0].ssid.c_str());
        p.putString(kKeyPass, list[0].pass.c_str());
    } else {
        p.remove(kKeySsid);
        p.remove(kKeyPass);
    }
    p.end();
    return true;
}

bool addOrPromoteNetwork(const std::string& ssid, const std::string& pass, bool makePrimary) {
    if (ssid.empty()) return false;
    WifiNetwork list[Wifi::kMaxNetworks];
    size_t n = readNetworkList(list, Wifi::kMaxNetworks);

    size_t found = n;
    for (size_t i = 0; i < n; ++i) {
        if (list[i].ssid == ssid) { found = i; break; }
    }

    // An empty password means "keep the stored one" — the portal cannot echo a
    // password back, so it must be able to say "unchanged".
    WifiNetwork entry;
    entry.ssid = ssid;
    entry.pass = (pass.empty() && found < n) ? list[found].pass : pass;

    if (found < n) {
        for (size_t i = found; i + 1 < n; ++i) list[i] = list[i + 1];
        --n;
    }

    if (makePrimary) {
        if (n == Wifi::kMaxNetworks) --n;          // the tail is the oldest spare
        for (size_t i = n; i > 0; --i) list[i] = list[i - 1];
        list[0] = entry;
        ++n;
    } else {
        if (n == Wifi::kMaxNetworks) n = Wifi::kMaxNetworks - 1;
        list[n++] = entry;
    }
    return writeNetworkList(list, n);
}

/** Point the STA at a stored slot. Used by begin() and by failover. */
void startNetwork(size_t index) {
    WifiNetwork list[Wifi::kMaxNetworks];
    const size_t n = readNetworkList(list, Wifi::kMaxNetworks);
    if (index >= n) return;
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    WiFi.begin(list[index].ssid.c_str(), list[index].pass.c_str());
    s_activeIndex = static_cast<int>(index);
    s_activeSsid  = list[index].ssid;
    s_attemptsOnCurrent = 0;
    s_lastRetryMs = millis();
    Serial.printf("[WIFI] trying saved network %u/%u '%s'\n",
                  static_cast<unsigned>(index + 1), static_cast<unsigned>(n),
                  list[index].ssid.c_str());
}

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
    if (!provisioned()) return false;   // nothing saved yet: the portal comes first

    // persistent(false): we own the credential store, so the driver must not
    // write flash on every connect.
    WiFi.persistent(false);
    WiFi.setHostname(kHostname);        // BEFORE mode()/begin() or it is ignored
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    registerEvents();

    s_begun        = true;
    s_retryDelayMs = kRetryMinMs;
    s_lastRetryMs  = millis();
    startNetwork(0);                    // priority order starts at slot 0
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
            for (uint8_t i = 0; i < Wifi::kMaxNetworks; ++i) {
                char sk[12], pk[12];
                slotKeys(i, sk, sizeof(sk), pk, sizeof(pk));
                p.remove(sk);
                p.remove(pk);
            }
            p.putUChar(kKeyCount, 0);
            p.remove(kKeySsid);
            p.remove(kKeyPass);
            p.end();
        }
        s_activeIndex = -1;
        s_activeSsid.clear();
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
    // ── pump an in-flight scan (independent of the STA's state) ─────────────
    if (s_scanRunning) {
        const int16_t found = WiFi.scanComplete();
        if (found >= 0) {
            s_scanCount = 0;
            for (int i = 0; i < found && s_scanCount < (int)kMaxScanEntries; ++i) {
                const String ss = WiFi.SSID(i);
                if (ss.length() == 0) continue;          // hidden networks
                const int16_t rssi = (int16_t)WiFi.RSSI(i);

                // One row per SSID: a mesh or a repeater shows up several times
                // and only the strongest sighting is worth reporting.
                int dup = -1;
                for (int j = 0; j < (int)s_scanCount; ++j) {
                    if (strncmp(s_scan[j].ssid, ss.c_str(), sizeof(s_scan[0].ssid)) == 0) {
                        dup = j;
                        break;
                    }
                }
                if (dup >= 0) {
                    if (rssi > s_scan[dup].rssi) s_scan[dup].rssi = rssi;
                    continue;
                }
                strncpy(s_scan[s_scanCount].ssid, ss.c_str(), sizeof(s_scan[0].ssid) - 1);
                s_scan[s_scanCount].ssid[sizeof(s_scan[0].ssid) - 1] = '\0';
                s_scan[s_scanCount].rssi = rssi;
                s_scan[s_scanCount].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 1 : 0;
                ++s_scanCount;
            }
            WiFi.scanDelete();
            s_scanRunning = false;
            s_scanFailed  = false;
        } else if (found == kScanFailed) {
            WiFi.scanDelete();
            s_scanRunning = false;
            s_scanFailed  = true;
            s_scanCount   = 0;
        }
    }

    if (!s_begun) return;

    // Re-check rather than trusting the event: STA_DISCONNECTED is noisy.
    if (WiFi.isConnected()) {
        s_retryDelayMs = kRetryMinMs;
        s_attemptsOnCurrent = 0;
        const String cur = WiFi.SSID();
        if (cur.length() && s_activeSsid != cur.c_str()) {
            // Landed on a different saved slot (or the driver roamed): remember
            // it so the UI can show which network is live. NVS is only read when
            // the SSID actually changes, never per tick.
            s_activeSsid = cur.c_str();
            const int idx = networkIndex(s_activeSsid);
            if (idx >= 0) s_activeIndex = idx;
        }
        return;
    }

    const uint32_t jitter = nowMs % 977u;   // spread the retries of many units
    if ((nowMs - s_lastRetryMs) < (s_retryDelayMs + jitter)) return;

    s_lastRetryMs = nowMs;
    s_retryDelayMs = (s_retryDelayMs * 2u > kRetryMaxMs) ? kRetryMaxMs : s_retryDelayMs * 2u;

    // Failover: after a few attempts on one network, walk to the next stored
    // one. This is what makes "home Wi-Fi, else phone hotspot" work with no
    // input — and it wraps, so it keeps re-trying the whole list.
    ++s_attemptsOnCurrent;
    const size_t count = networkCount();
    if (count > 1 && s_attemptsOnCurrent >= kAttemptsPerNetwork) {
        const size_t next = (size_t)((s_activeIndex + 1) % (int)count);
        startNetwork(next);
        return;
    }
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
    // "Provision this network" always means "prefer it": the user just typed it.
    if (!addOrPromoteNetwork(ssid, pass, /*makePrimary=*/true)) return false;
    Preferences p;
    if (p.begin(kNvsNamespace, false)) {
        p.putBool(kKeyEnabled, true);       // provisioning implies "radio wanted"
        p.end();
    }
    return true;
}

bool Wifi::loadCredentials(std::string& ssid, std::string& pass) {
    WifiNetwork n;
    if (!networkAt(0, n)) {
        ssid.clear();
        pass.clear();
        return false;
    }
    ssid = n.ssid;
    pass = n.pass;
    return !ssid.empty();
}

bool Wifi::provisioned() {
    return networkCount() > 0;
}

bool Wifi::enabled() {
    Preferences p;
    if (!p.begin(kNvsNamespace, true)) return false;
    const bool intent = p.getBool(kKeyEnabled, false);
    p.end();
    return intent;
}

// ── saved-network list ──────────────────────────────────────────────────────

size_t Wifi::networkCount() {
    WifiNetwork list[kMaxNetworks];
    return readNetworkList(list, kMaxNetworks);
}

bool Wifi::networkAt(size_t index, WifiNetwork& out) {
    WifiNetwork list[kMaxNetworks];
    const size_t n = readNetworkList(list, kMaxNetworks);
    if (index >= n) return false;
    out = list[index];
    return true;
}

int Wifi::networkIndex(const std::string& ssid) {
    if (ssid.empty()) return -1;
    WifiNetwork list[kMaxNetworks];
    const size_t n = readNetworkList(list, kMaxNetworks);
    for (size_t i = 0; i < n; ++i) {
        if (list[i].ssid == ssid) return static_cast<int>(i);
    }
    return -1;
}

bool Wifi::saveNetwork(const std::string& ssid, const std::string& pass, bool makePrimary) {
    const bool ok = addOrPromoteNetwork(ssid, pass, makePrimary);
    if (ok) {
        Preferences p;
        if (p.begin(kNvsNamespace, false)) {
            p.putBool(kKeyEnabled, true);
            p.end();
        }
    }
    return ok;
}

bool Wifi::forgetNetwork(const std::string& ssid) {
    WifiNetwork list[kMaxNetworks];
    size_t n = readNetworkList(list, kMaxNetworks);
    size_t w = 0;
    bool removed = false;
    for (size_t i = 0; i < n; ++i) {
        if (list[i].ssid == ssid) { removed = true; continue; }
        list[w++] = list[i];
    }
    if (!removed) return false;
    if (!writeNetworkList(list, w)) return false;

    if (ssid == s_activeSsid) {
        // Drop the live network: stop trying it and let tick() pick the next
        // stored one (or go quiet when the list is now empty).
        s_activeSsid.clear();
        s_activeIndex = -1;
        if (w == 0) {
            disconnect(/*eraseCreds=*/false);
        } else {
            s_attemptsOnCurrent = kAttemptsPerNetwork;
            WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
        }
    }
    return true;
}

int Wifi::activeNetworkIndex() {
    return s_begun ? s_activeIndex : -1;
}

// ── scan ────────────────────────────────────────────────────────────────────

bool Wifi::startScan() {
    if (s_scanRunning) return false;
    // A scan needs an initialised radio. If nothing has been switched on yet
    // (never provisioned, portal closed), bring STA up for the scan only — the
    // settings screen has to be able to show what is in range before anything is
    // saved. A live AP_STA (portal) is left alone: switching modes there would
    // drop the phone.
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.persistent(false);
        WiFi.setHostname(kHostname);
        WiFi.mode(WIFI_STA);
    }
    s_scanFailed = false;
    s_scanRunning = true;
    s_scanCount   = 0;
    // async = true: this returns immediately and tick() collects the result, so
    // the loop never stalls for the 1-2 s a full scan takes.
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
    return true;
}

bool Wifi::scanRunning() { return s_scanRunning; }

size_t Wifi::scanCount() {
    const int n = s_scanCount;
    return n < 0 ? 0u : static_cast<size_t>(n);
}

bool Wifi::scanAt(size_t index, WifiScanEntry& out) {
    if (index >= scanCount()) return false;
    out.ssid = s_scan[index].ssid;
    out.rssi = s_scan[index].rssi;
    out.open = s_scan[index].open != 0;
    return true;
}

int Wifi::signalFor(const std::string& ssid) {
    if (ssid.empty()) return 0;
    // Live figure first: the driver knows the link it is holding.
    const WifiState s = state();
    if (s.connected && s.ssid == ssid) return s.rssi;
    for (size_t i = 0; i < scanCount(); ++i) {
        if (ssid == s_scan[i].ssid) return s_scan[i].rssi;
    }
    return 0;   // not in range (or no scan has completed yet)
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
size_t    Wifi::networkCount()                                                     { return 0; }
bool      Wifi::networkAt(size_t, WifiNetwork&)                                     { return false; }
int       Wifi::networkIndex(const std::string&)                                    { return -1; }
bool      Wifi::saveNetwork(const std::string&, const std::string&, bool)            { return false; }
bool      Wifi::forgetNetwork(const std::string&)                                   { return false; }
int       Wifi::activeNetworkIndex()                                                { return -1; }
bool      Wifi::startScan()                                                        { return false; }
bool      Wifi::scanRunning()                                                      { return false; }
size_t    Wifi::scanCount()                                                        { return 0; }
bool      Wifi::scanAt(size_t, WifiScanEntry&)                                      { return false; }
int       Wifi::signalFor(const std::string&)                                       { return 0; }

}  // namespace net

#endif  // ARDUINO
