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

    // ── Wolfram|Alpha verification hop (architecture §7) ─────────────────────
    // The AppID is BYO and resolved exactly like apiKey: NVS -> config.json ->
    // (none). There is deliberately NO compiled default — a shipped AppID would
    // put one non-commercial allowance behind the whole fleet, and it is a
    // string recoverable with `strings` on a flash dump. Empty = the check is
    // not offered at all.
    std::string waHost      = "https://www.wolframalpha.com";
    std::string waPath      = "/api/v1/llm-api";
    std::string waAppId;                       ///< never a compiled default
    int         waMaxChars  = 1200;            ///< WA's default 6800 is ~25 device pages
    int         waTimeoutMs = 20000;
    /**
     * WA's `units` / `location` / `languagecode`. These are CONFIG, not model
     * output: the model never chooses them, or it could spend the user's monthly
     * allowance on a request shape they did not ask for. Empty = WA's default.
     * The picker lands later; the fields exist now so that is a config write
     * rather than a request rebuild.
     */
    std::string waUnits;
    std::string waLocation;
    std::string waLanguage;

    /// Where the AppID came from, for display. "nvs" | "config" | "none".
    std::string waKeySource() const { return _waKeySource; }
    std::string _waKeySource = "none";
    /**
     * Which transport to use. NOT a user setting any more: on hardware it is
     * fixed to "wifi" (the board's own radio + mbedTLS) by AiConfig::load(), and
     * the field exists only because the transport seam keys off it. On the host
     * and the emulator it stays "replay" (recorded fixture, no network) so the
     * app can be driven headless; a dev config may set "emulator" for libcurl.
     * "device" is kept as a legacy alias for "wifi".
     */
#if defined(ARDUINO)
    std::string transport    = "wifi";     ///< hardware: Wi-Fi + mbedTLS
#else
    std::string transport    = "replay";   ///< host/emulator: recorded fixture
#endif

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

    /**
     * The model's proposed verification hop (schema rev 1 — present in the
     * schema from the first revision, unread until now). `toolQuery()` empty is
     * the common case: no external check applies, so no affordance is offered.
     * The model returns a QUERY STRING, never a URL — the device builds the
     * request, and `toolServer()` says which tool the string is for.
     */
    const std::string& toolQuery() const   { return _toolQuery; }
    const std::string& toolServer() const  { return _toolServer; }
    /// What the model read off the photo. The user judges THIS, because
    /// verification checks arithmetic, not transcription.
    const std::string& transcribedQuestion() const { return _transcribed; }

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
    /**
     * Route one decoded VALUE character to whichever field the current key names.
     * One place instead of four: the value branches are duplicated per branch of
     * the escape machinery, and adding a field to only some of them is exactly
     * how a field ends up silently dropped in one path.
     */
    void pushField(char c);
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
    bool        _inUni = false;       ///< inside a \uXXXX escape, digits still to come
    int         _uniLen = 0;          ///< digits of a \uXXXX escape collected so far
    unsigned    _uniVal = 0;          ///< the \uXXXX value being assembled
    std::string _key;
    std::string _title;
    std::string _answer;
    std::string _toolQuery;
    std::string _toolServer;
    std::string _transcribed;
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

// ═══════════════════════════════════════════════════════════════════════════
// The %%ai: metadata line, written and read back
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Encode/decode ONE value of the trailing `%%ai: key=value key=value%%` line.
 *
 * The line is space-separated `key=value` tokens, and two of the fields the
 * check hop needs (`tool_query`, `transcribed`) are free English with spaces and
 * possibly an `=` in them ("solve x=2"), so a value written raw would be read
 * back as the wrong number of tokens. Anything outside `[A-Za-z0-9._:/-]` is
 * written as %XX, uppercase. The values stay machine-only: the same information
 * is already visible in the document body when a human should read it.
 *
 * `decodeMetaValue` returns false on a malformed escape rather than silently
 * keeping the %XX text.
 */
std::string encodeMetaValue(const std::string& v);
bool        decodeMetaValue(const std::string& v, std::string* out);

/**
 * The `%%ai: …%%` line of a saved answer, read back.
 *
 * This is what makes the Wolfram check reachable from a REOPENED answer: the
 * live run has the scanner's fields in RAM, but a file opened from Recent
 * answers has only what the document kept. `found` is false when the file has no
 * such line at all (an answer saved before this hop existed); a `_Wolfram.md`
 * check document parses like any other line — it just carries no `tool_query`,
 * so it never offers a check of its own.
 */
struct AnswerMeta {
    bool        found = false;
    int         pages = 0;
    std::string model;
    std::string hash;
    std::string image;
    std::string toolQuery;
    std::string toolServer;
    std::string transcribed;
};

/// Parse the LAST `%%ai: …%%` line in `text`. Pure, so it is host-testable.
AnswerMeta parseAnswerMeta(const std::string& text);
/// Same, for a file. A missing file parses to `found == false`.
AnswerMeta readAnswerMeta(const std::string& path);

/**
 * Filename slug for a title: lowercased, <=40 ASCII, filesystem-safe. Free
 * rather than a member so the Wolfram check document can derive the SAME slug
 * from the answer it belongs to — `<slug>_Wolfram.md` has to sit beside
 * `<slug>.md` or the pairing is guesswork.
 */
std::string slugify(const std::string& title);

/// Upper bound on a decoded answer body (architecture §4).
constexpr size_t kMaxAnswerBytes = 8u * 1024u;
/// Cap on committed pages.
constexpr int    kMaxPages       = 6;

}  // namespace ai
