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
 * WolframClient.cpp — see the header for the contract, the licence boundary and
 * the reasons. The pure half (URL build, error classification, body parse) has
 * no socket and is covered by tests/host/wolfram_client_test.cpp.
 *
 * Like AiClient.cpp: LVGL-free, so a host test compiles this file with
 * `g++ -I src` alone. The two REAL fetches (device = WiFi + TLS, emulator =
 * libcurl) are compiled per target; a build with neither gets a named refusal
 * rather than a silent stub.
 */

#include "ai/WolframClient.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <esp_heap_caps.h>
  // The filesystem GLOBALS are Arduino-only names. hal/FileSystem.h gives the
  // HOST shim; on device `LittleFS` comes from the core's FS headers instead.
  // Without these the native build compiles clean and the FIRMWARE build dies on
  // `'LittleFS' was not declared in this scope` — which is exactly what happened
  // the first time this file was built for the board. An emulator build does not
  // prove a firmware build for anything that touches the filesystem.
  #include <FS.h>
  #include <LittleFS.h>
#endif

#if defined(NUMOS_HAVE_LIBCURL)
  #include <curl/curl.h>
#endif

#if defined(ARDUINO)
  #include "net/Clock.h"
  #include "net/HttpStream.h"
  #include "net/Wifi.h"
#endif

#include "hal/FileSystem.h"

namespace ai {

// ═══════════════════════════════════════════════════════════════════════════
// Small string helpers (internal linkage — AiClient.cpp has its own copies)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string rtrimCopy(std::string s) {
    // '\n' is in here as well as ' ': a body of nothing but newlines must read as
    // EMPTY, or classifyWolfram() calls it Ok and the parse then yields a blank
    // page instead of a named failure.
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::string ltrimCopy(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string trimCopy(const std::string& s) { return ltrimCopy(rtrimCopy(s)); }

bool blank(const std::string& s) { return trimCopy(s).empty(); }

/// Case-insensitive prefix test.
bool startsWithCI(const std::string& s, const char* prefix) {
    const std::string p = lower(prefix);
    if (s.size() < p.size()) return false;
    return lower(s.substr(0, p.size())) == p;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

/**
 * WA's `image:` / `Images:` lines. The device's Markdown whitelist bans images,
 * so these can never be drawn — and a bare URL would be drawn as gibberish.
 */
bool isImageLine(const std::string& t) {
    return startsWithCI(t, "image:") || startsWithCI(t, "images:");
}

/**
 * A WA heading is a short label ending in ':' with no '|' in it. The length and
 * pipe guards exist so a content line that happens to end in a colon is not
 * promoted to a section.
 */
bool isHeading(const std::string& t) {
    if (t.size() < 2 || t.size() > 64) return false;
    if (t.back() != ':') return false;
    if (t.find('|') != std::string::npos) return false;
    if (isImageLine(t)) return false;
    return true;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// WaBuffer
// ═══════════════════════════════════════════════════════════════════════════

bool WaBuffer::reserve(size_t bytes) {
    reset();
    if (bytes == 0) return false;
#if defined(ARDUINO)
    // PSRAM, deliberately: the internal heap carries the mbedTLS content buffers
    // and a result body parked there is how a handshake starts failing.
    _p = static_cast<char*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
#else
    _p = static_cast<char*>(std::malloc(bytes));
#endif
    if (!_p) return false;
    _cap = bytes;
    _len = 0;
    return true;
}

bool WaBuffer::append(const char* d, size_t n) {
    if (n == 0) return true;
    if (!_p || !d) return false;
    if (_len >= _cap) return false;
    const size_t room = _cap - _len;
    const size_t take = (n < room) ? n : room;
    std::memcpy(_p + _len, d, take);
    _len += take;
    return take == n;
}

void WaBuffer::reset() {
    if (_p) {
#if defined(ARDUINO)
        heap_caps_free(_p);
#else
        std::free(_p);
#endif
    }
    _p   = nullptr;
    _cap = 0;
    _len = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Request
// ═══════════════════════════════════════════════════════════════════════════

std::string percentEncode(const std::string& in) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (const char ch : in) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) { out.push_back(static_cast<char>(c)); continue; }
        out.push_back('%');
        out.push_back(kHex[(c >> 4) & 0x0F]);
        out.push_back(kHex[c & 0x0F]);
    }
    return out;
}

std::string buildWolframUrl(const AiConfig& cfg, const std::string& input) {
    std::string url = cfg.waHost;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += cfg.waPath;
    url += "?input=";
    url += percentEncode(input);
    if (cfg.waMaxChars > 0) {
        url += "&maxchars=";
        url += std::to_string(cfg.waMaxChars);
    }
    // units / location / languagecode are CONFIG, never model output. The model
    // proposing them would let it shape a request the user did not ask for, and
    // one bad maxchars spends the month's allowance.
    if (!cfg.waUnits.empty())    { url += "&units=";        url += percentEncode(cfg.waUnits); }
    if (!cfg.waLocation.empty()) { url += "&location=";     url += percentEncode(cfg.waLocation); }
    if (!cfg.waLanguage.empty()) { url += "&languagecode="; url += percentEncode(cfg.waLanguage); }
    return url;
}

// ── The digits-only query edit (the check screen's model) ──────────────────

void WaQueryEdit::reset(const std::string& proposed) {
    _original = proposed;
    _text     = proposed;
    _caret    = static_cast<int>(_text.size());
}

bool WaQueryEdit::moveLeft() {
    if (_caret <= 0) return false;
    --_caret;
    return true;
}

bool WaQueryEdit::moveRight() {
    if (_caret >= static_cast<int>(_text.size())) return false;
    ++_caret;
    return true;
}

bool WaQueryEdit::insert(char c) {
    // Digits only: the keypad has no letters, so any other character is a key
    // that means something else on this screen.
    if (c < '0' || c > '9') return false;
    if (_text.size() >= kMaxChars) return false;
    if (_caret < 0) _caret = 0;
    if (_caret > static_cast<int>(_text.size())) _caret = static_cast<int>(_text.size());
    _text.insert(_text.begin() + _caret, c);
    ++_caret;
    return true;
}

bool WaQueryEdit::backspace() {
    if (_caret <= 0 || _text.empty()) return false;
    if (_caret > static_cast<int>(_text.size())) _caret = static_cast<int>(_text.size());
    _text.erase(_text.begin() + (_caret - 1));
    --_caret;
    return true;
}

void WaQueryEdit::restore() {
    _text  = _original;
    _caret = static_cast<int>(_text.size());
}

// ═══════════════════════════════════════════════════════════════════════════
// Errors
// ═══════════════════════════════════════════════════════════════════════════

const char* waStatusName(WaStatus s) {
    switch (s) {
        case WaStatus::Ok:               return "ok";
        case WaStatus::MissingAppId:     return "appid-missing";
        case WaStatus::InvalidAppId:     return "appid-invalid";
        case WaStatus::NotInterpretable: return "not-interpretable";
        case WaStatus::Forbidden:        return "forbidden";
        case WaStatus::RateLimited:      return "rate-limited";
        case WaStatus::BadRequest:       return "bad-request";
        case WaStatus::ServerError:      return "server-error";
        case WaStatus::EmptyBody:        return "empty-body";
        case WaStatus::TooLarge:         return "too-large";
        case WaStatus::Transport:        return "transport";
    }
    return "unknown";
}

const char* waStatusText(WaStatus s) {
    switch (s) {
        case WaStatus::Ok:               return "ok";
        case WaStatus::MissingAppId:     return "no Wolfram AppID was sent";
        case WaStatus::InvalidAppId:     return "the Wolfram AppID was rejected";
        case WaStatus::NotInterpretable: return "Wolfram|Alpha could not read that query";
        case WaStatus::Forbidden:        return "the request was refused";
        case WaStatus::RateLimited:      return "Wolfram|Alpha is rate limiting (slow down)";
        case WaStatus::BadRequest:       return "the request was rejected";
        case WaStatus::ServerError:      return "Wolfram|Alpha returned a server error";
        case WaStatus::EmptyBody:        return "Wolfram|Alpha returned nothing usable";
        case WaStatus::TooLarge:         return "the result exceeded the size cap";
        case WaStatus::Transport:        return "the request never reached Wolfram|Alpha";
    }
    return "unknown failure";
}

WaStatus classifyWolfram(int http, const std::string& body, std::string* msgOut) {
    if (msgOut) {
        msgOut->clear();
        if (!body.empty()) {
            const size_t n = std::min<size_t>(body.size(), 180);
            std::string t = body.substr(body.size() - n);
            for (char& c : t) if (c == '\n' || c == '\r') c = ' ';
            *msgOut = t;
        }
    }

    // The BODY decides. The published table is stale: it claims 403 for both
    // appid errors (never observed in any probe) and describes 400 as "no input
    // parameter", when 400 is in fact the missing-appid case. Live, the two
    // appid failures are 13 bytes of plain text — no JSON to key off.
    if (startsWithCI(trimCopy(body), "appid missing")) return WaStatus::MissingAppId;
    if (startsWithCI(trimCopy(body), "invalid appid")) return WaStatus::InvalidAppId;

    if (http == 0)   return WaStatus::Transport;
    if (http == 501) return WaStatus::NotInterpretable;
    if (http == 429) return WaStatus::RateLimited;
    if (http == 400) return WaStatus::MissingAppId;
    if (http == 401) return WaStatus::InvalidAppId;
    if (http == 403) return WaStatus::Forbidden;
    if (http >= 500) return WaStatus::ServerError;
    if (http >= 400) return WaStatus::BadRequest;

    if (trimCopy(body).empty()) return WaStatus::EmptyBody;
    return WaStatus::Ok;
}

// ═══════════════════════════════════════════════════════════════════════════
// Response parse
// ═══════════════════════════════════════════════════════════════════════════

WolframResult parseWolframBody(const std::string& raw) {
    WolframResult r;
    std::vector<std::string> lines = splitLines(raw);

    // ── 1. The trailing attribution is TWO lines: the label, then the URL. ──
    // Matching only the label strands the URL as stray body text, and the URL is
    // the one piece of the response the check document is allowed to keep.
    for (size_t i = 0; i < lines.size(); ++i) {
        if (startsWithCI(trimCopy(lines[i]), "wolfram|alpha website result")) {
            for (size_t j = i + 1; j < lines.size(); ++j) {
                const std::string u = trimCopy(lines[j]);
                if (u.empty()) continue;
                if (u.rfind("http", 0) == 0) r.link = u;
                break;
            }
            lines.erase(lines.begin() + static_cast<long>(i), lines.end());
            break;
        }
    }

    // ── 2. Group into blocks: a heading line opens one, content follows. ────
    struct Block {
        std::string heading;
        std::vector<std::string> content;
    };
    std::vector<Block> blocks;
    Block cur;
    bool started = false;

    for (const std::string& ln : lines) {
        const std::string t = trimCopy(ln);
        if (isHeading(t)) {
            if (started || !cur.content.empty() || !cur.heading.empty()) blocks.push_back(cur);
            cur = Block{};
            cur.heading = t;
            started = true;
            continue;
        }
        if (blank(t)) continue;                  // blank runs collapsed: 176 px pages
        cur.content.push_back(rtrimCopy(ln));
    }
    if (started || !cur.content.empty() || !cur.heading.empty()) blocks.push_back(cur);

    // ── 3. Keep what the reader needs, drop what it cannot draw. ────────────
    std::string body;
    for (const Block& b : blocks) {
        // The body OPENS with `Query:` — the device already holds the query
        // verbatim, so this block is noise, not content.
        if (startsWithCI(b.heading, "query")) continue;

        std::vector<std::string> kept;
        bool hadImageOnly = !b.content.empty();
        for (const std::string& c : b.content) {
            if (isImageLine(trimCopy(c))) continue;   // the whitelist bans images
            kept.push_back(c);
            hadImageOnly = false;
        }
        // A section that held only images leaves an orphan heading. Drop it:
        // "Periodic table location:" over nothing is worse than its absence.
        if (kept.empty() && hadImageOnly) continue;

        if (startsWithCI(b.heading, "input interpretation")) {
            // The verification UX — what WA thinks was asked. Rendered apart
            // from the body, because this is the line the user judges.
            for (const std::string& k : kept) {
                if (!r.interpretation.empty()) r.interpretation += ' ';
                r.interpretation += trimCopy(k);
            }
            if (r.interpretation.empty()) r.interpretation = trimCopy(b.heading);
            continue;
        }

        if (!b.heading.empty()) { body += b.heading; body += '\n'; }
        for (const std::string& k : kept) { body += k; body += '\n'; }
        body += '\n';
    }
    while (!body.empty() && body.back() == '\n') body.pop_back();
    r.body = body;

    // A 501 body can carry suggested inputs and no interpretation. Fall back to
    // the raw body so the user still sees something rather than a blank page.
    if (r.body.empty() && r.interpretation.empty() && r.link.empty())
        r.body = trimCopy(raw);

    return r;
}

// ═══════════════════════════════════════════════════════════════════════════
// The check document — query, transcription, link. NEVER Wolfram's text.
// ═══════════════════════════════════════════════════════════════════════════

std::string buildWolframDoc(const std::string& sourceSlug,
                            const std::string& query,
                            const std::string& transcribed,
                            const std::string& link,
                            const std::string& model) {
    std::string out;
    out += "# Wolfram|Alpha check\n\n";
    out += "**Query:** ";
    out += query;
    out += "\n\n";

    if (!transcribed.empty()) {
        out += "_Read from the photo as:_ ";
        out += transcribed;
        out += "\n\n";
    }

    if (!link.empty()) {
        out += "Open the result on Wolfram|Alpha:\n\n";
        out += link;
        out += "\n\n";
    }

    out += "%%ai: kind=wolfram";
    // Hash of what WE wrote, never of WA's content: it only has to change when
    // the question or the transcription changes, so a re-press on the same query
    // does not rewrite the file.
    out += " hash=" + contentHash32(query + "\n" + transcribed + "\n" + link);
    if (!sourceSlug.empty()) out += " source=" + sourceSlug;
    if (!model.empty())      out += " model=" + model;
    out += "%%\n";
    return out;
}

namespace {

/// Pull our own `hash=` value back out of a document we built.
std::string hashOfDoc(const std::string& doc) {
    const size_t at = doc.find("hash=");
    if (at == std::string::npos) return {};
    size_t e = at + 5;
    while (e < doc.size() && doc[e] != ' ' && doc[e] != '%' && doc[e] != '\n') ++e;
    return doc.substr(at + 5, e - (at + 5));
}

}  // namespace

bool saveWolframDoc(const AiConfig& cfg, const std::string& slugBase,
                    const std::string& doc, std::string* pathOut) {
    if (doc.empty() || slugBase.empty()) return false;
    if (!ensureDir(cfg.resultsDir)) return false;

    const std::string path = cfg.resultsDir + "/" + slugBase + kWolframSlugSuffix + ".md";

    // Same content hash = nothing to do. ONE check file per answer, so a second
    // press overwrites rather than making _Wolfram2 — this is a check, not a log.
    const std::string hash = hashOfDoc(doc);
    if (!hash.empty()) {
        const std::string existing = readTextFile(path);
        if (existing.find("hash=" + hash) != std::string::npos) {
            if (pathOut) *pathOut = path;
            return true;
        }
    }

    // temp + rename: a half-written check is never readable.
    const std::string tmp = path + ".tmp";
    if (!writeTextFile(tmp, doc)) return false;
    if (LittleFS.rename(tmp.c_str(), path.c_str())) {
        if (pathOut) *pathOut = path;
        return true;
    }
    // LittleFS's rename() can refuse when the destination already exists — and it
    // always does on the second press. A plain overwrite beats reporting a failed
    // save for a write that clearly works.
    const bool ok = writeTextFile(path, doc);
    LittleFS.remove(tmp.c_str());
    if (ok && pathOut) *pathOut = path;
    return ok;
}

// ═══════════════════════════════════════════════════════════════════════════
// The fetch seam
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/**
 * One GET. `pump` returns false when the transfer is over (successfully or not)
 * and hands back whatever bytes arrived first.
 */
class WaFetch {
public:
    virtual ~WaFetch() = default;
    virtual bool open(const AiConfig& cfg, const std::string& url, std::string& err) = 0;
    virtual bool pump(std::string& out, std::string& err) = 0;
    virtual int  http() const = 0;
    virtual void close() = 0;
};

/** Refuses with a named reason instead of pretending to work. */
class StubFetch final : public WaFetch {
public:
    explicit StubFetch(std::string why) : _why(std::move(why)) {}
    bool open(const AiConfig&, const std::string&, std::string& err) override {
        err = _why;
        return false;
    }
    bool pump(std::string&, std::string&) override { return false; }
    int  http() const override { return 0; }
    void close() override {}
private:
    std::string _why;
};

#if defined(NUMOS_HAVE_LIBCURL)
/**
 * Replay: the recorded body for the same request, read from the run's own
 * filesystem — `/ai/replay/wolfram-llm-api.txt`, derived from the config's own
 * results dir so nothing new has to be configured.
 *
 * WHY THIS EXISTS. Every other transport in the AI path has a recorded-fixture
 * mode (the answer stream's `transport: "replay"`), because the interesting
 * failures and the happy path both have to be reachable without a network, a
 * key, or a bill. This is the same idea for the check hop: with
 * `transport: "replay"` the app drives ITSELF end to end — the confirm screen,
 * the live card, the pagination, the sibling `_Wolfram.md` — with no socket at
 * all and no Wolfram allowance spent.
 *
 * EMULATOR ONLY, and not because of a config value somebody could set: FIXTURES
 * ARE NOT ON THE DEVICE. The repo's fs root is the emulator's, the firmware
 * image carries no `/ai/replay`, and this whole class is compiled out of a
 * device build (the ARDUINO branch builds DeviceFetch and nothing else). A
 * shipping unit has no recorded body to find.
 *
 * The body is delivered in chunks so the live card has something to show, which
 * is also what the device path does.
 */
class ReplayFetch final : public WaFetch {
public:
    bool open(const AiConfig& cfg, const std::string&, std::string& err) override {
        close();
        _path = replayPath(cfg);
        _body = readTextFile(_path);
        if (_body.empty()) {
            err = "no recorded Wolfram body at " + _path;
            return false;
        }
        _at = 0;
        _http = 200;
        return true;
    }

    bool pump(std::string& out, std::string& err) override {
        (void)err;
        if (_at >= _body.size()) return false;
        const size_t n = std::min<size_t>(512, _body.size() - _at);
        out.assign(_body, _at, n);
        _at += n;
        // One extra pump after the last chunk ends the transfer, so the caller's
        // "more" flag goes false the way a real Content-Length body does.
        return _at < _body.size();
    }

    int  http() const override { return _http; }
    void close() override {
        _body.clear();
        _at = 0;
    }

    /// `/ai/results` -> `/ai/replay/wolfram-llm-api.txt`
    static std::string replayPath(const AiConfig& cfg) {
        std::string dir = cfg.resultsDir;
        while (!dir.empty() && dir.back() == '/') dir.pop_back();
        const size_t slash = dir.find_last_of('/');
        if (slash != std::string::npos) dir.erase(slash);
        return dir + "/replay/wolfram-llm-api.txt";
    }

private:
    std::string _path;
    std::string _body;
    size_t      _at   = 0;
    int         _http = 0;
};
#endif  // NUMOS_HAVE_LIBCURL

#if defined(ARDUINO)
/**
 * Device: WiFi + mbedTLS + esp_http_client, via the same net::HttpStream the
 * answer transport uses. GET, so there is no body to send.
 *
 * Unlike DeviceTransport there is NO worker task and no SPSC ring: a Wolfram
 * response is a single small body, not an unbounded stream, so pump() reads it
 * in bounded chunks. NOTE the consequence, which DeviceTransport does NOT have:
 * `open()` here is SYNCHRONOUS — it performs the TLS handshake and the header
 * fetch on the CALLING thread, so a caller on the UI task freezes the screen and
 * stalls the keypad scanner for the length of that handshake. Moving this onto a
 * worker task (the shape DeviceTransport::open already uses) is an open item;
 * do not describe `open()` as non-blocking before that lands.
 */
class DeviceFetch final : public WaFetch {
public:
    ~DeviceFetch() override { close(); }

    bool open(const AiConfig& cfg, const std::string& url, std::string& err) override {
        close();

        // Every prerequisite failure is NAMED: an unsynced clock otherwise shows
        // up as an opaque mbedTLS "certificate expired" at handshake time.
        if (!net::Wifi::state().connected) { err = "Wi-Fi not connected"; return false; }
        if (!net::timeSynced()) {
            err = "clock not set (no NTP) - TLS cannot validate certificates";
            return false;
        }
        if (cfg.waAppId.empty()) { err = "no Wolfram AppID configured"; return false; }

        // No memory pre-check here either: net::HttpStream::open() is the single
        // canonical TLS gate (net/TlsSession.h) and it also serialises this
        // session against the AI stream and the OTA check.

        _req              = net::HttpReq{};
        _req.url          = url;
        _req.method       = "GET";
        _req.body         = nullptr;
        _req.contentType.clear();          // a GET with no body has none
        _req.accept       = "text/plain";  // the LLM API's only real content type
        _req.bearer       = cfg.waAppId;   // the ONLY copy of the AppID here
        _req.userAgent    = "NumOS/1.0";
        _req.timeoutMs    = cfg.waTimeoutMs;
        _req.headerBufSize = 2048;
        // A silent 3xx hop is an availability and security surprise; surface it.
        _req.followRedirects = false;

        net::Wifi::setPowerSave(true);     // modem sleep adds DTIM latency per chunk

        if (!_stream.open(_req)) {
            const size_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            const size_t l = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
            char why[192];
            std::snprintf(why, sizeof(why), "%s (heap %uK free, %uK largest)",
                          _stream.error().c_str(),
                          static_cast<unsigned>(f / 1024u), static_cast<unsigned>(l / 1024u));
            err = why;
            net::Wifi::setPowerSave(false);
            return false;
        }
        _http = _stream.status();
        return true;
    }

    bool pump(std::string& out, std::string& err) override {
        char buf[1024];
        const int n = _stream.read(buf, sizeof(buf));
        if (n > 0) {
            out.assign(buf, static_cast<size_t>(n));
            return true;
        }
        // End of a fixed-length body: read() <= 0 is the only signal that
        // matters here (the LLM API sends Content-Length, unlike SSE).
        _http = _stream.status();
        if (_http == 0) {
            err = "no HTTP status (connect or TLS failed): " + _stream.error();
        }
        return false;
    }

    int  http() const override { return _http; }

    void close() override {
        _stream.close();
        _req.bearer.clear();               // the AppID's own copy goes with it
        net::Wifi::setPowerSave(false);
    }

private:
    net::HttpStream _stream;
    net::HttpReq    _req;
    int             _http = 0;
};
#endif  // ARDUINO

#if defined(NUMOS_HAVE_LIBCURL)
/**
 * Emulator: real HTTP(S) through libcurl, on the MULTI interface so no frame ever
 * blocks on the network. The write callback runs inside pump(), i.e. on the UI
 * thread — which is why there is no lock and no queue here, and why that
 * threading shape must NOT be carried back to the device.
 */
class CurlFetch final : public WaFetch {
public:
    ~CurlFetch() override { close(); }

    bool open(const AiConfig& cfg, const std::string& url, std::string& err) override {
        close();
        if (cfg.waAppId.empty()) { err = "no Wolfram AppID configured"; return false; }
        if (!globalInit()) { err = "curl_global_init failed"; return false; }

        _easy  = curl_easy_init();
        _multi = curl_multi_init();
        if (!_easy || !_multi) { err = "curl_easy/multi_init failed"; return false; }

        if (!cfg.waAppId.empty()) {
            // "Bearer", not a redaction: this string IS the wire header. A
            // placeholder here (the literal `***`) is not a safe default, it is a
            // malformed request — the server reads no appid at all and answers
            // 400 `Appid Missing`, which looks exactly like a missing header.
            // The AppID exists in this string and nowhere else: not in the URL,
            // not in a log line, not in an error string. CURLOPT_VERBOSE stays
            // off for exactly this reason.
            _auth = "Authorization: Bearer " + cfg.waAppId;
            _hdr  = curl_slist_append(_hdr, _auth.c_str());
        }
        _hdr = curl_slist_append(_hdr, "Accept: text/plain");

        curl_easy_setopt(_easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(_easy, CURLOPT_HTTPHEADER, _hdr);
        curl_easy_setopt(_easy, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(_easy, CURLOPT_WRITEFUNCTION, &CurlFetch::onWrite);
        curl_easy_setopt(_easy, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(_easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(_easy, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(_easy, CURLOPT_USERAGENT, "numos-emulator/1.0");
        curl_easy_setopt(_easy, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(_easy, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(_easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(_easy, CURLOPT_LOW_SPEED_TIME,
                         (long)std::max(5, static_cast<int>(cfg.waTimeoutMs / 1000)));
        curl_easy_setopt(_easy, CURLOPT_VERBOSE, 0L);

        if (curl_multi_add_handle(_multi, _easy) != CURLM_OK) {
            err = "curl_multi_add_handle failed";
            finish();
            return false;
        }
        _open = true;
        return true;
    }

    bool pump(std::string& out, std::string& err) override {
        out.clear();
        if (!_open) return false;

        int running = 0;
        curl_multi_poll(_multi, nullptr, 0, 0, nullptr);
        const CURLMcode mc = curl_multi_perform(_multi, &running);
        if (mc != CURLM_OK) {
            err = std::string("multi_perform: ") + curl_multi_strerror(mc);
            finish();
            return false;
        }

        int pending = 0;
        while (CURLMsg* m = curl_multi_info_read(_multi, &pending)) {
            if (m->msg != CURLMSG_DONE) continue;
            _code = m->data.result;
            long http = 0;
            curl_easy_getinfo(m->easy_handle, CURLINFO_RESPONSE_CODE, &http);
            _http = static_cast<int>(http);
            _done = true;
        }

        if (_done) {
            if (_code != CURLE_OK) {
                err = std::string("curl: ") + curl_easy_strerror(_code);
            } else if (_http >= 400) {
                // Deliberately NOT an error here: the body IS the diagnosis, and
                // classifyWolfram() reads it. Hand the bytes over and let the
                // caller name the failure with the provider's own words.
            }
        }

        if (!_buf.empty()) out.swap(_buf);

        if (_done || running == 0) {
            if (!_done && err.empty()) err = "transfer stopped early";
            finish();
            return false;
        }
        return true;
    }

    int http() const override { return _http; }

    void close() override { finish(); }

private:
    static size_t onWrite(char* ptr, size_t size, size_t nmemb, void* userp) {
        CurlFetch* self = static_cast<CurlFetch*>(userp);
        const size_t n = size * nmemb;
        self->_buf.append(ptr, n);
        return n;
    }

    /// Idempotent, and never clears the caller's error string.
    void finish() {
        if (_multi && _easy) curl_multi_remove_handle(_multi, _easy);
        if (_easy)  { curl_easy_cleanup(_easy);   _easy  = nullptr; }
        if (_multi) { curl_multi_cleanup(_multi); _multi = nullptr; }
        if (_hdr)   { curl_slist_free_all(_hdr);  _hdr   = nullptr; }
        _auth.clear();
        _open = false;
    }

    static bool globalInit() {
        static const bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        return ok;
    }

    std::string _auth;      ///< the ONLY copy of the AppID here; cleared on close
    std::string _buf;
    std::string _why;
    CURL*       _easy  = nullptr;
    CURLM*      _multi = nullptr;
    curl_slist* _hdr   = nullptr;
    CURLcode    _code  = CURLE_OK;
    int         _http  = 0;
    bool        _open  = false;
    bool        _done  = false;
};
#endif  // NUMOS_HAVE_LIBCURL

WaFetch* makeFetch(const AiConfig& cfg) {
#if defined(ARDUINO)
    (void)cfg;                             // hardware is WiFi-only, always live
    return new DeviceFetch();
#elif defined(NUMOS_HAVE_LIBCURL)
    // The emulator's transport field means the same thing here as it does for the
    // answer stream: "replay" drives the app from a recorded body with no socket
    // and nothing spent; anything else is a real request.
    if (cfg.transport == "replay") return new ReplayFetch();
    return new CurlFetch();
#else
    (void)cfg;
    return new StubFetch(
        "the Wolfram fetch needs a real transport "
        "(firmware: WiFi + TLS; native: -DNUMOS_HAVE_LIBCURL + -lcurl)");
#endif
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// WolframClient
// ═══════════════════════════════════════════════════════════════════════════

WolframClient::~WolframClient() { abort(); }

bool WolframClient::begin(const AiConfig& cfg, const std::string& query) {
    abort();

    _cfg      = cfg;
    _query    = trimCopy(query);
    _err.clear();
    _result   = WolframResult{};
    _status   = WaStatus::Ok;
    _http     = 0;
    _bytes    = 0;
    _done     = false;

    if (_query.empty()) {
        _err = "empty Wolfram query";
        _done = true;
        return false;
    }
    // No AppID = the check is not offered at all. Do not build an affordance for
    // a call that cannot be made, and do not fall back to anybody else's AppID.
    if (cfg.waAppId.empty()) {
        _err  = "no Wolfram AppID configured (the check is off)";
        _status = WaStatus::MissingAppId;
        _done = true;
        return false;
    }

    _impl = makeFetch(cfg);
    if (!_impl) {
        _err  = "no Wolfram transport";
        _status = WaStatus::Transport;
        _done = true;
        return false;
    }

    if (!_raw.reserve(kMaxWaBytes)) {
        _err  = "could not allocate the result buffer";
        _status = WaStatus::TooLarge;
        abort();
        _done = true;
        return false;
    }

    _url = buildWolframUrl(cfg, _query);

    std::string why;
    if (!static_cast<WaFetch*>(_impl)->open(cfg, _url, why)) {
        _err    = why.empty() ? std::string("could not open the request") : why;
        _status = WaStatus::Transport;
        abort();
        _done = true;
        return false;
    }
    return true;
}

bool WolframClient::pump() {
    if (_done || !_impl) return false;

    std::string chunk;
    std::string why;
    const bool more = static_cast<WaFetch*>(_impl)->pump(chunk, why);

    if (!chunk.empty()) {
        _bytes += chunk.size();
        if (!_raw.append(chunk.data(), chunk.size())) {
            static_cast<WaFetch*>(_impl)->close();
            _status = WaStatus::TooLarge;
            _err    = std::string(waStatusText(WaStatus::TooLarge)) +
                      " (cap " + std::to_string(kMaxWaBytes) + " bytes)";
            _done   = true;
            return false;
        }
    }

    if (more) return true;

    _http = static_cast<WaFetch*>(_impl)->http();
    static_cast<WaFetch*>(_impl)->close();
    _done = true;

    // A transport failure is named BEFORE anything is inferred from the body: a
    // TLS or stall failure has no status and no bytes, and reporting that as
    // "empty result" sends you debugging the parser instead of the socket.
    if (!why.empty()) {
        _status = WaStatus::Transport;
        _err    = why;
        return false;
    }

    const std::string body = _raw.str();
    std::string detail;
    _status = classifyWolfram(_http, body, &detail);

    if (_status != WaStatus::Ok) {
        // The cap can only have been reported by the append path above, so a
        // still-full buffer at this point means the body was exactly at cap.
        _err = std::string(waStatusText(_status));
        if (_http) _err += " (HTTP " + std::to_string(_http) + ")";
        if (!detail.empty()) _err += ": " + detail;
        return false;
    }

    _result = parseWolframBody(body);
    return false;
}

void WolframClient::abort() {
    if (_impl) {
        static_cast<WaFetch*>(_impl)->close();
        delete static_cast<WaFetch*>(_impl);
        _impl = nullptr;
    }
    _raw.reset();
}

}  // namespace ai
