/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/** net/HttpStream.cpp — the Arduino half of net/HttpStream.h. */

#include "net/HttpStream.h"

#if defined(ARDUINO)

#include <Arduino.h>
#include <esp_err.h>
#include <esp_http_client.h>

#include <cstdio>
#include <netdb.h>
#include <time.h>
#include <esp_heap_caps.h>

#include "net/Clock.h"       // the clock is a dependency of TLS, not a sibling
#include "net/TlsSession.h"  // the ONE TLS memory budget + session serialisation

// ── TLS trust: the esp_crt_bundle.h ambiguity ───────────────────────────────
// This framework ships TWO files named esp_crt_bundle.h behind the SAME include
// guard (_ESP_CRT_BUNDLE_H_), and they declare DIFFERENT entry points:
//   * IDF:  <sdk>/<chip>/include/mbedtls/esp_crt_bundle/include/esp_crt_bundle.h
//           → esp_err_t esp_crt_bundle_attach(void*);
//   * Core: libraries/WiFiClientSecure/src/esp_crt_bundle.h
//           → esp_err_t arduino_esp_crt_bundle_attach(void*);
// Whichever include dir comes first wins. In a full firmware build it is
// WiFiClientSecure's (PlatformIO's LDF puts that src dir on the path), which
// leaves esp_crt_bundle_attach undeclared and the build dead:
//   error: 'esp_crt_bundle_attach' was not declared in this scope
//            note: suggested alternative: 'arduino_esp_crt_bundle_attach'
// So do not depend on include-path order — name the IDF entry point directly.
// That is a second, identical declaration when the IDF header won (legal C++)
// and the only one when the Arduino header did, in which case the file is not
// included at all and there is no guard to clash with. The symbol always links:
// it lives in libmbedtls.a, and the Arduino wrapper calls straight through.
//
// This is the bundle, NOT setInsecure() and NOT a pinned root — the bundle is
// the validation policy (device-networking.md §1.3).
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace net {
namespace {

/**
 * The header holds the handle as void* on purpose, so it stays Arduino-free (no
 * esp_http_client.h above the HAL line). C++ will not implicitly convert a void*
 * back to a typed pointer, so every call site goes through this.
 */
inline esp_http_client_handle_t hc(void* p) {
    return static_cast<esp_http_client_handle_t>(p);
}

/// The host component of an absolute URL ("scheme://[user@]host[:port]/path").
/// Used only on the failure path, to tell a name-resolution failure from a
/// transport/certificate one.
std::string hostOf(const std::string& url) {
    const size_t s = url.find("://");
    const size_t b = (s == std::string::npos) ? 0 : s + 3;
    size_t e = url.find('/', b);
    if (e == std::string::npos) e = url.size();
    const size_t at = url.rfind('@', e);          // drop any user:pass@
    const size_t h  = (at != std::string::npos && at >= b) ? at + 1 : b;
    const size_t c  = url.find(':', h);           // drop any :port
    if (c != std::string::npos && c < e) e = c;
    return url.substr(h, e - h);
}

/// Verbose on purpose: a failure this deep has no room on the device's own
/// screen. Only the request line is logged — the Authorization header is never,
/// and no endpoint used here carries a credential in the URL.
void logFail(const char* what, const std::string& detail) {
    Serial.printf("[NET] %s: %s\n", what, detail.c_str());
}

/// Map the header's method string onto the esp_http_client enum. Unknown strings
/// (including the default when a caller forgets to set one) keep POST so the AI
/// path is bit-for-bit unchanged.
esp_http_client_method_t methodFromString(const std::string& method) {
    if (method == "GET")  return HTTP_METHOD_GET;
    if (method == "PUT")  return HTTP_METHOD_PUT;
    if (method == "HEAD") return HTTP_METHOD_HEAD;
    return HTTP_METHOD_POST;
}

/**
 * Diagnostic only. Streaming from here is the classic bug: data events arrive on
 * the HTTP task, and any UI call from this thread is a race (trap 2).
 */
esp_err_t onHttpEvent(esp_http_client_event_t* e) {
    auto* t = static_cast<HttpStream::Timing*>(e->user_data);
    if (!t) return ESP_OK;
    switch (e->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            t->connectMs = millis() - t->t0;
            break;
        case HTTP_EVENT_ON_HEADER:
            if (t->headerMs == 0) t->headerMs = millis() - t->t0;
            break;
        default:
            break;
    }
    return ESP_OK;
}

}  // namespace

HttpStream::~HttpStream() { close(); }

bool HttpStream::open(const HttpReq& r) {
    close();
    _url       = r.url;
    _userAgent = r.userAgent;
    _body      = r.body;
    _timing    = Timing{};
    _timing.t0 = millis();

    if (_url.empty()) { _err = "empty url"; logFail("http", _err); return false; }

    // ONE canonical TLS gate, checked while the one-and-only session slot is
    // held (net/TlsSession.h). Serialising FIRST is the fix for the real bug:
    // the boot OTA auto-check (core 0) and an AI request (core 1) used to hold
    // 2x16 KB each, at the same time, and the per-session floor could not see
    // the other session. Now the floor is checked against the heap this session
    // will actually get, and the numbers (not an errno 0) come back by name.
    if (_url.compare(0, 6, "https:") == 0) {
        // Bounded, so no caller can hang indefinitely behind a long session (an
        // OTA firmware download can hold the slot for minutes). Past the bound
        // the request fails WITH A NAME instead of stalling the caller.
        const uint32_t waitMs = (r.timeoutMs > 0 && (uint32_t)r.timeoutMs < 15000u)
                                    ? (uint32_t)r.timeoutMs : 15000u;
        if (!tlsSessionAcquire(waitMs)) {
            _err = "another TLS session is active (sessions are serialised so "
                   "mbedTLS fits in internal RAM)";
            logFail("http", _err);
            close();
            return false;
        }
        _tlsHeld = true;

        const TlsMemReport m = tlsMemoryCheck();
        if (!m.ok) {
            char why[224];
            std::snprintf(why, sizeof(why),
                          "low internal RAM for TLS handshake: free %uK (need %uK), "
                          "largest %uK (need %uK) [%s]",
                          static_cast<unsigned>(m.freeBytes / 1024u),
                          static_cast<unsigned>(kTlsMinInternalFree / 1024u),
                          static_cast<unsigned>(m.largestBytes / 1024u),
                          static_cast<unsigned>(kTlsMinInternalLargest / 1024u),
                          _url.c_str());
            _err = why;
            logFail("http", _err);
            close();
            return false;
        }
    }

    esp_http_client_config_t cfg = {};
    cfg.url                   = _url.c_str();
    cfg.method                = methodFromString(r.method);
    cfg.timeout_ms            = r.timeoutMs;
    cfg.buffer_size           = r.headerBufSize;
    cfg.buffer_size_tx        = 1024;
    cfg.disable_auto_redirect = !r.followRedirects;
    cfg.keep_alive_enable     = false;     // one handle per request: tear mbedTLS down deterministically
    cfg.user_agent            = _userAgent.c_str();
    cfg.crt_bundle_attach     = esp_crt_bundle_attach;   // never setInsecure()
    cfg.user_data             = &_timing;
    cfg.event_handler         = &onHttpEvent;

    _h = esp_http_client_init(&cfg);
    if (!_h) { _err = "esp_http_client_init failed"; logFail("http", _err); return false; }

    Serial.printf("[NET] http %s %s (body %d, timeout %ums, hdr buf %u)\n",
                  r.method.empty() ? "POST" : r.method.c_str(), _url.c_str(),
                  (int)(_body ? _body->size() : 0), (unsigned)r.timeoutMs,
                  (unsigned)r.headerBufSize);

    if (!r.contentType.empty())
        esp_http_client_set_header(hc(_h), "Content-Type", r.contentType.c_str());
    if (!r.accept.empty())
        esp_http_client_set_header(hc(_h), "Accept", r.accept.c_str());

    // The key rides in this header and nowhere else: never in the body, never in
    // a log line, never in an error string. set_header copies the value.
    if (!r.bearer.empty()) {
        std::string auth = "Bearer ";
        auth += r.bearer;
        esp_http_client_set_header(hc(_h), "Authorization", auth.c_str());
    }

    const int bodyLen = _body ? (int)_body->size() : 0;
    const esp_err_t oerr = esp_http_client_open(hc(_h), bodyLen);
    if (oerr != ESP_OK) {
        // Three very different faults fold into this one call, and the errno is
        // only ever filled from the SOCKET layer (transport.c fills sock_errno
        // from ESP_TLS_ERR_TYPE_SYSTEM alone), so it reads 0 for a certificate
        // failure AND for a DNS failure — it cannot classify anything. Separate
        // them explicitly instead: "connect failed" alone has cost days here.
        char why[256];
        const int sockErrno = esp_http_client_get_errno(hc(_h));
        const std::string host = hostOf(_url);

        if (!timeSynced()) {
            // The class guard: whatever a caller forgot to check, an HTTPS
            // request on a 1970 clock can only fail in certificate validation.
            std::snprintf(why, sizeof(why),
                          "clock not set (no NTP): TLS cannot validate certificates [%s]",
                          _url.c_str());
        } else {
            struct addrinfo hints = {};
            hints.ai_family   = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo* res = nullptr;
            const int gai = host.empty() ? -1 : getaddrinfo(host.c_str(), nullptr, &hints, &res);
            if (res) freeaddrinfo(res);
            if (gai != 0) {
                std::snprintf(why, sizeof(why),
                              "DNS: cannot resolve '%s' (getaddrinfo %d) [%s]",
                              host.c_str(), gai, _url.c_str());
            } else {
                std::snprintf(why, sizeof(why),
                              "esp_http_client_open failed: %s (errno %d; 0 = TLS handshake/cert, name resolved, clock %ld) [%s]",
                              esp_err_to_name(oerr), sockErrno, (long)time(nullptr), _url.c_str());
            }
        }
        _err = why;
        logFail("http", _err);
        close();
        return false;
    }

    for (int off = 0; off < bodyLen; ) {
        const int n = esp_http_client_write(hc(_h), _body->data() + off, bodyLen - off);
        if (n <= 0) {
            _err = "short write while sending the body";
            logFail("http", _err);
            close();
            return false;
        }
        off += n;
    }

    // Consumes the status line + response headers. SSE has no Content-Length, so
    // a 0 here is normal and get_content_length() is useless.
    (void)esp_http_client_fetch_headers(hc(_h));
    if (_timing.headerMs == 0) _timing.headerMs = millis() - _timing.t0;

    Serial.printf("[NET] http status %d (headers %ums)\n",
                  esp_http_client_get_status_code(hc(_h)), (unsigned)_timing.headerMs);

    // A >=400 is deliberately NOT an open failure: the provider's explanation is
    // the response body, and it is drained by the caller for the error string.
    return true;
}

int HttpStream::status() const {
    return _h ? esp_http_client_get_status_code(hc(_h)) : 0;
}

bool HttpStream::complete() const {
    return _h ? esp_http_client_is_complete_data_received(hc(_h)) : true;
}

long HttpStream::contentLength() const {
    return _h ? static_cast<long>(esp_http_client_get_content_length(hc(_h))) : -1;
}

int HttpStream::read(char* buf, size_t n) {
    if (!_h || !buf || n == 0) return -1;
    return esp_http_client_read(hc(_h), buf, (int)n);
}

void HttpStream::close() {
    if (_h) {
        esp_http_client_close(hc(_h));
        esp_http_client_cleanup(hc(_h));
        _h = nullptr;
    }
    // Give the one TLS slot back only AFTER the socket is torn down:
    // esp_http_client_cleanup() is what frees mbedTLS's two 16 KB content
    // buffers, so releasing first would let the next session start while those
    // blocks are still committed.
    if (_tlsHeld) { tlsSessionRelease(); _tlsHeld = false; }
    _body = nullptr;
    _url.clear();
    _userAgent.clear();
}

}  // namespace net

#else

namespace net {

HttpStream::~HttpStream() {}
bool HttpStream::open(const HttpReq&)      { return false; }
int  HttpStream::status() const            { return 0; }
bool HttpStream::complete() const          { return true; }
long HttpStream::contentLength() const     { return -1; }
int  HttpStream::read(char*, size_t)       { return -1; }
void HttpStream::close()                   {}

}  // namespace net

#endif  // ARDUINO
