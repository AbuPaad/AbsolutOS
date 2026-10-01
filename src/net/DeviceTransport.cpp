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
 * net/DeviceTransport.cpp — ai::AiTransport over WiFi + TLS + esp_http_client.
 *
 * This is the ONLY AI-specific piece of the networking work
 * (device-networking.md §1.5), and it exists because of one rule: the socket is
 * blocked on by a FreeRTOS task, while LVGL, the keypad scanner and
 * lv_timer_handler all share the loop task. So a worker task blocks in read()
 * and pushes raw bytes into an SPSC ring in PSRAM; poll() only ever touches the
 * ring, and can therefore never stall a frame.
 *
 * Contrast with the emulator's CurlTransport, whose write callback runs inside
 * poll() on the UI thread — which is exactly why that one needs no lock and no
 * queue, and why this threading shape must NOT be copied back the other way.
 *
 * Memory (device-networking.md §2): TLS lives in INTERNAL SRAM and must not be
 * pushed to PSRAM; the ring and the request body live in PSRAM.
 */

#include "net/DeviceTransport.h"

#if defined(ARDUINO)

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "ai/AiClient.h"
#include "net/Clock.h"
#include "net/HttpStream.h"
#include "net/Wifi.h"

namespace net {
namespace {

constexpr size_t      kRingBytes      = 16u * 1024u;   ///< PSRAM, SPSC
constexpr uint32_t    kStallTimeoutMs = 60000;         ///< our own last-byte timer
constexpr uint32_t    kTaskStackBytes = 8192;
constexpr UBaseType_t kTaskPriority   = 1;             ///< at/below the loop task
constexpr BaseType_t  kTaskCore       = 1;             ///< Wi-Fi is pinned to core 0
constexpr size_t      kTailBytes      = 240;           ///< bounded error-body tail
constexpr size_t      kMinInternalRam = 40u * 1024u;   ///< TLS needs internal SRAM

class DeviceTransport final : public ai::AiTransport {
public:
    ~DeviceTransport() override { close(); }

    bool open(const ai::AiConfig& cfg, const ai::AiRequest& req) override {
        close();

        // Every prerequisite failure is NAMED. An unsynced clock otherwise shows
        // up as an opaque mbedTLS "certificate expired" at handshake time.
        if (!Wifi::state().connected) { _err = "Wi-Fi not connected"; return false; }
        if (!timeSynced()) {
            _err = "clock not set (no NTP) - TLS cannot validate certificates";
            return false;
        }
        if (cfg.apiKey.empty()) { _err = "no API key"; return false; }

        std::string url = cfg.baseUrl;
        while (!url.empty() && url.back() == '/') url.pop_back();
        url += "/chat/completions";

        _req           = HttpReq{};
        _req.url       = url;
        _req.body      = &req.body;        // AiSession owns it; consumed inside open()
        _req.bearer    = cfg.apiKey;       // the ONLY copy of the key here
        _req.userAgent = "NumOS/1.0";
        _req.timeoutMs = cfg.timeoutMs;

        // Failing with "low memory" here beats an mbedTLS error code no user can
        // act on.
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < kMinInternalRam) {
            _err = "low internal RAM for a TLS session";
            return false;
        }

        _ring = static_cast<char*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM));
        if (!_ring) { _err = "ring buffer allocation failed (PSRAM)"; return false; }
        _wr.store(0, std::memory_order_relaxed);
        _rd.store(0, std::memory_order_relaxed);

        if (!_stream.open(_req)) { _err = _stream.error(); return false; }
        _http = (uint32_t)_stream.status();

        _cancel.store(false);
        _finished.store(false);
        _taskDone.store(false);
        _eof    = false;
        _stall  = false;
        _bytes  = 0;
        _tail.clear();

        if (xTaskCreatePinnedToCore(&DeviceTransport::workerTrampoline, "ai_http",
                                    kTaskStackBytes, this, kTaskPriority, &_task,
                                    kTaskCore) != pdPASS) {
            _task = nullptr;
            _err  = "worker task create failed";
            close();
            return false;
        }
        return true;
    }

    bool poll(std::string& out) override {
        out.clear();
        const size_t r = _rd.load(std::memory_order_acquire);
        const size_t w = _wr.load(std::memory_order_acquire);
        if (r != w) {
            if (w > r) {
                out.assign(_ring + r, w - r);
            } else {
                out.assign(_ring + r, kRingBytes - r);
                out.append(_ring, w);
            }
            _rd.store(w, std::memory_order_release);
        }

        if (!out.empty()) {
            // Keep a bounded tail so a 4xx/5xx is diagnosable. This is the
            // provider's own error JSON; it never contains the key.
            _tail += out;
            if (_tail.size() > kTailBytes) _tail.erase(0, _tail.size() - kTailBytes);
            return true;
        }

        if (_finished.load(std::memory_order_acquire)) {
            diagnose();
            return false;
        }
        return true;   // worker alive, nothing buffered yet
    }

    void close() override {
        _cancel.store(true);

        // Aborting the socket is what unblocks the worker's read().
        _stream.close();

        const uint32_t t0 = millis();
        while (!_taskDone.load(std::memory_order_acquire) && (millis() - t0) < 5000u) {
            vTaskDelay(1);
        }

        if (_task && !_taskDone.load(std::memory_order_acquire)) {
            // A hung socket must not leak the task. The ring is deliberately NOT
            // freed in this path: the doomed task may still be writing into it.
            Serial.println("[AI] WARN: device worker task did not join; leaking ring");
            vTaskDelete(_task);
            _leakedRing = true;
        } else if (_ring && !_leakedRing) {
            heap_caps_free(_ring);
        }
        if (!_leakedRing) _ring = nullptr;
        _task = nullptr;

        _req.bearer.clear();     // the key's own copy goes with it
        _req.body = nullptr;

        _finished.store(true);
    }

    const char* name() const override { return "device(wifi+tls)"; }

    std::string error() const override {
        if (!_err.empty()) return _err;
        if (_stall) return "stream stalled (no bytes for 60 s)";
        if (_http >= 400) {
            std::string s = "HTTP " + std::to_string(_http);
            if (!_tail.empty()) s += ": " + _tail;
            return s;
        }
        return {};
    }

    // ── diagnostics worth having from day one (§1.5) ────────────────────────
    struct Diag {
        uint32_t connectMs = 0;
        uint32_t headerMs  = 0;
        uint32_t bytes     = 0;
        bool     stalled   = false;
    };
    Diag diag() const {
        Diag d;
        d.connectMs = _stream.connectMs();
        d.headerMs  = _stream.headerMs();
        d.bytes     = _bytes;
        d.stalled   = _stall;
        return d;
    }

private:
    static void workerTrampoline(void* arg) {
        static_cast<DeviceTransport*>(arg)->worker();
    }

    void worker() {
        char          buf[1460];
        uint32_t      lastByteMs = millis();
        const uint32_t startedMs = lastByteMs;

        while (!_cancel.load(std::memory_order_relaxed)) {
            // The stall rule is OUR timer, reset on every byte: a slow-but-alive
            // stream is not a stall, and timeout_ms is only a socket timeout.
            if ((uint32_t)(millis() - lastByteMs) > kStallTimeoutMs) {
                _stall = true;
                break;
            }

            const int n = _stream.read(buf, sizeof(buf));
            if (n <= 0) {
                // SSE has no Content-Length: read() <= 0 is the only end signal.
                _http = (uint32_t)_stream.status();
                _eof  = true;
                break;
            }

            lastByteMs = millis();
            _bytes += (uint32_t)n;

            for (int i = 0; i < n; ++i) {
                if (!pushByte(buf[i])) { _cancel.store(true); break; }
            }
        }

        // Name a connect that never produced a byte, so the app never limps to a
        // silent empty answer.
        if (!_eof && !_stall && _cancel.load(std::memory_order_relaxed)) _aborted = true;
        (void)startedMs;

        _finished.store(true, std::memory_order_release);
        _taskDone.store(true, std::memory_order_release);
        vTaskDelete(nullptr);
    }

    /// One byte into the SPSC ring; false = cancelled while waiting for space.
    bool pushByte(char c) {
        for (int spin = 0; spin < 4000; ++spin) {
            const size_t w  = _wr.load(std::memory_order_relaxed);
            const size_t nw = (w + 1) % kRingBytes;
            if (nw != _rd.load(std::memory_order_acquire)) {
                _ring[w] = c;
                _wr.store(nw, std::memory_order_release);
                return true;
            }
            vTaskDelay(1);   // reader is behind: never spin the CPU
        }
        return false;
    }

    void diagnose() {
        if (!_err.empty()) return;
        if (_stall) { _err = "stream stalled (no bytes for 60 s)"; return; }
        if (_http >= 400) {
            _err = "HTTP " + std::to_string(_http);
            if (!_tail.empty()) _err += ": " + _tail;
            return;
        }
        if (_aborted) { _err = "request aborted"; return; }
    }

    HttpStream          _stream;
    HttpReq             _req;
    std::string         _err;
    std::string         _tail;

    char*               _ring       = nullptr;
    std::atomic<size_t> _wr{0};       ///< producer cursor (worker task)
    std::atomic<size_t> _rd{0};       ///< consumer cursor (main loop)
    std::atomic<bool>   _cancel{false};
    std::atomic<bool>   _finished{false};
    std::atomic<bool>   _taskDone{false};

    TaskHandle_t        _task       = nullptr;
    uint32_t            _http       = 0;
    uint32_t            _bytes      = 0;
    bool                _eof        = false;
    bool                _stall      = false;
    bool                _aborted    = false;
    bool                _leakedRing = false;
};

}  // namespace

ai::AiTransport* makeDeviceTransport(const ai::AiConfig&) {
    return new DeviceTransport();
}

}  // namespace net

#endif  // ARDUINO
