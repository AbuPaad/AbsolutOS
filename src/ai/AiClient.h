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
 * AiClient.h — the AI wrapper's non-UI half: config resolution, the transport
 * seam, the SSE/JSON scanner and the save path.
 *
 * Deliberately LVGL-FREE: it includes no lvgl.h, so a host test can compile it
 * with `g++ -I src` alone (like notes_mdrender_test.cpp does for mdrender).
 * The app (src/apps/AiApp) owns every pixel and every key.
 *
 * Design record: ~/musings/sserialprintthing/ai-wrapper-architecture.md
 * Build record:  ~/musings/sserialprintthing/aiimplement.md
 *
 * Credentials: AiConfig::load() resolves NVS -> /ai/config.json -> compiled
 * defaults, and compiled defaults exist for the NON-SECRET fields only. The
 * host and emulator must build and run keyless.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ai {

// ═══════════════════════════════════════════════════════════════════════════
// Config (architecture §3)
// ═══════════════════════════════════════════════════════════════════════════

struct AiConfig {
    std::string baseUrl      = "https://openrouter.ai/api/v1";
    std::string modelsUrl    = "https://openrouter.ai/api/v1/models";
    std::string apiKey;                        ///< never a compiled default
    std::string model        = "google/gemini-2.5-flash-lite";
    int         timeoutMs    = 60000;
    std::string promptsDir   = "/ai/prompts";
    std::string resultsDir   = "/ai/results";
    std::string configPath   = "/ai/config.json";
    std::string sysPrompt;                     ///< data, not compiled
    int         retentionMaxFiles = 200;
    long        retentionMaxBytes = 32L * 1024L * 1024L;
    int         maxTokens    = 1200;               ///< data, not compiled
    /**
     * Which transport to use. "replay" feeds a recorded SSE fixture and sends
     * NOTHING anywhere — it is the bringup/emulator default so the app can be
     * driven and screenshotted with no network. "emulator" = real HTTPS from
     * the PC build (libcurl). "device" = real HTTPS on hardware (WiFi + TLS).
     */
    std::string transport    = "replay";

    /// NVS -> config.json -> compiled defaults. Never throws, always usable.
    static AiConfig load(const std::string& path = "/ai/config.json");

    /// Where the key came from, for display. "nvs" | "config" | "none".
    std::string keySource() const { return _keySource; }

    std::string _keySource = "none";
};

// ═══════════════════════════════════════════════════════════════════════════
// Transport seam (architecture §2) — the ONLY place the two targets differ
// ═══════════════════════════════════════════════════════════════════════════

/**
 * What one run asks for. `body` is the exact JSON POSTed, built once by
 * buildRequestBody() so the wire contract is verifiable with no socket at all.
 */
struct AiRequest {
    std::string question;    ///< typed question (empty when the image IS the question)
    std::string imageFile;   ///< filename under cfg.promptsDir, or ""
    std::string body;        ///< the request JSON
};

/**
 * The frozen wire contract as a pure function: POST {base_url}/chat/completions
 * with stream:true and a flat, strict json_schema. Key is NEVER in here — it
 * rides in the Authorization header, which the transport builds.
 */
std::string buildRequestBody(const AiConfig& cfg, const AiRequest& req);

/**
 * Open a stream, then pump it. poll() is non-blocking and returns true while
 * the stream may still yield more bytes; each successful poll hands back a
 * chunk of RAW stream text (SSE lines), which AiScanner consumes.
 */
class AiTransport {
public:
    virtual ~AiTransport() = default;

    /// Returns false if the stream could not be opened at all.
    virtual bool open(const AiConfig& cfg, const AiRequest& req) = 0;
    /// Pump once: false = finished (or failed; see error()).
    virtual bool poll(std::string& out) = 0;
    virtual void close() = 0;
    virtual const char* name() const = 0;
    /// Non-empty after a failure.
    virtual std::string error() const { return {}; }
};

/// Factory: chooses the implementation named by cfg.transport.
AiTransport* makeTransport(const AiConfig& cfg);

// ═══════════════════════════════════════════════════════════════════════════
// Answer scanner — char-level over the concatenated delta.content
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Eats raw SSE text and, from it, decodes the `title` and `answer` fields of
 * the structured response.
 *
 * Two layers, on purpose:
 *  1. SSE framing: `data: ...` payload lines; comments (`: ...`) and blank
 *     lines are skipped; `[DONE]` ends the stream. A naive parser dies here.
 *  2. A character walker over the concatenated JSON fragment. It is NOT a
 *     JSON parse per chunk (ArduinoJson cannot stream), so it un-escapes on
 *     the open path: \n \" \\ \t \uXXXX, holding ONE pending byte when a chunk
 *     ends on a bare backslash (the escape is only decidable next chunk).
 *
 * `answer` is one Markdown string. A line that is exactly `---`, OUTSIDE a
 * fenced code block, is a page break — the same rule the notes parser uses.
 */
class AiScanner {
public:
    void reset();

    /// Feed one raw chunk of stream text.
    void feed(const std::string& raw);

    /// Call once the stream ended: flushes any pending escape.
    void finish();

    const std::string& title() const  { return _title; }
    const std::string& answer() const { return _answer; }

    /// Pages counted so far (1 + the number of `---` breaks seen).
    int  pageCount() const { return _pages; }
    /// True once `[DONE]` was seen.
    bool sawDone() const { return _sawDone; }
    /**
     * Bytes that arrived as `delta.reasoning`. Thinking models (qwen3.5, most
     * Ollama builds) stream their whole monologue there and put NOTHING in
     * delta.content when the budget runs out — which otherwise looks exactly
     * like an empty response. Counted so it can be reported instead of guessed.
     */
    size_t reasoningChars() const { return _reasoning; }
    /// True if the last decoded chars completed a page break.
    bool takePageBreak() { const bool b = _pageBreak; _pageBreak = false; return b; }

    /// Exposed for the host test + diagnostics.
    bool inFence() const { return _fence; }

private:
    void feedLine(const std::string& payload, bool isEventLine);

    /// Stage 1: walk the SSE envelope, streaming the decoded `content` bytes
    /// into stage 2.
    void walkEnvelope(char c);
    /// Stage 2: walk the structured payload (title / answer / ...).
    void walkJson(char c);
    void pushAnswerChar(char c);
    void endString();

    // ── stage 1 state: the SSE envelope ─────────────────────────────────────
    // Nesting-aware on purpose: `content` lives at choices[0].delta.content, so
    // the "choices" value is an ARRAY, and a flat key/value machine (exactly
    // what stage 2 is) would take that '[' for a scalar and swallow the whole
    // array unparsed — leaving an empty answer and no error. Already happened
    // once; the host test guards this seam.
    bool        _envInStr = false;
    bool        _envStrIsKey = false;
    bool        _envEsc = false;
    bool        _envInUni = false;
    bool        _envExpectValue = false;
    bool        _envCapture = false;   ///< true while decoding a `content` value
    bool        _envReasoning = false; ///< ...or a `reasoning` value (thinking)
    int         _envUniLen = 0;
    unsigned    _envUniVal = 0;
    std::string _envKeyBuf;            ///< the key currently being read
    std::string _envLastKey;           ///< the last key that closed

    // SSE framing
    std::string _line;

    // ── stage 2 state: the structured payload ───────────────────────────────
    enum class St : uint8_t { SeekKey, InKey, SeekValue, InValue, SkipValue };
    St          _st = St::SeekKey;
    bool        _inStr = false;
    bool        _esc = false;         ///< a bare backslash is held until the next char
    char        _pendingEsc = 0;      ///< reserved (kept for ABI stability of the struct)
    int         _uniLen = 0;          ///< digits of a \uXXXX escape collected so far
    unsigned    _uniVal = 0;          ///< the \uXXXX value being assembled
    std::string _key;
    std::string _title;
    std::string _answer;
    int         _depth = 0;

    // page-break / fence state over decoded answer text
    bool _fence = false;
    int  _ticks = 0;                  ///< consecutive backticks at line start
    bool _lineStart = true;
    int  _dashes = 0;                 ///< consecutive '-' on this line
    bool _pageBreak = false;
    int  _pages = 1;
    bool _sawDone = false;
    size_t _reasoning = 0;            ///< delta.reasoning bytes (thinking models)
};

// ═══════════════════════════════════════════════════════════════════════════
// Session — one run: build the request, pump, commit
// ═══════════════════════════════════════════════════════════════════════════

class AiSession {
public:
    ~AiSession();

    /// Start a run. `image` is a filename under cfg.promptsDir, or empty for
    /// a typed question. Sends nothing on the replay transport.
    bool begin(const AiConfig& cfg, const std::string& image,
               const std::string& question = std::string());

    /// Pump the transport once. false = finished (check failed()/done()).
    bool pump();

    bool done() const    { return _state == State::Done; }
    bool failed() const  { return _state == State::Failed; }
    bool running() const { return _state == State::Running; }
    const std::string& error() const { return _error; }

    const AiScanner& scanner() const { return _scan; }

    /**
     * Page 1's live text: the decoded answer as it arrives, drawn plain and
     * word-by-word by the app. The rest of the response accumulates in the
     * scanner's buffer and lands in ONE fell-swoop render at completion.
     */
    const std::string& liveText() const { return _live; }

    /// Atomic temp+rename, content-hash dedupe. Returns the saved path.
    bool commit(std::string* savedPathOut = nullptr);

    /// Abort mid-stream: nothing is written. Accepted cost: tokens paid for.
    void abort();

    /// Suggested slug from the title (<=40 ASCII, filesystem-safe).
    std::string slug() const;

private:
    enum class State : uint8_t { Idle, Running, Done, Failed };

    AiConfig     _cfg;
    AiScanner    _scan;
    AiTransport* _t = nullptr;
    State        _state = State::Idle;
    std::string  _live;         ///< decoded answer tail shown as page 1
    std::string  _error;
    std::string  _image;
    AiRequest    _req;          ///< what the transport posts
    size_t       _liveFrom = 0; ///< how much of answer() is already in _live
    bool         _committed = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// Filesystem helpers (LittleFS on device, the native shim on the emulator)
// ═══════════════════════════════════════════════════════════════════════════

/// Files directly in `dir` whose lowercased name ends with `extLower`
/// (pass ".jpg"); sorted, so scripts and goldens are deterministic.
std::vector<std::string> listFiles(const std::string& dir, const char* extLower);

/// Newest-first list of entries in `dir`, up to `max`.
std::vector<std::string> listRecent(const std::string& dir, size_t max);

std::string readTextFile(const std::string& path);
/// Raw bytes (the prompt image). Empty if unreadable.
std::string readBinaryFile(const std::string& path);
/// Standard base64 — the image rides in the body as a data URL.
std::string base64Encode(const std::string& in);
bool        writeTextFile(const std::string& path, const std::string& text);

/**
 * Point the stored config at a different model, leaving every other key alone.
 *
 * Edits the JSON in place rather than re-serialising AiConfig: a round-trip
 * through the struct would silently drop any key this build does not model, and
 * sys_prompt is data that has to survive byte-identical. Same temp+rename as
 * commit(), so a half-written config is never readable.
 */
bool        setConfigModel(const std::string& path, const std::string& modelId);
bool        ensureDir(const std::string& path);

/// FNV-1a over the body, 8 hex chars. The dedupe key in the %%ai:%% line.
std::string contentHash32(const std::string& body);

/// Upper bound on a decoded answer body (architecture §4).
constexpr size_t kMaxAnswerBytes = 8u * 1024u;
/// Cap on committed pages.
constexpr int    kMaxPages       = 6;

}  // namespace ai
