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
 * net/OtaUpdater.cpp — the Arduino half of net/OtaUpdater.h.
 *
 * One worker task runs either a version check or a firmware download; the loop
 * task only reads the spinlocked status POD and calls tick(). Downloading writes
 * straight into the spare OTA bank with esp_ota and validates the image before
 * handing the bootloader a new partition.
 */

#include "net/OtaUpdater.h"

#include "Config.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "net/Clock.h"
#include "net/HttpStream.h"
#include "net/TlsSession.h"
#include "net/Wifi.h"

// Upstream repository that hosts the releases. Kept for reference and forks, but
// NOT polled directly any more: the firmware talks to NUMOS_OTA_BASE below, which
// is a Cloudflare Redirect Rule on our own domain pointing at this repo's
// `releases/latest`. That indirection is the point — moving or re-hosting a
// release never requires reflashing a single device, only changing the rule.
// Overridable at build time with -DNUMOS_OTA_REPO="owner/name".
#ifndef NUMOS_OTA_REPO
#define NUMOS_OTA_REPO "AbuPaad/AbsolutOS"
#endif

// The one base URL the firmware actually polls. Overridable at build time with
// -DNUMOS_OTA_BASE="https://host" so a fork does not have to edit this file.
// Paths it must serve (Cloudflare 302s, so followRedirects is required):
//   /latest.json -> <repo> .../releases/latest                    (version check)
//   /NumOS.bin   -> <repo> .../releases/latest/download/NumOS.bin (firmware image)
#ifndef NUMOS_OTA_BASE
#define NUMOS_OTA_BASE "https://fw.absolutcas.com"
#endif

namespace net {
namespace {

constexpr uint32_t    kChunkBytes      = 4096;
constexpr uint32_t    kTaskStackBytes  = 8192;
constexpr UBaseType_t kTaskPriority    = 1;
constexpr BaseType_t  kTaskCore        = 0;   ///< Wi-Fi is pinned to core 0
constexpr int         kCheckTimeoutMs  = 20000;
constexpr int         kDownloadTimeout = 90000;
constexpr size_t      kMaxApiBody      = 128u * 1024u;
constexpr uint32_t    kAutoCheckDelayMs = 6000;

enum class WorkerMode : uint8_t { None, Check, Install };

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
OtaPhase     g_phase = OtaPhase::Idle;
char         g_latest[32] = {0};
char         g_error[128] = {0};
char         g_sha[65]    = {0};       ///< lowercase hex, empty = unknown
uint32_t     g_rx = 0;
uint32_t     g_total = 0;

std::atomic<bool>    g_workerActive{false};
std::atomic<bool>    g_taskDone{false};
std::atomic<bool>    g_cancel{false};
std::atomic<bool>    g_autoCheck{true};
std::atomic<bool>    g_autoCheckDone{false};
std::atomic<uint8_t> g_mode{static_cast<uint8_t>(WorkerMode::None)};
uint32_t             g_autoArmedAt = 0;

HttpStream g_stream;     ///< one instance: check and install never overlap
TaskHandle_t g_task = nullptr;

// ── status helpers ──────────────────────────────────────────────────────────
void setPhase(OtaPhase p) {
    portENTER_CRITICAL(&g_lock);
    g_phase = p;
    if (p != OtaPhase::Failed) g_error[0] = 0;
    portEXIT_CRITICAL(&g_lock);
}

void setError(const char* why) {
    portENTER_CRITICAL(&g_lock);
    g_phase = OtaPhase::Failed;
    std::snprintf(g_error, sizeof(g_error), "%s", (why && *why) ? why : "unknown error");
    portEXIT_CRITICAL(&g_lock);
}

void setLatest(const std::string& tag) {
    portENTER_CRITICAL(&g_lock);
    std::snprintf(g_latest, sizeof(g_latest), "%s", tag.c_str());
    portEXIT_CRITICAL(&g_lock);
}

void setSha(const std::string& hex) {
    portENTER_CRITICAL(&g_lock);
    std::snprintf(g_sha, sizeof(g_sha), "%s", hex.c_str());
    portEXIT_CRITICAL(&g_lock);
}

void getSha(char out[65]) {
    portENTER_CRITICAL(&g_lock);
    std::memcpy(out, g_sha, sizeof(g_sha));
    portEXIT_CRITICAL(&g_lock);
}

void setProgress(uint32_t rx, uint32_t total) {
    portENTER_CRITICAL(&g_lock);
    g_rx = rx;
    if (total) g_total = total;
    portEXIT_CRITICAL(&g_lock);
}

void resetProgress() {
    portENTER_CRITICAL(&g_lock);
    g_rx = 0;
    g_total = 0;
    portEXIT_CRITICAL(&g_lock);
}

// ── pure helpers (run on the worker task only) ──────────────────────────────
void semverParse(const std::string& in, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    const char* p = in.c_str();
    while (*p && !std::isdigit(static_cast<unsigned char>(*p))) ++p;  // skip 'v'
    int i = 0;
    while (i < 3 && *p) {
        char* end = nullptr;
        const long n = std::strtol(p, &end, 10);
        if (end == p) break;
        out[i++] = static_cast<int>(n);
        p = end;
        if (*p == '.') ++p; else break;
    }
}

int semverCompare(const std::string& a, const std::string& b) {
    int va[3], vb[3];
    semverParse(a, va);
    semverParse(b, vb);
    for (int i = 0; i < 3; ++i) {
        if (va[i] < vb[i]) return -1;
        if (va[i] > vb[i]) return 1;
    }
    return 0;
}

/// Pull a top-level-ish `"key":"value"` string out of the release JSON. Good
/// enough for GitHub's stable schema; no allocation beyond `out`.
bool jsonString(const std::string& body, const char* key, std::string& out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t k = body.find(pat);
    if (k == std::string::npos) return false;
    size_t colon = body.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    size_t q1 = body.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    size_t q2 = q1 + 1;
    while (q2 < body.size()) {
        if (body[q2] == '\\') { q2 += 2; continue; }
        if (body[q2] == '"') break;
        ++q2;
    }
    if (q2 >= body.size()) return false;
    out = body.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

/// GitHub populates assets[].digest with "sha256:<64 hex>" on newer releases.
bool findDigest(const std::string& body, std::string& hex) {
    const std::string pat = "\"digest\":\"sha256:";
    size_t k = body.find(pat);
    if (k == std::string::npos) return false;
    size_t start = k + pat.size();
    size_t end = start;
    while (end < body.size() && std::isxdigit(static_cast<unsigned char>(body[end]))) ++end;
    if (end - start != 64) return false;
    hex = body.substr(start, 64);
    return true;
}

std::string hexLower(const unsigned char* d, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(kHex[(d[i] >> 4) & 0xF]);
        s.push_back(kHex[d[i] & 0xF]);
    }
    return s;
}

// ── worker ──────────────────────────────────────────────────────────────────
void runCheck();
void runInstall();

void workerTrampoline(void*) {
    const WorkerMode mode = static_cast<WorkerMode>(g_mode.load());
    if (mode == WorkerMode::Check) runCheck();
    else                           runInstall();

    g_workerActive.store(false);
    g_taskDone.store(true);
    vTaskDelete(nullptr);
}

void runCheck() {
    if (!Wifi::state().connected) { setError("Wi-Fi not connected"); return; }
    if (!timeSynced()) { setError("clock not set (no NTP) - TLS cannot validate certificates"); return; }

    HttpReq req;
    req.method          = "GET";
    // Via our own domain, which 302s to GitHub's releases/latest. GitHub is not
    // polled directly, so the hostname the firmware knows never has to change
    // when a release is re-hosted.
    req.url             = std::string(NUMOS_OTA_BASE) + "/latest.json";
    req.accept          = "application/vnd.github+json";
    req.contentType.clear();                 // GET carries no body
    req.userAgent       = std::string("NumOS/") + OtaUpdater::runningVersion();
    req.timeoutMs       = kCheckTimeoutMs;
    // MUST stay true: NUMOS_OTA_BASE answers with a 302. Unfollowed, that
    // surfaces below as "releases/latest HTTP 302" and the check fails.
    req.followRedirects = true;

    if (!g_stream.open(req)) { setError(g_stream.error().c_str()); return; }
    const int http = g_stream.status();
    if (http != 200) {
        char why[48];
        std::snprintf(why, sizeof(why), "releases/latest HTTP %d", http);
        g_stream.close();
        setError(why);
        return;
    }

    std::string body;
    body.reserve(32768);
    char buf[1024];
    for (;;) {
        if (g_cancel.load()) { g_stream.close(); setPhase(OtaPhase::Idle); return; }
        const int n = g_stream.read(buf, sizeof(buf));
        if (n <= 0) break;
        body.append(buf, static_cast<size_t>(n));
        if (body.size() > kMaxApiBody) break;
    }
    g_stream.close();

    std::string tag;
    if (!jsonString(body, "tag_name", tag) || tag.empty()) {
        setError("release has no tag");
        return;
    }
    if (tag[0] == 'v' || tag[0] == 'V') tag.erase(0, 1);
    setLatest(tag);

    std::string digest;
    setSha(findDigest(body, digest) ? digest : std::string());

    if (semverCompare(tag, OtaUpdater::runningVersion()) > 0) {
        setPhase(OtaPhase::Available);
    } else {
        setPhase(OtaPhase::UpToDate);
    }
}

void runInstall() {
    if (!Wifi::state().connected) { setError("Wi-Fi not connected"); return; }
    if (!timeSynced()) { setError("clock not set (no NTP) - TLS cannot validate certificates"); return; }

    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) { setError("no spare OTA bank (flash layout is not OTA-capable)"); return; }

    HttpReq req;
    req.method          = "GET";
    // Two hops now: our own domain 302s to github.com, which 302s again to the
    // signed CDN host that actually serves the bytes.
    req.url             = std::string(NUMOS_OTA_BASE) + "/NumOS.bin";
    req.accept          = "application/octet-stream";
    req.contentType.clear();
    req.userAgent       = std::string("NumOS/") + OtaUpdater::runningVersion();
    req.timeoutMs       = kDownloadTimeout;
    req.followRedirects = true;              // our 302, then GitHub's 302 to the CDN

    setProgress(0, 0);
    if (!g_stream.open(req)) { setError(g_stream.error().c_str()); return; }
    const int http = g_stream.status();
    if (http != 200) {
        char why[48];
        std::snprintf(why, sizeof(why), "firmware download HTTP %d", http);
        g_stream.close();
        setError(why);
        return;
    }
    const long clen = g_stream.contentLength();
    if (clen > 0) setProgress(0, static_cast<uint32_t>(clen));

    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota) != ESP_OK) {
        g_stream.close();
        setError("esp_ota_begin failed");
        return;
    }

    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(kChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!buf) {
        buf = static_cast<uint8_t*>(
            heap_caps_malloc(kChunkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!buf) {
        esp_ota_abort(ota);
        g_stream.close();
        setError("OTA buffer allocation failed");
        return;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    uint32_t   rx = 0;
    const char* failWhy = nullptr;
    for (;;) {
        if (g_cancel.load()) { failWhy = "update cancelled"; break; }
        const int n = g_stream.read(reinterpret_cast<char*>(buf), kChunkBytes);
        if (n <= 0) break;
        if (esp_ota_write(ota, buf, static_cast<size_t>(n)) != ESP_OK) {
            failWhy = "flash write failed";
            break;
        }
        mbedtls_sha256_update_ret(&sha, buf, static_cast<size_t>(n));
        rx += static_cast<uint32_t>(n);
        setProgress(rx, clen > 0 ? static_cast<uint32_t>(clen) : 0);
    }

    unsigned char digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    heap_caps_free(buf);
    g_stream.close();

    if (failWhy) { esp_ota_abort(ota); setError(failWhy); return; }
    if (rx == 0) { esp_ota_abort(ota); setError("empty firmware download"); return; }

    char expect[65];
    getSha(expect);
    if (expect[0] != 0) {
        if (hexLower(digest, sizeof(digest)) != expect) {
            esp_ota_abort(ota);
            setError("firmware digest mismatch");
            return;
        }
    }

    if (esp_ota_end(ota) != ESP_OK) {
        setError("image verification failed (esp_ota_end)");
        return;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        setError("esp_ota_set_boot_partition failed");
        return;
    }

    setProgress(rx, rx);
    setPhase(OtaPhase::Rebooting);
    vTaskDelay(pdMS_TO_TICKS(1200));   // let the UI paint "Rebooting..."
    ESP.restart();
}

bool startWorker(WorkerMode mode) {
    if (g_workerActive.exchange(true)) return false;
    g_cancel.store(false);
    g_taskDone.store(false);
    g_mode.store(static_cast<uint8_t>(mode));
    resetProgress();
    setPhase(mode == WorkerMode::Check ? OtaPhase::Checking : OtaPhase::Downloading);

    if (xTaskCreatePinnedToCore(&workerTrampoline, "ota", kTaskStackBytes, nullptr,
                                kTaskPriority, &g_task, kTaskCore) != pdPASS) {
        g_workerActive.store(false);
        setError("update task create failed");
        return false;
    }
    return true;
}

}  // namespace

const char* OtaUpdater::runningVersion() { return NUMOS_VERSION; }

bool OtaUpdater::check()  { return startWorker(WorkerMode::Check); }
bool OtaUpdater::install() { return startWorker(WorkerMode::Install); }

void OtaUpdater::cancel() {
    if (!g_workerActive.load()) return;
    g_cancel.store(true);
    g_stream.close();   // unblocks the worker's read()
}

void OtaUpdater::tick(uint32_t nowMs) {
    if (g_taskDone.exchange(false)) {
        g_task = nullptr;   // worker self-deleted; the handle is not ours to join
    }

    if (!g_autoCheck.load() || g_autoCheckDone.load() || g_workerActive.load()) return;

    const bool ready = Wifi::state().connected && timeSynced();
    if (!ready) { g_autoArmedAt = 0; return; }

    // Never START the boot check while ANY TLS session is live — an AI request
    // the user just made, or a Wolfram hop. All of them want the same 2x16 KB of
    // internal RAM, and the automatic check is the lowest-priority user of it, so
    // it waits its turn. The arm timer is deliberately NOT reset here: the check
    // then fires the moment the slot frees, instead of restarting its 6 s wait.
    if (tlsSessionBusy()) return;

    if (g_autoArmedAt == 0) g_autoArmedAt = nowMs;
    if (static_cast<uint32_t>(nowMs - g_autoArmedAt) >= kAutoCheckDelayMs) {
        g_autoCheckDone.store(true);   // one shot per boot
        check();
    }
}

OtaState OtaUpdater::state() {
    OtaState s;
    char latest[sizeof(g_latest)];
    char err[sizeof(g_error)];
    OtaPhase phase;
    uint32_t rx, total;
    portENTER_CRITICAL(&g_lock);
    phase = g_phase;
    std::memcpy(latest, g_latest, sizeof(latest));
    std::memcpy(err, g_error, sizeof(err));
    rx = g_rx;
    total = g_total;
    portEXIT_CRITICAL(&g_lock);

    s.phase         = phase;
    s.latestVersion = latest;
    s.error         = err;
    s.bytesReceived = rx;
    s.bytesTotal    = total;
    s.runningVersion = runningVersion();
    return s;
}

bool OtaUpdater::busy() { return g_workerActive.load(); }

const char* OtaUpdater::unavailableReason() { return nullptr; }

void OtaUpdater::setAutoCheck(bool on) { g_autoCheck.store(on); }

}  // namespace net

#else  // !ARDUINO — emulator / host build

namespace net {

const char* OtaUpdater::runningVersion() { return NUMOS_VERSION; }
bool OtaUpdater::check() { return false; }
bool OtaUpdater::install() { return false; }
void OtaUpdater::cancel() {}
void OtaUpdater::tick(uint32_t) {}
bool OtaUpdater::busy() { return false; }
OtaState OtaUpdater::state() {
    OtaState s;
    s.runningVersion = NUMOS_VERSION;
    return s;
}
const char* OtaUpdater::unavailableReason() {
    return "the update service is firmware-only (no network on this build)";
}
void OtaUpdater::setAutoCheck(bool) {}

}  // namespace net

#endif  // ARDUINO
