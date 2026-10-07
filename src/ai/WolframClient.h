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
 * WolframClient.h — the Wolfram|Alpha verification hop.
 *
 * THE PREMISE: the LLM hallucinates. The answer is a draft; a Wolfram check is
 * the user's own check on it, not a feature the model drives. So the model only
 * ever PROPOSES a query (the `tool_query` / `tool_server` fields in the answer
 * schema) and the USER triggers the call. Two consequences are load-bearing:
 *
 *  1. The model returns a QUERY STRING, never a URL and never request
 *     parameters. The device owns scheme, host, path, maxchars, units and
 *     location. A model free to shape the URL is an SSRF/encoding surface, and a
 *     model free to set maxchars can spend the user's monthly allowance on one
 *     call.
 *  2. The keypress is the QUOTA GATE. Wolfram's free tier is a monthly cap; an
 *     explicit keypress is what stops every scan draining it.
 *
 * WHY THE LLM API AND NOT SHORT ANSWERS. `GET /v1/result` returns one bare
 * sentence and OMITS `Input interpretation:`. That line is the whole feature: it
 * is what the user compares the model's transcription against. Without it a
 * "check" is a second answer with no way to tell whether the two agree on the
 * question. It is also unpaginatable — one line cannot fill a 176 px page.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * WHAT MAY AND MAY NOT BE STORED
 * ═══════════════════════════════════════════════════════════════════════════
 * The Wolfram|Alpha API Terms of Use, verbatim: "Your API Client is prohibited
 * from caching Wolfram|Alpha content." The Prohibitions clause bars "access,
 * cache, store, retain or in any way compile any copies or portion of any
 * Wolfram|Alpha content".
 *
 * So this client draws a hard line:
 *   - WA's returned TEXT (interpretation, body) is held in RAM for the run,
 *     rendered on screen, then DROPPED. Display is the licensed use — the terms
 *     describe Results as content "displayed by, or otherwise used within" the
 *     API Client.
 *   - The sibling check document stores the QUERY (the user's own input), the
 *     model's transcription (our own model's output) and the RESULTS-PAGE LINK.
 *     The link is not a concession, it is an obligation: the same terms require
 *     "a conspicuous hyperlink directly to the corresponding results page of the
 *     Wolfram|Alpha website on every page with Results", and attribution by
 *     "a direct link to the specific Wolfram|Alpha result page from which the
 *     content was derived".
 *   - No WA text is ever written to flash. buildWolframDoc() cannot even accept
 *     it: it takes the link, not the result.
 *
 * BYO AppID, direct, no relay. A user's own AppID relayed through a host of ours
 * is an open relay carrying that host and its reputation, and buys nothing.
 * There is deliberately NO compiled default: an AppID shipped in firmware would
 * put our non-commercial allowance behind the whole fleet (voiding it) and is a
 * string recoverable with `strings` on a flash dump. No AppID configured = the
 * check is not offered at all.
 *
 * LVGL-FREE, like AiClient.h: a host test compiles the pure half with
 * `g++ -I src` and no LVGL, no socket, no network.
 *
 * Design record: ~/musings/sserialprintthing/ai-wrapper-architecture.md
 * Build record:  ~/musings/sserialprintthing/ai-wrapper-implement.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ai/AiClient.h"

namespace ai {

// ═══════════════════════════════════════════════════════════════════════════
// Wire constants
// ═══════════════════════════════════════════════════════════════════════════

/// Upper bound on a Wolfram response body we hold. `maxchars` bounds the result
/// text, but the body also carries `image:` lines and the trailing website
/// block, so the cap is defensive rather than derived.
constexpr size_t kMaxWaBytes = 8u * 1024u;

/// Appended to the answer's slug for the sibling check document.
constexpr const char* kWolframSlugSuffix = "_Wolfram";

// ═══════════════════════════════════════════════════════════════════════════
// Raw byte accumulator
// ═══════════════════════════════════════════════════════════════════════════

/**
 * The response body, held as raw bytes.
 *
 * On hardware this is PSRAM (agents.md §5.2): the ~328 KB internal SRAM also
 * carries the mbedTLS content buffers, and a result body parked there next to a
 * live TLS session is how a handshake starts failing on the boards that work.
 * On the host it is plain malloc, so the emulator and this test behave the same.
 *
 * A raw pointer rather than std::string so the two targets have ONE code path
 * instead of a std::string one that silently violates the rule on device.
 */
class WaBuffer {
public:
    WaBuffer() = default;
    ~WaBuffer() { reset(); }
    WaBuffer(const WaBuffer&) = delete;
    WaBuffer& operator=(const WaBuffer&) = delete;

    /// Allocate `bytes`. PSRAM on device, malloc on the host. False = no room.
    bool reserve(size_t bytes);
    /// Append, up to cap(). False once the body is at cap.
    bool append(const char* d, size_t n);
    void reset();

    const char* data() const { return _p; }
    size_t      size() const { return _len; }
    size_t      cap()  const { return _cap; }
    bool       full()  const { return _len >= _cap; }

    /// Copy out. The one place a std::string of the body exists, and it is
    /// short-lived: it is parsed and dropped.
    std::string str() const { return _p ? std::string(_p, _len) : std::string(); }

private:
    char*  _p   = nullptr;
    size_t _cap = 0;
    size_t _len = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// The pure half — no socket, host-testable
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Percent-encode for a query-string value. Unreserved (RFC 3986) characters pass
 * through; everything else becomes %XX, uppercase. Space becomes %20 — not '+',
 * which this API reads literally inside a single-line English `input`.
 */
std::string percentEncode(const std::string& in);

/**
 * GET {waHost}{waPath}?input=...&maxchars=...&[units=]&[location=]&[languagecode=]
 *
 * THE APPID IS NOT IN HERE. It rides in the Authorization header, which the
 * transport builds, so it stays out of URLs, out of server logs and out of any
 * error string. Pure: the whole request shape is verifiable with no socket.
 */
std::string buildWolframUrl(const AiConfig& cfg, const std::string& input);

/**
 * Digits-only editing of the proposed query — the check screen's model.
 *
 * WHY DIGITS ONLY. An fx-82 keypad has no letter keys, so free-text editing is
 * not achievable; the user corrects the NUMBERS ("derivative of x^3" becomes
 * "derivative of x^4") and leaves the rest of the model's wording alone. That is
 * the entire edit surface, so it lives here as a pure string operation with host
 * coverage instead of inside a screen where it can only be checked by eye.
 *
 * The caret is an index in [0, size()]. LEFT/RIGHT walk it, a digit INSERTS at
 * it, DEL rubs out the character BEFORE it, and restore() puts the model's
 * original back — which is what the check screen's AC does.
 */
class WaQueryEdit {
public:
    /// Longest editable query. The device owns the URL, and a query past this is
    /// not a question — the cap also keeps the answer's metadata line bounded.
    static constexpr size_t kMaxChars = 120;

    /// Start a new edit on the model's proposal; the caret starts at the end,
    /// which is where a number the user wants to change usually is.
    void reset(const std::string& proposed);

    const std::string& original() const { return _original; }
    const std::string& text()     const { return _text; }
    int  caret() const { return _caret; }
    bool empty() const { return _text.empty(); }
    /// True when the text differs from what the model proposed.
    bool edited() const { return _text != _original; }

    bool moveLeft();
    bool moveRight();
    /// Insert a digit at the caret. Any other character is refused.
    bool insert(char c);
    /// Rub out the character before the caret.
    bool backspace();
    /// Back to the model's original, caret at its end.
    void restore();

private:
    std::string _original;
    std::string _text;
    int         _caret = 0;
};

/**
 * What went wrong, as a value.
 *
 * The published error table is STALE and must not be coded to. Probed live: no
 * appid at all answers **400** with a bare 13-byte `Appid Missing` body, and a
 * bad appid answers **401** `Invalid appid` whether it arrives as the `appid`
 * parameter or as the Bearer header. The docs claim 403 for both — it never
 * occurred — and describe 400 as "no input parameter", which is the missing-appid
 * case instead. So the BODY decides and the status only breaks ties.
 *
 * 501 (uninterpretable input, suggested inputs in the body) is documented but
 * UNVERIFIED: the vendor's public `appid=DEMO` never gets past auth, so no probe
 * reaches that path. Kept because the docs are explicit about it.
 */
enum class WaStatus : uint8_t {
    Ok,                ///< a usable result body
    MissingAppId,      ///< 400 — body `Appid Missing`
    InvalidAppId,      ///< 401 — body `Invalid appid`
    NotInterpretable,  ///< 501 — WA could not read the query (body may suggest inputs)
    Forbidden,         ///< 403 — never observed; kept so an unknown 403 is nameable
    RateLimited,       ///< 429
    BadRequest,        ///< other 4xx
    ServerError,       ///< 5xx
    EmptyBody,         ///< 200 with nothing usable in it
    TooLarge,          ///< body hit kMaxWaBytes
    Transport,         ///< never reached HTTP
};

const char* waStatusName(WaStatus s);

/// A sentence a user can act on. Never contains the AppID.
const char* waStatusText(WaStatus s);

/**
 * Classify a finished response. `msgOut`, when given, receives a bounded tail of
 * the provider's own explanation — that body is what makes a 4xx diagnosable and
 * it never contains the key.
 */
WaStatus classifyWolfram(int http, const std::string& body, std::string* msgOut = nullptr);

/**
 * The parsed response, split at the seams that matter.
 *
 * Nothing here is ever persisted — see the caching note at the top of this file.
 */
struct WolframResult {
    /// WA's own reading of the query. THE VERIFICATION UX: the user compares
    /// this against the model's transcription, so a misread shows up instead of
    /// hiding behind a correctly-computed wrong problem.
    std::string interpretation;
    /// The direct results-page URL. Required by the terms, and the one piece of
    /// the response the check document is allowed to keep.
    std::string link;
    /// The remainder, cleaned for display: `image:` lines dropped, headings left
    /// orphaned by those drops removed, blank runs collapsed. DISPLAY ONLY.
    std::string body;
};

/**
 * Parse WA's `text/plain` body.
 *
 * The real shape, taken from the vendor's own worked example — and NOT what this
 * project's earlier notes said:
 *
 *     Query:
 *     "10 densest elemental metals"
 *
 *     Input interpretation:
 *     10 densest metallic elements | by mass density
 *
 *     Result:
 *     1 | hassium | 41 g/cm^3 |
 *     ...
 *
 *     Periodic table location:
 *     image: https://www6b3.wolframalpha.com/...
 *
 *     Wolfram|Alpha website result for "10 densest elemental metals":
 *     https://www.wolframalpha.com/input?i=10+densest+elemental+metals
 *
 * Three traps a naive line filter walks into:
 *  - The body OPENS with `Query:`, so a parser hunting `Input interpretation:`
 *    first mis-anchors.
 *  - `image: <url>` lines sit UNDER a heading ("Periodic table location:").
 *    Dropping the line leaves an empty heading, so the heading goes with it.
 *  - The trailing attribution is TWO lines — the label, then the URL. Matching
 *    only the label strands the URL as stray body text.
 *
 * The `|` separators WA uses inside a result block are LEFT ALONE: without a
 * delimiter row md4c does not read them as a table, and collapsing them would
 * invent punctuation WA did not send.
 */
WolframResult parseWolframBody(const std::string& raw);

/**
 * The sibling check document — query, transcription and link. Never WA's text.
 *
 * Visible: the query as actually sent (post-edit — the user must see what was
 * asked, not what the model originally proposed), the model's transcription when
 * there was one, and the results-page link the terms require.
 *
 * Not rendered: the source answer slug, the dedupe hash and the model, in the
 * trailing `%%ai: ...%%` line the parser strips and Obsidian hides — so the file
 * stays valid Markdown with no frontmatter and no new whitelist key.
 */
std::string buildWolframDoc(const std::string& sourceSlug,
                            const std::string& query,
                            const std::string& transcribed,
                            const std::string& link,
                            const std::string& model);

/**
 * Write `<slugBase>_Wolfram.md` into cfg.resultsDir.
 *
 * Atomic temp+rename so a dropped connection leaves no half-file, with the plain
 * overwrite fallback because LittleFS's rename() does not reliably replace an
 * existing destination. ONE check file per answer: a second press on the same
 * answer overwrites rather than creating `_Wolfram2` — it is a check, not a log.
 * An identical doc (same content hash) is never rewritten.
 */
bool saveWolframDoc(const AiConfig& cfg, const std::string& slugBase,
                    const std::string& doc, std::string* pathOut = nullptr);

// ═══════════════════════════════════════════════════════════════════════════
// The fetch
// ═══════════════════════════════════════════════════════════════════════════

/**
 * One Wolfram check. Shaped like AiTransport (begin / pump / abort) so the app's
 * worker task drives it the way it drives the answer stream — but NOT the same
 * scanner: this is a single `text/plain` body, no SSE framing, no deltas.
 * Forcing it through AiScanner would be the bug, not the reuse.
 *
 * Threading: on hardware `begin()` blocks through the TLS handshake, exactly as
 * DeviceTransport::open() already does, and pump() reads a bounded chunk. Drive
 * it from the shared network worker task, never from a UI callback.
 */
class WolframClient {
public:
    WolframClient() = default;
    ~WolframClient();

    /**
     * Resolve the request and open the socket. `query` is the WA input as it
     * will be sent — the model's `tool_query`, after any user edit. Fails with a
     * NAMED reason for every prerequisite (no AppID, no Wi-Fi, unsynced clock,
     * no room for the body).
     */
    bool begin(const AiConfig& cfg, const std::string& query);

    /// Pump once. false = finished; then check failed() / status().
    bool pump();

    bool finished() const { return _done; }
    bool failed()   const { return !_err.empty(); }
    const std::string& error() const { return _err; }

    WaStatus status()     const { return _status; }
    int      httpStatus() const { return _http; }

    /// Parsed result. Valid when finished() && !failed(). RAM only — the caller
    /// renders it and drops it. Never persist `interpretation` or `body`.
    const WolframResult& result() const { return _result; }

    /// The exact input sent (post-edit). This IS persistable: it is the user's
    /// own string, not Wolfram's.
    const std::string& query() const { return _query; }

    /// The URL actually requested. Never contains the AppID. Diagnostics only.
    const std::string& url() const { return _url; }

    /// Bytes the server delivered. Diagnostics only.
    size_t bytes() const { return _bytes; }

    /**
     * The raw body as it has arrived so far — for the live "checking…" card,
     * which shows what WA has sent before the transfer ends. RAM only; the
     * pointer is valid until the next begin()/abort(). DISPLAY ONLY: this is
     * Wolfram's content and the terms prohibit caching it, so nothing here may
     * be written to flash (see the note at the top of the header).
     */
    const char* rawData() const { return _raw.data(); }
    size_t      rawSize() const { return _raw.size(); }

    /// Abort: nothing is written, no file is produced.
    void abort();

private:
    AiConfig      _cfg;
    std::string   _query;
    std::string   _url;
    std::string   _err;
    WolframResult _result;
    WaBuffer      _raw;
    WaStatus      _status = WaStatus::Ok;
    int           _http   = 0;
    size_t        _bytes  = 0;
    bool          _done   = false;
    /// Transport state. Opaque so this header names no curl, Arduino or
    /// esp_http_client type — the .cpp owns the real one.
    void*         _impl   = nullptr;
};

}  // namespace ai
