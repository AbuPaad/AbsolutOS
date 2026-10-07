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
 * AiClient.cpp — see the header. LVGL-free on purpose: a host test can compile
 * this file with `g++ -I src` and no LVGL, exactly like mdrender does.
 *
 * The two REAL network transports (emulator = libcurl, device = WiFi + TLS) are
 * stubs that fail with a named reason. They are the next task; the replay
 * transport is what the app runs on today, and it sends nothing anywhere.
 */

#include "ai/AiClient.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ARDUINO)
  #include <Preferences.h>    // NVS outranks config.json on hardware
  #include <FS.h>             // File
  #include <LittleFS.h>       // the fs::LittleFSFS object hal/FileSystem.h stands in for
#endif

#if defined(NUMOS_HAVE_LIBCURL)
  #include <curl/curl.h>      // emulator transport: PC only, never on device
#endif

#if defined(ARDUINO)
  #include "net/DeviceTransport.h"   // firmware transport: WiFi + TLS, Arduino-only TU
#endif

#include "hal/FileSystem.h"   // LittleFS shim — identical API on device + emulator

namespace ai {

// ═══════════════════════════════════════════════════════════════════════════
// Small string helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimCopy(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, e - b + 1);
}

/**
 * Flat-JSON string field lookup. config.json is ours and flat, so a full JSON
 * dependency would be the wrong trade on a 320x200 device: this finds "key",
 * then the next quoted value, honouring backslash escapes.
 */
bool jsonFindString(const std::string& js, const char* key, std::string& out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = js.find(needle);
    if (at == std::string::npos) return false;
    size_t colon = js.find(':', at + needle.size());
    if (colon == std::string::npos) return false;
    size_t q = js.find('"', colon + 1);
    if (q == std::string::npos) return false;
    std::string v;
    for (size_t i = q + 1; i < js.size(); ++i) {
        const char c = js[i];
        if (c == '\\' && i + 1 < js.size()) {
            const char n = js[++i];
            switch (n) {
                case 'n':  v.push_back('\n'); break;
                case 't':  v.push_back('\t'); break;
                case 'r':  v.push_back('\r'); break;
                case '"':  v.push_back('"');  break;
                case '\\': v.push_back('\\'); break;
                case '/':  v.push_back('/');  break;
                default:   v.push_back(n);    break;
            }
            continue;
        }
        if (c == '"') { out = v; return true; }
        v.push_back(c);
    }
    return false;
}

bool jsonFindInt(const std::string& js, const char* key, long& out) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t at = js.find(needle);
    if (at == std::string::npos) return false;
    size_t colon = js.find(':', at + needle.size());
    if (colon == std::string::npos) return false;
    size_t i = colon + 1;
    while (i < js.size() && (js[i] == ' ' || js[i] == '\t')) ++i;
    if (i >= js.size() || !(std::isdigit(static_cast<unsigned char>(js[i])) || js[i] == '-'))
        return false;
    out = std::strtol(js.c_str() + i, nullptr, 10);
    return true;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Filesystem helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string readTextFile(const std::string& path) {
    File f = LittleFS.open(path.c_str(), "r");
    if (!f) return {};
    std::string out;
    uint8_t buf[256];
    while (true) {
        const int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        out.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
        if (out.size() > 256u * 1024u) break;   // never slurp unbounded
    }
    f.close();
    return out;
}

bool writeTextFile(const std::string& path, const std::string& text) {
    File f = LittleFS.open(path.c_str(), "w");
    if (!f) return false;
    const size_t n = f.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    f.close();
    return n == text.size();
}

bool ensureDir(const std::string& path) {
    if (path.empty()) return false;
#if defined(ARDUINO)
    // fs::FS has no isDirectory(); File does. Ask the File, not the FS.
    File probe = LittleFS.open(path.c_str(), "r");
    if (probe && probe.isDirectory()) {
        probe.close();
        return true;
    }
#else
    if (LittleFS.isDirectory(path.c_str())) return true;
#endif
    return LittleFS.mkdir(path.c_str());
}

std::vector<std::string> listFiles(const std::string& dir, const char* extLower) {
    std::vector<std::string> out;
    File d = LittleFS.open(dir.c_str(), "r");
    if (!d || !d.isDirectory()) return out;
    const std::string ext = lower(extLower ? extLower : "");
    while (File entry = d.openNextFile()) {
        if (entry.isDirectory()) continue;
        std::string name = entry.name();
        const size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (!ext.empty()) {
            const std::string lc = lower(name);
            if (lc.size() < ext.size() ||
                lc.compare(lc.size() - ext.size(), ext.size(), ext) != 0)
                continue;
        }
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());   // determinism for scripts and goldens
    return out;
}

std::vector<std::string> listRecent(const std::string& dir, size_t max) {
    std::vector<std::string> out = listFiles(dir, ".md");
    if (out.size() > max) out.resize(max);
    return out;
}

std::string contentHash32(const std::string& body) {
    uint32_t h = 2166136261u;                    // FNV-1a
    for (unsigned char c : body) {
        h ^= c;
        h *= 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(h));
    return std::string(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// Config
// ═══════════════════════════════════════════════════════════════════════════

AiConfig AiConfig::load(const std::string& path) {
    AiConfig c;                     // compiled defaults — never a key
    c.configPath = path;

    const std::string js = readTextFile(path);
    std::string s;
    long n = 0;
    if (!js.empty()) {
        if (jsonFindString(js, "base_url", s))    c.baseUrl = s;
        if (jsonFindString(js, "models_url", s))  c.modelsUrl = s;
        if (jsonFindString(js, "model", s) && !s.empty()) c.model = s;
        if (jsonFindInt(js, "timeout_ms", n))     c.timeoutMs = static_cast<int>(n);
        if (jsonFindString(js, "prompts_dir", s)) c.promptsDir = s;
        if (jsonFindString(js, "results_dir", s)) c.resultsDir = s;
        if (jsonFindString(js, "sys_prompt", s))  c.sysPrompt = s;
#if !defined(ARDUINO)
        // Host/emulator only: a dev may force "emulator" (libcurl) or "replay".
        // On hardware the transport is fixed to "wifi" below — it is not a
        // setting, so a stale value in the file cannot divert the radio.
        if (jsonFindString(js, "transport", s) && !s.empty()) c.transport = s;
#endif
        if (jsonFindInt(js, "retention_max_files", n)) c.retentionMaxFiles = static_cast<int>(n);
        if (jsonFindInt(js, "retention_max_bytes", n)) c.retentionMaxBytes = n;
        // Wolfram|Alpha: the AppID is BYO and lives beside the provider key, in
        // the same file and the same NVS namespace — one config surface, one
        // resolution ladder, one place the portal has to write.
        if (jsonFindString(js, "wa_host", s))      c.waHost = s;
        if (jsonFindString(js, "wa_path", s))      c.waPath = s;
        if (jsonFindInt(js, "wa_maxchars", n))     c.waMaxChars = static_cast<int>(n);
        if (jsonFindInt(js, "wa_timeout_ms", n))   c.waTimeoutMs = static_cast<int>(n);
        if (jsonFindString(js, "wa_units", s))     c.waUnits = s;
        if (jsonFindString(js, "wa_location", s))  c.waLocation = s;
        if (jsonFindString(js, "wa_language", s))  c.waLanguage = s;
        if (jsonFindString(js, "wa_appid", s) && !s.empty()) {
            c.waAppId = s;
            c._waKeySource = "config";
        }
        if (jsonFindString(js, "api_key", s) && !s.empty()) {
            c.apiKey = s;
            c._keySource = "config";   // bringup only — the SD card is readable
        }
    }

#if defined(ARDUINO)
    // NVS outranks the file on hardware: the captive portal (later) writes here.
    // Compiled out entirely on the host so the emulator stays keyless.
    {
        Preferences prefs;
        if (prefs.begin("numos-ai", true)) {
            const String k = prefs.getString("api_key", String());
            const String w = prefs.getString("wa_appid", String());
            prefs.end();
            if (k.length() > 0) {
                c.apiKey = std::string(k.c_str());
                c._keySource = "nvs";
            }
            if (w.length() > 0) {
                c.waAppId = std::string(w.c_str());
                c._waKeySource = "nvs";
            }
        }
    }
#endif
#if defined(ARDUINO)
    // Hardware is WiFi-only. The AI path is always the board's own radio + TLS;
    // "transport" is not a user setting, so nothing in config.json can send the
    // app back onto the recorded fixture.
    c.transport = "wifi";
#endif
    if (c.apiKey.empty()) c._keySource = "none";
    if (c.waAppId.empty()) c._waKeySource = "none";
    return c;
}

namespace {
/**
 * Create the folders that hold `path`. LittleFS.open(..., "w") does NOT create
 * parents, so writing /ai/config.json on a unit whose /ai was never made failed
 * at the last step and the app reported "not saved" — which is what a fresh
 * board, and a fresh emulator fs root, did every time. The portal's config
 * writer already mkdirs /ai; this brings the on-device save path in line.
 */
void ensureParentDir(const std::string& path) {
    for (size_t at = path.find('/', 1); at != std::string::npos;
         at = path.find('/', at + 1)) {
        ensureDir(path.substr(0, at));
    }
}

/**
 * Replace (or insert) one flat "key": "value" string in the config file, via
 * temp+rename. Shared by the two on-device settings the app can change. A
 * round-trip through the AiConfig struct would drop keys this build does not
 * model, so the edit is textual and surgical, exactly like the model picker's.
 */
bool setConfigString(const std::string& path, const char* keyName,
                     const std::string& value) {
    if (value.empty()) return false;
    ensureParentDir(path);

    std::string esc;
    for (char ch : value) {
        if (ch == '"' || ch == '\\') esc += '\\';
        esc += ch;
    }

    const std::string key = std::string("\"") + keyName + "\"";
    const std::string js  = readTextFile(path);
    if (js.empty()) {
        // No config yet. load() fills everything else from compiled defaults, so
        // a file holding just this key is a valid config.
        return writeTextFile(path, "{\n  " + key + ": \"" + esc + "\"\n}\n");
    }

    std::string out = js;
    const size_t k = js.find(key);
    if (k == std::string::npos) {
        const size_t open = js.find('{');
        if (open == std::string::npos) return false;
        out.insert(open + 1, "\n  " + key + ": \"" + esc + "\",");
    } else {
        // Advance past the ':' to the value's opening quote. The quotes on the
        // key are what keep "model" from matching inside "models_url".
        const size_t v = js.find('"', js.find(':', k + key.size()));
        if (v == std::string::npos) return false;
        size_t e = v + 1;
        while (e < js.size() && js[e] != '"') {
            if (js[e] == '\\') ++e;
            ++e;
        }
        if (e >= js.size()) return false;
        out.replace(v + 1, e - v - 1, esc);
    }

    const std::string tmp = path + ".tmp";
    if (!writeTextFile(tmp, out)) return false;
    if (LittleFS.rename(tmp.c_str(), path.c_str())) return true;
    // LittleFS's rename() can refuse when the destination already exists; a plain
    // overwrite is better than reporting a failed save for a write that works.
    const bool ok = writeTextFile(path, out);
    LittleFS.remove(tmp.c_str());
    return ok;
}
}  // namespace

bool setConfigModel(const std::string& path, const std::string& modelId) {
    return setConfigString(path, "model", modelId);
}

// ═══════════════════════════════════════════════════════════════════════════
// The request — the frozen wire contract, as data in and JSON out
// ═══════════════════════════════════════════════════════════════════════════

std::string readBinaryFile(const std::string& path) {
    File f = LittleFS.open(path.c_str(), "r");
    if (!f) return {};
    const size_t n = f.size();
    std::string out(n, '\0');
    const size_t got = n ? f.read(reinterpret_cast<uint8_t*>(&out[0]), n) : 0;
    out.resize(got);
    return out;
}

std::string base64Encode(const std::string& in) {
    static const char* kT =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8) |
                            static_cast<unsigned char>(in[i + 2]);
        out += kT[(v >> 18) & 63]; out += kT[(v >> 12) & 63];
        out += kT[(v >> 6) & 63];  out += kT[v & 63];
        i += 3;
    }
    const size_t rem = in.size() - i;
    if (rem == 1) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += kT[(v >> 18) & 63]; out += kT[(v >> 12) & 63]; out += "==";
    } else if (rem == 2) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                           (static_cast<unsigned char>(in[i + 1]) << 8);
        out += kT[(v >> 18) & 63]; out += kT[(v >> 12) & 63];
        out += kT[(v >> 6) & 63];  out += '=';
    }
    return out;
}

namespace {

/// Escape for embedding inside a JSON string literal.
std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 16);
    for (const char ch : in) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

/**
 * The flat, strict response schema. This is CODE, not data: the schema and the
 * renderer are one contract, so changing one without the other breaks the app.
 * (The system prompt, by contrast, is data — it ships in config.json.)
 */
const char* kAnswerSchema =
    "{"
      "\"type\":\"object\","
      "\"additionalProperties\":false,"
      "\"required\":[\"title\",\"answer\",\"followups\",\"confidence\","
                     "\"tool_query\",\"tool_server\",\"transcribed_question\"],"
      "\"properties\":{"
        "\"title\":{\"type\":\"string\",\"maxLength\":40},"
        "\"answer\":{\"type\":\"string\"},"
        "\"followups\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
                       "\"minItems\":3,\"maxItems\":3},"
        "\"confidence\":{\"type\":\"number\"},"
        "\"tool_query\":{\"type\":\"string\"},"
        "\"tool_server\":{\"type\":\"string\"},"
        "\"transcribed_question\":{\"type\":\"string\"}"
      "}"
    "}";

bool isOpenRouter(const std::string& url) {
    return url.find("openrouter.ai") != std::string::npos;
}

}  // namespace

std::string buildRequestBody(const AiConfig& cfg, const AiRequest& req) {
    std::string b;
    b.reserve(1024 + cfg.sysPrompt.size() + req.question.size());

    b += "{\"model\":\"" + jsonEscape(cfg.model) + "\"";
    b += ",\"stream\":true";
    b += ",\"temperature\":0.2";
    b += ",\"max_tokens\":" + std::to_string(cfg.maxTokens);
    b += ",\"response_format\":{\"type\":\"json_schema\",\"json_schema\":{"
         "\"name\":\"numos_answer\",\"strict\":true,\"schema\":";
    b += kAnswerSchema;
    b += "}}";

    // Structured-output support is a property of the ENDPOINT, not the model.
    // Without this, OpenRouter may route to a provider that ignores the schema
    // and hand back prose with HTTP 200 — a silent, expensive failure. It is
    // emitted only for OpenRouter: other servers have no such field.
    if (isOpenRouter(cfg.baseUrl)) b += ",\"provider\":{\"require_parameters\":true}";

    b += ",\"messages\":[";
    b += "{\"role\":\"system\",\"content\":\"" + jsonEscape(cfg.sysPrompt) + "\"}";

    // With an image the user turn becomes a parts ARRAY, and the text part MUST
    // come first (contract §2.5). With no typed text the image IS the question.
    const std::string img = cfg.promptsDir + "/" + req.imageFile;
    const std::string bytes = req.imageFile.empty() ? std::string() : readBinaryFile(img);
    const std::string text = req.question.empty() ? std::string("(look at the image)")
                                                  : req.question;
    b += ",{\"role\":\"user\",\"content\":";
    if (bytes.empty()) {
        b += "\"" + jsonEscape(text) + "\"";
    } else {
        b += "[{\"type\":\"text\",\"text\":\"" + jsonEscape(text) + "\"},";
        b += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
        b += base64Encode(bytes);
        b += "\"}}]";
    }
    b += "}]}";
    return b;
}

// ═══════════════════════════════════════════════════════════════════════════
// Transports
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/** Fails with a named reason instead of pretending to work. */
class StubTransport final : public AiTransport {
public:
    explicit StubTransport(std::string why) : _why(std::move(why)) {}
    bool open(const AiConfig&, const AiRequest&) override { return false; }
    bool poll(std::string&) override { return false; }
    void close() override {}
    const char* name() const override { return "stub"; }
    std::string error() const override { return _why; }
private:
    std::string _why;
};

/**
 * Replay: feeds a recorded SSE fixture in small chunks, so the app streams,
 * paginates and commits exactly as it would against a live endpoint — while
 * sending NOTHING anywhere. This is the bringup default.
 */
class ReplayTransport final : public AiTransport {
public:
    bool open(const AiConfig&, const AiRequest&) override {
        _buf = ai::readTextFile(kFixture);
        if (_buf.empty()) {
            _why = std::string("replay fixture missing or empty: ") + kFixture;
            return false;
        }
        _pos = 0;
        _open = true;
        return true;
    }

    bool poll(std::string& out) override {
        if (!_open) return false;
        if (_pos >= _buf.size()) { _open = false; return false; }
        const size_t n = std::min(kChunk, _buf.size() - _pos);
        out.assign(_buf, _pos, n);
        _pos += n;
        return true;
    }

    void close() override { _open = false; }
    const char* name() const override { return "replay"; }
    std::string error() const override { return _why; }

    /// ~0.4 s per KB at 60 fps: slow enough to screenshot mid-stream.
    static constexpr size_t kChunk = 24;
    static constexpr const char* kFixture = "/ai/replay/answer.sse";

private:
    std::string _buf;
    size_t      _pos = 0;
    bool        _open = false;
    std::string _why;
};

#if defined(NUMOS_HAVE_LIBCURL)
/**
 * Emulator transport: real HTTP(S) through libcurl, on the MULTI interface so
 * no frame ever blocks on the network.
 *
 * Two things worth knowing:
 *  - The write callback runs inside poll(), i.e. on the UI thread, once per
 *    frame. That is why there is no lock and no queue here; the device
 *    transport, whose callback runs on the HTTP task, needs both.
 *  - The stall rule is curl's own low-speed check: under 1 byte/s for
 *    timeout_ms is a failure. There is deliberately NO overall transfer cap,
 *    because a model may legitimately think for a while — bytes still flowing
 *    means it is alive.
 */
class CurlTransport final : public AiTransport {
public:
    ~CurlTransport() override { close(); }

    bool open(const AiConfig& cfg, const AiRequest& req) override {
        close();
        _cfg = cfg;
        _req = req;
        if (!globalInit()) { _why = "curl_global_init failed"; return false; }
        _easy  = curl_easy_init();
        _multi = curl_multi_init();
        if (!_easy || !_multi) { _why = "curl_easy/multi_init failed"; return false; }

        _url = cfg.baseUrl;
        while (!_url.empty() && _url.back() == '/') _url.pop_back();
        _url += "/chat/completions";

        _hdr = curl_slist_append(_hdr, "Content-Type: application/json");
        // The key exists in this header and nowhere else: not in the body, not
        // in a log line, not in an error string. CURLOPT_VERBOSE stays off for
        // exactly this reason — verbose mode prints request headers.
        if (!cfg.apiKey.empty()) {
            _auth = "Authorization: Bearer " + cfg.apiKey;
            _hdr  = curl_slist_append(_hdr, _auth.c_str());
        }
        _hdr = curl_slist_append(_hdr, "HTTP-Referer: https://numos.local");
        _hdr = curl_slist_append(_hdr, "X-Title: NumOS");

        curl_easy_setopt(_easy, CURLOPT_URL, _url.c_str());
        curl_easy_setopt(_easy, CURLOPT_HTTPHEADER, _hdr);
        curl_easy_setopt(_easy, CURLOPT_POST, 1L);
        curl_easy_setopt(_easy, CURLOPT_POSTFIELDS, _req.body.data());
        curl_easy_setopt(_easy, CURLOPT_POSTFIELDSIZE, (long)_req.body.size());
        curl_easy_setopt(_easy, CURLOPT_WRITEFUNCTION, &CurlTransport::onWrite);
        curl_easy_setopt(_easy, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(_easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(_easy, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(_easy, CURLOPT_USERAGENT, "numos-emulator/1.0");
        curl_easy_setopt(_easy, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(_easy, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(_easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(_easy, CURLOPT_LOW_SPEED_TIME,
                         (long)std::max(5, static_cast<int>(cfg.timeoutMs / 1000)));
        curl_easy_setopt(_easy, CURLOPT_VERBOSE, 0L);

        if (curl_multi_add_handle(_multi, _easy) != CURLM_OK) {
            _why = "curl_multi_add_handle failed";
            finish();
            return false;
        }
        _open = true;
        _done = false;
        return true;
    }

    bool poll(std::string& out) override {
        out.clear();
        if (!_open) return false;

        int running = 0;
        curl_multi_poll(_multi, nullptr, 0, 0, nullptr);   // service the sockets
        const CURLMcode mc = curl_multi_perform(_multi, &running);
        if (mc != CURLM_OK) {
            _why = std::string("multi_perform: ") + curl_multi_strerror(mc);
            finish();
            return false;
        }

        int pending = 0;
        while (CURLMsg* m = curl_multi_info_read(_multi, &pending)) {
            if (m->msg != CURLMSG_DONE) continue;
            _code = m->data.result;
            long http = 0;
            curl_easy_getinfo(m->easy_handle, CURLINFO_RESPONSE_CODE, &http);
            _http = http;
            _done = true;
        }

        // Diagnose BEFORE handing the buffer away: on a 4xx/5xx the provider's
        // explanation is IN that buffer, and an error body never carries a key.
        if (_done) {
            if (_code != CURLE_OK) {
                _why = std::string("curl: ") + curl_easy_strerror(_code);
            } else if (_http >= 400) {
                _why = "HTTP " + std::to_string(_http) + tailOfBody();
            }
        }

        if (!_buf.empty()) out.swap(_buf);

        if (_done || running == 0) {
            if (!_done && _why.empty()) _why = "transfer stopped early";
            finish();
            return false;
        }
        return true;
    }

    void close() override { finish(); }

    const char* name() const override { return "emulator(libcurl)"; }
    std::string error() const override { return _why; }

private:
    static size_t onWrite(char* ptr, size_t size, size_t nmemb, void* userp) {
        CurlTransport* self = static_cast<CurlTransport*>(userp);
        const size_t n = size * nmemb;
        self->_buf.append(ptr, n);
        self->_total += n;
        return n;
    }

    /// Release curl state. Idempotent, and never clears `_why`: callers report
    /// the error AFTER close().
    void finish() {
        // Native-only diagnostic: exactly what libcurl actually delivered. This is
        // what distinguishes "the server sent a short body" from "our transport
        // dropped bytes", which is otherwise invisible — the app commits whatever
        // it holds either way.
        if (_easy) {
            std::fprintf(stderr, "[CURL] done=%d code=%d http=%ld bytes=%zu sawDone=%d\n",
                         (int)_done, (int)_code, _http, _total,
                         (int)(_buf.find("[DONE]") != std::string::npos));
        }
        if (_multi && _easy) curl_multi_remove_handle(_multi, _easy);
        if (_easy)  { curl_easy_cleanup(_easy);   _easy  = nullptr; }
        if (_multi) { curl_multi_cleanup(_multi); _multi = nullptr; }
        if (_hdr)   { curl_slist_free_all(_hdr);  _hdr   = nullptr; }
        _auth.clear();          // the header's own copy goes with it
        _open = false;
    }

    /// A bounded tail of the response body, for a diagnosable HTTP error.
    std::string tailOfBody() const {
        if (_buf.empty()) return {};
        const size_t n = std::min<size_t>(_buf.size(), 180);
        std::string s = _buf.substr(_buf.size() - n);
        for (char& c : s) if (c == '\n' || c == '\r') c = ' ';
        return std::string(": ") + s;
    }

    static bool globalInit() {
        static const bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        return ok;
    }

    AiConfig    _cfg;
    AiRequest   _req;       ///< owns the body curl points at
    std::string _url;
    std::string _auth;      ///< the ONLY copy of the key material; cleared on close
    std::string _buf;       ///< what the write callback has produced so far
    size_t      _total = 0; ///< every byte the server delivered (diagnostic)
    std::string _why;
    CURL*       _easy  = nullptr;
    CURLM*      _multi = nullptr;
    curl_slist* _hdr   = nullptr;
    CURLcode    _code  = CURLE_OK;
    long        _http  = 0;
    bool        _open  = false;
    bool        _done  = false;
};
#endif  // NUMOS_HAVE_LIBCURL

}  // namespace

AiTransport* makeTransport(const AiConfig& cfg) {
    if (cfg.transport == "emulator") {
#if defined(NUMOS_HAVE_LIBCURL)
        return new CurlTransport();
#else
        // Built without libcurl: refuse loudly rather than degrade silently.
        return new StubTransport(
            "emulator transport needs libcurl (-DNUMOS_HAVE_LIBCURL + -lcurl)");
#endif
    }
    if (cfg.transport == "device" || cfg.transport == "wifi") {
#if defined(ARDUINO)
        // net/DeviceTransport.cpp. Guarded so the host test and the emulator
        // build keep compiling with `g++ -I src` and no Arduino headers.
        return net::makeDeviceTransport(cfg);
#else
        return new StubTransport(
            "device transport is firmware-only (WiFi + mbedTLS + esp_http_client)");
#endif
    }
    return new ReplayTransport();
}

// ═══════════════════════════════════════════════════════════════════════════
// The scanner
// ═══════════════════════════════════════════════════════════════════════════

void AiScanner::reset() {
    _line.clear();
    _st = St::SeekKey;
    _inStr = false;
    _esc = false;
    _pendingEsc = 0;
    _uniLen = 0;
    _uniVal = 0;
    _key.clear();
    _title.clear();
    _answer.clear();
    _toolQuery.clear();
    _toolServer.clear();
    _transcribed.clear();
    _depth = 0;
    _fence = false;
    _ticks = 0;
    _lineStart = true;
    _dashes = 0;
    _pageBreak = false;
    _pages = 1;
    _sawDone = false;
    _reasoning = 0;

    // stage 1 (envelope)
    _envInStr = false;
    _envReasoning = false;
    _envStrIsKey = false;
    _envEsc = false;
    _envInUni = false;
    _envExpectValue = false;
    _envCapture = false;
    _envUniLen = 0;
    _envUniVal = 0;
    _envKeyBuf.clear();
    _envLastKey.clear();
}

void AiScanner::feed(const std::string& raw) {
    for (const char c : raw) {
        if (c != '\n') {
            _line.push_back(c);
            continue;
        }
        std::string line = _line;
        _line.clear();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;              // event boundary
        if (line[0] == ':') continue;            // ": OPENROUTER PROCESSING" etc.
        if (line.rfind("data:", 0) == 0) {
            std::string payload = line.substr(5);
            if (!payload.empty() && payload[0] == ' ') payload.erase(0, 1);
            if (trimCopy(payload) == "[DONE]") { _sawDone = true; continue; }
            // The SSE wrapper is NOT the payload: each data: line is
            // {"choices":[{"delta":{"content":"<escaped fragment of the
            // structured answer>"}}]}. walkEnvelope finds that content string,
            // unescapes it, and streams the decoded bytes into walkJson — the
            // two stages are the difference between an empty answer and a real
            // one, and the host test guards exactly this seam.
            for (const char pc : payload) walkEnvelope(pc);
        } else {
            // Unknown SSE field: ignore the line rather than guess.
        }
    }
}

void AiScanner::finish() {
    // A stream that ended mid-escape: the held byte never became decidable.
    _line.clear();
}

void AiScanner::endString() {
    _inStr = false;
    if (_st == St::InKey) {
        _st = St::SeekValue;
    } else if (_st == St::InValue) {
        _st = St::SeekKey;
    }
}

void AiScanner::pushAnswerChar(char c) {
    _answer.push_back(c);

    if (c == '\n') {
        if (_ticks >= 3) _fence = !_fence;
        else if (!_fence && _dashes == 3) {
            if (_pages < kMaxPages) ++_pages;
            _pageBreak = true;
        }
        _ticks = 0;
        _dashes = 0;
        _lineStart = true;
        return;
    }
    if (_lineStart) {
        if (c == '`')      { ++_ticks; return; }
        if (c == '-')      { ++_dashes; return; }
        _ticks = 0;
        _dashes = 0;
        _lineStart = false;
    }
}

/**
 * The single place a decoded VALUE character is routed to a field.
 *
 * Adding tool_query / tool_server / transcribed_question by copying the old
 * two-branch `if (_key == "answer") ... else if (_key == "title") ...` into four
 * separate spots is how a field ends up present in one escape path and silently
 * missing in another — and that class of bug is invisible in the app, because
 * the stream still "succeeds".
 */
void AiScanner::pushField(char c) {
    if (_key == "answer")                    pushAnswerChar(c);
    else if (_key == "title")                _title.push_back(c);
    else if (_key == "tool_query")           _toolQuery.push_back(c);
    else if (_key == "tool_server")          _toolServer.push_back(c);
    else if (_key == "transcribed_question") _transcribed.push_back(c);
}

void AiScanner::walkJson(char c) {
    // ── inside a JSON string ────────────────────────────────────────────────
    if (_inStr) {
        if (_esc) {
            _esc = false;
            if (c == 'u') {              // \uXXXX
                // Collect four hex digits, then emit UTF-8. The guard below is
                // `_inUni`, NOT `_uniLen > 0`: entering the escape leaves the
                // digit count at 0, so a count-based guard was never true and the
                // four hex characters fell through as literal text — every \u
                // escape in an answer was written out as bare hex (Greek letters
                // arrived as "03b1 03b2 ..." and rendered as that text).
                _inUni = true;
                _uniLen = 0;
                _uniVal = 0;
                return;
            }
            char d = c;
            switch (c) {
                case 'n':  d = '\n'; break;
                case 't':  d = '\t'; break;
                case 'r':  d = '\r'; break;
                case 'b':  d = '\b'; break;
                case 'f':  d = '\f'; break;
                case '"':  d = '"';  break;
                case '\\': d = '\\'; break;
                case '/':  d = '/';  break;
                default:   d = c;    break;
            }
            if (_st == St::InKey) _key.push_back(d);
            else                  pushField(d);
            return;
        }
        if (_inUni) {                    // collecting \uXXXX digits
            const int hex = std::isdigit(static_cast<unsigned char>(c))
                                ? c - '0'
                                : (std::tolower(c) >= 'a' && std::tolower(c) <= 'f'
                                       ? std::tolower(c) - 'a' + 10 : -1);
            if (hex < 0) { _inUni = false; _uniLen = 0; return; }
            _uniVal = (_uniVal << 4) | static_cast<unsigned>(hex);
            if (++_uniLen == 4) {
                _inUni = false;
                _uniLen = 0;
                const unsigned cp = _uniVal;
                if (cp < 0x80) {
                    if (_st == St::InValue) pushField(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    char b[2] = {static_cast<char>(0xC0 | (cp >> 6)),
                                 static_cast<char>(0x80 | (cp & 0x3F))};
                    for (char x : b) if (_st == St::InValue) pushField(x);
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    if (_st == St::InValue) pushField('?');
                } else {
                    char b[3] = {static_cast<char>(0xE0 | (cp >> 12)),
                                 static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
                                 static_cast<char>(0x80 | (cp & 0x3F))};
                    for (char x : b) if (_st == St::InValue) pushField(x);
                }
            }
            return;
        }
        if (c == '\\') { _esc = true; return; }   // held until the next char arrives
        if (c == '"')  { endString(); return; }
        if (_st == St::InKey) _key.push_back(c);
        else if (_st == St::InValue) pushField(c);
        return;
    }

    // ── outside a string ────────────────────────────────────────────────────
    switch (_st) {
        case St::SeekKey:
            if (c == '"') { _inStr = true; _st = St::InKey; _key.clear(); }
            else if (c == '{' || c == ',') { /* stay */ }
            break;

        case St::SeekValue:
            // The ':' that separates a key from its value arrives HERE: the
            // key's closing quote already moved us out of InKey (endString).
            // Treating it as "not a quote" would push the whole value into
            // SkipValue and silently drop every field.
            if (c == ':') break;
            if (c == '"') { _inStr = true; _st = St::InValue; }
            else if (c == ',' || c == '}') { _st = St::SeekKey; }
            else if (!std::isspace(static_cast<unsigned char>(c))) {
                _st = St::SkipValue;      // a number, object or array: not ours
                _depth = 0;
            }
            break;

        case St::InKey:
            // Consumes ':' after the closing quote (endString already moved on).
            if (c == ':') _st = St::SeekValue;
            break;

        case St::SkipValue:
            if (c == '[' || c == '{') ++_depth;
            else if (c == ']' || c == '}') { if (_depth > 0) --_depth; else _st = St::SeekKey; }
            else if (c == ',' && _depth == 0) _st = St::SeekKey;
            break;

        case St::InValue:
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 1 — the SSE envelope
//
// A data: line carries the answer inside a JSON wrapper:
//   {"choices":[{"delta":{"content":"<escaped fragment>"}}]}
// `content` is itself JSON text, and consecutive deltas carry consecutive
// fragments of it. So: find that string, unescape it, and hand the decoded
// bytes to stage 2 — which is stateful across deltas, because one payload can
// be split anywhere, including between the two characters of an escape.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// JSON's single-character escapes; an unknown escape decodes to itself.
char jsonUnescapeSimple(char c) {
    switch (c) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case 'b':  return '\b';
        case 'f':  return '\f';
        case '"':  return '"';
        case '\\': return '\\';
        case '/':  return '/';
        default:   return c;
    }
}

int jsonHexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

void AiScanner::walkEnvelope(char c) {
    if (_envInStr) {
        char d   = 0;
        bool have = false;

        if (_envEsc) {
            _envEsc = false;
            if (c == 'u') {                       // \uXXXX
                _envInUni = true;
                _envUniLen = 0;
                _envUniVal = 0;
                return;
            }
            d    = jsonUnescapeSimple(c);
            have = true;
        } else if (_envInUni) {                   // collecting the 4 hex digits
            const int hex = jsonHexVal(c);
            if (hex < 0) { _envInUni = false; return; }
            _envUniVal = (_envUniVal << 4) | static_cast<unsigned>(hex);
            if (++_envUniLen < 4) return;
            _envInUni = false;
            const unsigned cp = _envUniVal;
            if (cp >= 0x80) {
                // Emit UTF-8; a lone surrogate becomes '?'. The answer contract
                // is ASCII, so this only ever catches stray envelope text.
                char b[3];
                int  n = 0;
                if (cp < 0x800) {
                    b[n++] = static_cast<char>(0xC0 | (cp >> 6));
                    b[n++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    b[n++] = '?';
                } else {
                    b[n++] = static_cast<char>(0xE0 | (cp >> 12));
                    b[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    b[n++] = static_cast<char>(0x80 | (cp & 0x3F));
                }
                for (int i = 0; i < n; ++i) {
                    if (_envStrIsKey)          _envKeyBuf.push_back(b[i]);
                    else if (_envReasoning)    ++_reasoning;
                    else if (_envCapture)      walkJson(b[i]);
                }
                return;
            }
            d    = static_cast<char>(cp);
            have = true;
        } else if (c == '\\') {
            _envEsc = true;                       // held: the escape is only
            return;                               // decidable on the next byte
        } else if (c == '"') {
            _envInStr   = false;
            if (_envStrIsKey) _envLastKey = _envKeyBuf;
            _envStrIsKey = false;
            _envCapture  = false;
            return;
        } else {
            d    = c;
            have = true;
        }

        if (have) {
            if (_envStrIsKey)          _envKeyBuf.push_back(d);
            else if (_envReasoning)    ++_reasoning;
            else if (_envCapture)      walkJson(d);
        }
        return;
    }

    // Outside a string: only enough structure to know whether the next string
    // opens a KEY or a VALUE. Imagined flat, this is where the bug lived.
    switch (c) {
        case '"':
            _envInStr       = true;
            _envStrIsKey    = !_envExpectValue;
            _envExpectValue = false;
            _envKeyBuf.clear();
            _envCapture     = (!_envStrIsKey && (_envLastKey == "content" ||
                                                 _envLastKey == "reasoning"));
            _envReasoning   = (!_envStrIsKey && _envLastKey == "reasoning");
            break;
        case ':':            _envExpectValue = true;  break;
        case ',':            _envExpectValue = false; break;
        case '{': case '[':  _envExpectValue = false; break;
        case '}': case ']':  _envExpectValue = false; break;
        default: break;                       // numbers, true/false/null
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Session
// ═══════════════════════════════════════════════════════════════════════════

AiSession::~AiSession() { abort(); }

bool AiSession::begin(const AiConfig& cfg, const std::string& image,
                      const std::string& question) {
    abort();
    _cfg       = cfg;
    _image     = image;
    _error.clear();
    _live.clear();
    _committed = false;
    _scan.reset();

    _req.question  = question;
    _req.imageFile = image;
    _req.body      = buildRequestBody(cfg, _req);

    _t = makeTransport(cfg);
    if (!_t) {
        _state = State::Failed;
        _error = "no transport";
        return false;
    }
    if (!_t->open(cfg, _req)) {
        _error = _t->error().empty() ? std::string("open failed") : _t->error();
        _state = State::Failed;
        return false;
    }
    _state = State::Running;
    return true;
}

bool AiSession::pump() {
    if (_state != State::Running) return false;

    std::string chunk;
    const bool more = _t->poll(chunk);
    if (!chunk.empty()) _scan.feed(chunk);

    // Page 1's live text = the decoded answer up to the first page break. The
    // remainder keeps accumulating in the scanner and lands in ONE fell-swoop
    // render when the response completes (architecture §5).
    const std::string& a = _scan.answer();
    const size_t brk = a.find("\n---");
    _live = (brk == std::string::npos) ? a : a.substr(0, brk);

    if (!more) {
        _scan.finish();
        const std::string& fin = _scan.answer();
        const size_t b2 = fin.find("\n---");
        _live = (b2 == std::string::npos) ? fin : fin.substr(0, b2);
        const std::string terr = _t ? _t->error() : std::string();
        if (_t) _t->close();

        // A failed request must NOT masquerade as an empty answer: say what
        // happened (HTTP status, TLS, stall) while the reason is still known.
        if (!terr.empty()) {
            _error = terr;
            _state = State::Failed;
            return false;
        }
        // A stream that never sent its terminator is a truncated answer, not a
        // short one. Observed in the field: OpenRouter answers with no [DONE] at
        // all and an unparseable body (device-networking.md). Committing that
        // would save half an answer as if it were whole, so require the
        // terminator the scanner already records. Fixtures and the emulator
        // transport both end with [DONE], so this gate costs them nothing.
        if (!_scan.sawDone()) {
            _error = "stream ended before [DONE] (truncated response)";
            _state = State::Failed;
            return false;
        }
        if (fin.empty() || _scan.title().empty()) {
            _error = _scan.reasoningChars() > 0
                         ? "the model only produced thinking, no answer"
                         : "no answer in the stream";
            _state = State::Failed;
            return false;
        }
        _state = State::Done;
        return false;
    }
    return true;
}

void AiSession::abort() {
    if (_t) {
        _t->close();
        delete _t;
        _t = nullptr;
    }
    // MODE mid-stream = abort, discard, no file. Only [DONE] ever saves.
    if (_state == State::Running) _state = State::Idle;
}

std::string slugify(const std::string& title) {
    std::string s = lower(trimCopy(title));
    std::string out;
    for (const char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
        else if (c == ' ' || c == '-' || c == '_' || c == '.') out.push_back('-');
        if (out.size() >= 40) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    if (out.empty()) out = "answer";
    return out;
}

std::string AiSession::slug() const {
    return slugify(_scan.title());
}

bool AiSession::commit(std::string* savedPathOut) {
    if (_state != State::Done || _committed) return false;

    const std::string& body = _scan.answer();
    if (body.empty()) {
        _error = "empty answer";
        return false;
    }
    if (!ensureDir(_cfg.resultsDir)) {
        _error = "cannot create " + _cfg.resultsDir;
        return false;
    }

    // Content-hash dedupe: an identical answer is never written twice, and no
    // side index is needed because the hash rides in the metadata line.
    const std::string hash = contentHash32(body);
    const std::string base = slug();
    for (const auto& f : listRecent(_cfg.resultsDir, 500)) {
        const std::string text = readTextFile(_cfg.resultsDir + "/" + f);
        if (text.find("hash=" + hash) != std::string::npos) {
            _committed = true;
            if (savedPathOut) *savedPathOut = _cfg.resultsDir + "/" + f;
            return true;
        }
    }

    std::string path = _cfg.resultsDir + "/" + base + ".md";
    for (int i = 2; i < 100 && LittleFS.exists(path.c_str()); ++i)
        path = _cfg.resultsDir + "/" + base + "-" + std::to_string(i) + ".md";

    std::string out = body;
    if (out.empty() || out.back() != '\n') out.push_back('\n');
    out += "%%ai: model=" + _cfg.model +
           " hash=" + hash +
           " pages=" + std::to_string(_scan.pageCount()) +
           " image=" + (_image.empty() ? std::string("-") : _image);
    // The check hop's fields ride here too. The live run has them in the
    // scanner's RAM, but an answer REOPENED from Recent has only what the
    // document kept — and a Wolfram check that only ever works right after the
    // run that produced it is half a feature. Both values are free English, so
    // they are encoded; absent fields add nothing, which leaves every earlier
    // answer's metadata line byte-identical.
    if (!_scan.toolQuery().empty())
        out += " tool_query=" + encodeMetaValue(_scan.toolQuery());
    if (!_scan.toolServer().empty())
        out += " tool_server=" + encodeMetaValue(_scan.toolServer());
    if (!_scan.transcribedQuestion().empty())
        out += " transcribed=" + encodeMetaValue(_scan.transcribedQuestion());
    out += "%%\n";

    // temp + rename: a half-written answer is never readable.
    const std::string tmp = path + ".tmp";
    if (!writeTextFile(tmp, out)) {
        _error = "write failed: " + tmp;
        return false;
    }
    if (!LittleFS.rename(tmp.c_str(), path.c_str())) {
        _error = "rename failed";
        return false;
    }
    _committed = true;
    if (savedPathOut) *savedPathOut = path;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// The %%ai: line — encode, decode, read back
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Characters that survive a round trip inside the space-separated token line.
bool metaSafeChar(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return true;
    switch (c) {
        case '.': case '_': case ':': case '/': case '-': case '+': return true;
        default: return false;
    }
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool metaSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}  // namespace

std::string encodeMetaValue(const std::string& v) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(v.size());
    for (char ch : v) {
        if (metaSafeChar(ch)) {
            out += ch;
            continue;
        }
        const unsigned char u = static_cast<unsigned char>(ch);
        out += '%';
        out += kHex[(u >> 4) & 0x0F];
        out += kHex[u & 0x0F];
    }
    return out;
}

bool decodeMetaValue(const std::string& v, std::string* out) {
    std::string r;
    r.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] != '%') { r += v[i]; continue; }
        // A truncated or non-hex escape is reported, never half-decoded: a
        // silently-kept "%2" would be a query nobody typed.
        if (i + 2 >= v.size()) return false;
        const int hi = hexDigit(v[i + 1]);
        const int lo = hexDigit(v[i + 2]);
        if (hi < 0 || lo < 0) return false;
        r += static_cast<char>((hi << 4) | lo);
        i += 2;
    }
    if (out) *out = r;
    return true;
}

AnswerMeta parseAnswerMeta(const std::string& text) {
    AnswerMeta m;

    // The LAST `%%ai:` in the file: a saved answer holds exactly one, and taking
    // the last means a body that happens to quote one cannot shadow it.
    size_t at = std::string::npos;
    for (size_t p = text.find("%%ai:"); p != std::string::npos;
         p = text.find("%%ai:", p + 1)) {
        at = p;
    }
    if (at == std::string::npos) return m;
    const size_t end = text.find("%%", at + 5);
    if (end == std::string::npos) return m;
    const std::string line = text.substr(at + 5, end - (at + 5));

    bool any = false;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && metaSpace(line[i])) ++i;
        size_t j = i;
        while (j < line.size() && !metaSpace(line[j])) ++j;
        if (j > i) {
            const std::string tok = line.substr(i, j - i);
            const size_t eq = tok.find('=');
            if (eq != std::string::npos) {
                const std::string key = tok.substr(0, eq);
                std::string val;
                if (decodeMetaValue(tok.substr(eq + 1), &val)) {
                    // `kind=wolfram` is a check document, not an answer: it sets
                    // no known key, so it leaves found == false.
                    if      (key == "model")       { m.model = val;        any = true; }
                    else if (key == "hash")        { m.hash = val;         any = true; }
                    else if (key == "image")       { m.image = val;        any = true; }
                    else if (key == "tool_query")  { m.toolQuery = val;    any = true; }
                    else if (key == "tool_server") { m.toolServer = val;   any = true; }
                    else if (key == "transcribed") { m.transcribed = val;  any = true; }
                    else if (key == "pages") {
                        m.pages = std::atoi(val.c_str());
                        any = true;
                    }
                }
            }
        }
        i = j;
    }
    m.found = any;
    return m;
}

AnswerMeta readAnswerMeta(const std::string& path) {
    return parseAnswerMeta(readTextFile(path));
}

}  // namespace ai
