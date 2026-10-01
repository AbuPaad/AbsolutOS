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

    if (_url.empty()) { _err = "empty url"; return false; }

    esp_http_client_config_t cfg = {};
    cfg.url                   = _url.c_str();
    cfg.method                = HTTP_METHOD_POST;
    cfg.timeout_ms            = r.timeoutMs;
    cfg.buffer_size           = r.headerBufSize;
    cfg.buffer_size_tx        = 1024;
    cfg.disable_auto_redirect = true;
    cfg.keep_alive_enable     = true;      // reuse across retries; OpenRouter is behind a CDN
    cfg.user_agent            = _userAgent.c_str();
    cfg.crt_bundle_attach     = esp_crt_bundle_attach;   // never setInsecure()
    cfg.user_data             = &_timing;
    cfg.event_handler         = &onHttpEvent;

    _h = esp_http_client_init(&cfg);
    if (!_h) { _err = "esp_http_client_init failed"; return false; }

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
    if (esp_http_client_open(hc(_h), bodyLen) != ESP_OK) {
        _err = "esp_http_client_open failed";
        close();
        return false;
    }

    for (int off = 0; off < bodyLen; ) {
        const int n = esp_http_client_write(hc(_h), _body->data() + off, bodyLen - off);
        if (n <= 0) {
            _err = "short write while sending the body";
            close();
            return false;
        }
        off += n;
    }

    // Consumes the status line + response headers. SSE has no Content-Length, so
    // a 0 here is normal and get_content_length() is useless.
    (void)esp_http_client_fetch_headers(hc(_h));
    if (_timing.headerMs == 0) _timing.headerMs = millis() - _timing.t0;

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
int  HttpStream::read(char*, size_t)       { return -1; }
void HttpStream::close()                   {}

}  // namespace net

#endif  // ARDUINO
