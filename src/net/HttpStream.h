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
 * net/HttpStream.h — one streaming HTTPS POST, no whole-body buffering.
 *
 * The header stays Arduino-free: the esp_http_client handle is held as void*,
 * the timing block is a plain POD, and everything platform-specific lives in
 * HttpStream.cpp.
 *
 * Deliberate choices (device-networking.md §1.4):
 *  - `esp_crt_bundle_attach` for validation. Never setInsecure(), never pin a
 *    leaf or root — CA rotation would turn into a dead device.
 *  - NOT `esp_http_client_perform()`: that blocks the calling task through the
 *    whole exchange and gives no streaming control. Consequence: 3xx following
 *    (which lives inside perform()'s loop in IDF) is done by open() itself, hop
 *    by hop, only when HttpReq::followRedirects asks for it.
 *  - No silent hop for the AI path: followRedirects defaults to false, and a 3xx
 *    is then left as the final status for the caller to report.
 *  - SSE has no Content-Length, so end detection is read() <= 0 /
 *    esp_http_client_is_complete_data_received().
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace net {

struct HttpReq {
    std::string        url;
    std::string        method      = "POST";
    /** POST body. Must outlive open(); it is written to the socket inside. */
    const std::string* body        = nullptr;
    std::string        contentType = "application/json";
    std::string        accept      = "text/event-stream";
    /** Bearer token. Empty = no Authorization header at all. */
    std::string        bearer;
    std::string        userAgent   = "NumOS/1.0";
    int                timeoutMs   = 60000;   ///< socket timeout, NOT the stall rule
    int                headerBufSize = 2048;  ///< 512 (default) is too small for headers
    /**
     * Chase 3xx hops. OFF for the AI path on purpose (a silent hop is an
     * availability/security surprise). ON for OTA: our own domain 302s to
     * GitHub's `releases/latest`, which 302s again to a signed CDN host, so both
     * the version check and the download must chase them. Chased only for a
     * bodiless request, and never from https down to http.
     */
    bool               followRedirects = false;
};

class HttpStream {
public:
    /** Timing + redirect capture, carried into the http event handler; public so
     *  the .cpp can use it. */
    struct Timing {
        uint32_t t0        = 0;
        uint32_t connectMs = 0;
        uint32_t headerMs  = 0;
        /**
         * The last response's Location header, captured by the event handler.
         * Not a convenience: response headers have no other public accessor
         * (esp_http_client_get_header() reads the REQUEST headers), and IDF 4.4
         * follows 3xx only inside esp_http_client_perform(), which this class
         * does not call. Without it, open() sees a bare status code and a
         * redirect is indistinguishable from a final answer.
         */
        std::string location;
    };

    ~HttpStream();

    /** init → headers → open(len) → write body in chunks → fetch_headers. */
    bool open(const HttpReq& r);

    /// HTTP status, 0 before the response line arrives.
    int  status() const;

    /// Chunked/streamed responses have no length; ask the client instead.
    bool complete() const;

    /// Response Content-Length, or -1 when absent (chunked/streamed). For OTA
    /// progress only.
    long contentLength() const;

    /// > 0 bytes read, 0 or negative = end (or failure).
    int  read(char* buf, size_t n);

    /// Idempotent. Deliberately does NOT clear error(): callers report after close().
    void close();

    const std::string& error() const { return _err; }

    /// Diagnostics: connect ms, time-to-first-header (device-networking.md §1.5).
    uint32_t connectMs() const { return _timing.connectMs; }
    uint32_t headerMs()  const { return _timing.headerMs; }

private:
    /// One hop: init → headers → open(len) → write body in chunks →
    /// fetch_headers. Sets _err and closes on failure. `hop` is for logging.
    /// Called in a loop by open(); it never follows a redirect itself.
    bool sendOnce(const HttpReq& r, int bodyLen, int hop);

    void*       _h         = nullptr;   ///< esp_http_client_handle_t
    /// True between a successful tlsSessionAcquire() and close(); guarantees the
    /// one-and-only TLS slot is given back exactly once, from any task.
    bool        _tlsHeld   = false;
    const std::string* _body = nullptr; ///< borrowed; only used inside open()
    std::string _url;
    std::string _userAgent;
    std::string _err;
    Timing      _timing;
};

}  // namespace net
