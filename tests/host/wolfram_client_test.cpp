// wolfram_client_test.cpp — host regression guard for src/ai/WolframClient.{h,cpp}
// and for the scanner fields this hop depends on.
//
// Why this file exists: three things here fail SILENTLY.
//   1. The response parser. WA's body opens with a `Query:` block, `image:` URLs
//      sit UNDER a heading, and the trailing attribution is TWO lines. Every one
//      of those was wrong in this project's earlier notes, and every one of them
//      fails as "the check shows nothing useful" rather than as an error.
//   2. The error classifier. The published table is stale (it claims 403 for
//      both appid errors; the wire says 400 and 401) and the failure body is 13
//      bytes of plain text, so a status-code switch classifies nothing.
//   3. The scanner fields. `tool_query` / `tool_server` /
//      `transcribed_question` were in the schema from rev 1 but unread, and a
//      value branch missing from one escape path looks exactly like success.
//
// Standalone host test: no LVGL, no PlatformIO, no network.
//
// Build (from the repo root):
//   g++ -std=gnu++17 -I src tests/host/wolfram_client_test.cpp \
//       src/ai/WolframClient.cpp src/ai/AiClient.cpp src/hal/FileSystem.cpp \
//       -o /tmp/wolfram_client_test && /tmp/wolfram_client_test
// Exit codes: 0 pass, 1 a check regressed.

#include <cstdio>
#include <string>

#include "ai/AiClient.h"
#include "ai/WolframClient.h"
#include "hal/FileSystem.h"   // the LittleFS shim's global, for setRoot()

static int g_failures = 0;
static int g_checks   = 0;

static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static void expectStr(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        std::printf("FAIL: %-46s got '%s', want '%s'\n", what, got.c_str(), want.c_str());
        ++g_failures;
    }
}

static void expectInt(long got, long want, const char* what) {
    ++g_checks;
    if (got != want) {
        std::printf("FAIL: %-46s got %ld, want %ld\n", what, got, want);
        ++g_failures;
    }
}

static bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// ── The vendor's own worked example, byte-for-byte from the LLM API docs. ─────
// Using the REAL body matters: a hand-written sample would have been written to
// match the parser, which is the bug this test exists to catch.
static const char* kRealBody =
    "Query:\n"
    "\"10 densest elemental metals\"\n"
    "\n"
    "Input interpretation:\n"
    "10 densest metallic elements | by mass density\n"
    "\n"
    "Result:\n"
    "1 | hassium | 41 g/cm^3 |\n"
    "2 | meitnerium | 37.4 g/cm^3 |\n"
    "3 | bohrium | 37.1 g/cm^3 |\n"
    "\n"
    "Periodic table location:\n"
    "image: https://www6b3.wolframalpha.com/Calculate/MSP/MSP3361h163543iihhdd83000025dfc938e28eg72b?MSPStoreType=image/png&s=6\n"
    "\n"
    "Images:\n"
    "image: https://www6b3.wolframalpha.com/Calculate/MSP/MSP3371h163543iihhdd8300001ad5c0fci021g8e5?MSPStoreType=image/png&s=6\n"
    "\n"
    "Basic elemental properties:\n"
    "atomic symbol | all | Bh  |  Db  |  Ds  |  Hs  |  Ir  |  Mt  |  Os\n"
    "atomic number | median | 106.5\n"
    "| highest | 111  (roentgenium)\n"
    "\n"
    "Wolfram|Alpha website result for \"10 densest elemental metals\":\n"
    "https://www.wolframalpha.com/input?i=10+densest+elemental+metals\n";

int main() {
    std::printf("== wolfram_client_test ==\n");
    LittleFS.setRoot("tests/emulator/fs");
    LittleFS.begin();

    ai::AiConfig cfg;                       // compiled defaults: no AppID, keyless

    // ── 1. percentEncode — strict RFC 3986 unreserved only ──────────────────
    // `(`, `)` and `*` are NOT unreserved, so they ARE percent-encoded. That is
    // correct and safe: the server decodes them. Spaces are %20, never '+', which
    // this API reads literally inside its single-line English `input`.
    {
        expectStr(ai::percentEncode("integrate x^2 sin(x) dx"),
                  "integrate%20x%5E2%20sin%28x%29%20dx", "encode: spaces are %20, not '+'");
        expectStr(ai::percentEncode("a-b_c.d~e"), "a-b_c.d~e", "encode: unreserved passes through");
        expectStr(ai::percentEncode("6*10^14"), "6%2A10%5E14",
                  "encode: the exponent rule survives the encoding");
        expectStr(ai::percentEncode("^&=%/?#+"), "%5E%26%3D%25%2F%3F%23%2B", "encode: reserved is hex'd");
        expectStr(ai::percentEncode(""), "", "encode: empty stays empty");
    }

    // ── 2. buildWolframUrl — and the AppID is NOT in it ──────────────────────
    {
        cfg.waAppId = "SUPERSECRET-1234";
        cfg.waUnits = "metric";
        const std::string url = ai::buildWolframUrl(cfg, "1+1");
        expectStr(url,
                  "https://www.wolframalpha.com/api/v1/llm-api?input=1%2B1&maxchars=1200&units=metric",
                  "url: built from config defaults");
        check(!has(url, "SUPERSECRET"),
              "url: the AppID is NOT in the URL (bearer header only)");
        check(!has(url, "appid="), "url: no appid parameter at all");
        check(has(url, "maxchars=1200"),
              "url: maxchars from config, never the model's 6800 default");

        ai::AiConfig bare;
        expectStr(ai::buildWolframUrl(bare, "pi"),
                  "https://www.wolframalpha.com/api/v1/llm-api?input=pi&maxchars=1200",
                  "url: optional params absent when config is empty");
    }

    // ── 3. The error classifier — body first, status only breaks ties ───────
    {
        expectInt((int)ai::classifyWolfram(400, "Appid Missing\n"),
                  (int)ai::WaStatus::MissingAppId, "error: 400 Appid Missing -> MissingAppId");
        expectInt((int)ai::classifyWolfram(401, "Invalid appid\n"),
                  (int)ai::WaStatus::InvalidAppId, "error: 401 Invalid appid -> InvalidAppId");
        // The published table says 403 for both. The wire says 400/401. Classify
        // on the BODY so whichever the server sends lands in the same place.
        expectInt((int)ai::classifyWolfram(403, "Appid Missing\n"),
                  (int)ai::WaStatus::MissingAppId, "error: 403 + Appid Missing body still classifies");
        expectInt((int)ai::classifyWolfram(403, ""),
                  (int)ai::WaStatus::Forbidden, "error: a bare 403 is nameable");
        expectInt((int)ai::classifyWolfram(501, "Wolfram|Alpha did not understand your input"),
                  (int)ai::WaStatus::NotInterpretable, "error: 501 -> NotInterpretable");
        expectInt((int)ai::classifyWolfram(429, "slow down"),
                  (int)ai::WaStatus::RateLimited, "error: 429 -> RateLimited");
        expectInt((int)ai::classifyWolfram(500, "internal"),
                  (int)ai::WaStatus::ServerError, "error: 5xx -> ServerError");
        expectInt((int)ai::classifyWolfram(418, "teapot"),
                  (int)ai::WaStatus::BadRequest, "error: other 4xx -> BadRequest");
        expectInt((int)ai::classifyWolfram(0, ""),
                  (int)ai::WaStatus::Transport, "error: no status -> Transport");
        expectInt((int)ai::classifyWolfram(200, "   \n"),
                  (int)ai::WaStatus::EmptyBody, "error: 200 with no usable body");
        expectInt((int)ai::classifyWolfram(200, kRealBody),
                  (int)ai::WaStatus::Ok, "error: a real body is Ok");

        std::string detail;
        ai::classifyWolfram(401, "Invalid appid\n", &detail);
        check(!detail.empty(), "error: the provider body tail is surfaced for diagnosis");
        check(!has(detail, "SUPERSECRET"), "error: the detail carries no key material");
    }

    // ── 4. parseWolframBody against the docs' real body ─────────────────────
    {
        const ai::WolframResult r = ai::parseWolframBody(kRealBody);

        expectStr(r.interpretation, "10 densest metallic elements | by mass density",
                  "parse: Input interpretation is captured (the verification UX)");
        expectStr(r.link, "https://www.wolframalpha.com/input?i=10+densest+elemental+metals",
                  "parse: the TWO-line trailing attribution yields the link");
        check(!has(r.body, "Wolfram|Alpha website result"),
              "parse: the attribution label is not left in the body");
        check(!has(r.body, "https://www.wolframalpha.com/input"),
              "parse: the link is not duplicated into the body");
        check(!has(r.body, "image:"), "parse: image lines are dropped (the whitelist bans them)");
        check(!has(r.body, "www6b3.wolframalpha.com"),
              "parse: no image URL survives anywhere in the body");
        check(!has(r.body, "Periodic table location"),
              "parse: a heading orphaned by its only-image content is dropped WITH it");
        check(!has(r.body, "Images:"), "parse: the Images section goes too");
        check(!has(r.body, "Query:"), "parse: the leading Query block is dropped");
        check(!has(r.body, "\"10 densest elemental metals\""),
              "parse: the quoted query echo is dropped");
        check(has(r.body, "Result:"), "parse: a real section keeps its heading");
        check(has(r.body, "Basic elemental properties:"), "parse: later sections survive");
        check(has(r.body, "| highest | 111  (roentgenium)"),
              "parse: WA's leading-pipe continuation lines are left verbatim");
        check(!has(r.body, "\n\n\n"), "parse: blank runs are collapsed (176 px pages)");

        // A 200 whose body has none of the known sections still yields something
        // rather than a blank page.
        const ai::WolframResult odd = ai::parseWolframBody("something unexpected\n");
        check(!odd.body.empty(), "parse: an unrecognised body still renders as text");

        const ai::WolframResult empty = ai::parseWolframBody("");
        check(empty.body.empty() && empty.link.empty() && empty.interpretation.empty(),
              "parse: an empty body yields an empty result, not a stray newline");
    }

    // ── 5. The check document stores the query and link, NEVER WA's text ────
    {
        const std::string doc = ai::buildWolframDoc(
            "quadratic-roots", "solve x^2-5x+6=0", "x^2 - 5x + 6 = 0",
            "https://www.wolframalpha.com/input?i=solve+x%5E2-5x%2B6%3D0",
            "google/gemini-2.5-flash-lite");

        check(has(doc, "**Query:** solve x^2-5x+6=0"),
              "doc: the query AS SENT is visible (post-edit, not the model's original)");
        check(has(doc, "_Read from the photo as:_ x^2 - 5x + 6 = 0"),
              "doc: the model's transcription rides along — verification checks"
              " arithmetic, not transcription");
        check(has(doc, "https://www.wolframalpha.com/input?i=solve+x%5E2-5x%2B6%3D0"),
              "doc: the results-page link is present (the terms require it)");
        check(has(doc, "%%ai: kind=wolfram"), "doc: machine metadata rides in the stripped comment");
        check(has(doc, "source=quadratic-roots"), "doc: the source answer slug is recorded");
        check(has(doc, "hash="), "doc: a dedupe hash is present");

        // THE LICENCE LINE. The WA ToU prohibits caching WA content, so the
        // document must not be able to carry it — buildWolframDoc takes the link,
        // not the result, and this asserts that no result text leaked in.
        check(!has(doc, "Input interpretation"),
              "doc: WA's own result text is NOT stored (caching is prohibited)");
        check(!has(doc, "g/cm^3"), "doc: no WA content in the check document");
        check(!has(doc, "image:"), "doc: no image URLs from WA");

        const std::string noTrans = ai::buildWolframDoc("a", "1+1", "", "https://x/", "m");
        check(!has(noTrans, "_Read from the photo as:_"),
              "doc: no empty transcription label when the model read nothing");
    }

    // ── 6. saveWolframDoc: atomic write, dedupe, one file per answer ───────
    {
        ai::AiConfig c;
        c.resultsDir = "/ai/results";
        const std::string doc = ai::buildWolframDoc(
            "seed-derivative", "derivative of x^2", "d/dx x^2",
            "https://www.wolframalpha.com/input?i=derivative+of+x%5E2", "test/model");

        std::string p1, p2;
        check(ai::saveWolframDoc(c, "seed-derivative", doc, &p1), "save: first write succeeds");
        expectStr(p1, "/ai/results/seed-derivative_Wolfram.md",
                  "save: one check file per answer, named from the slug");
        check(!LittleFS.exists("/ai/results/seed-derivative_Wolfram.md.tmp"),
              "save: no temp file left behind");

        // Same content = nothing to rewrite. This is the dedupe path, and on
        // LittleFS the rename() into an existing destination is exactly what
        // fails if the fallback is missing.
        check(ai::saveWolframDoc(c, "seed-derivative", doc, &p2), "save: re-save succeeds");
        expectStr(p2, p1, "save: an identical check reuses the same path");

        // A different query must overwrite the SAME file, not make _Wolfram2.
        const std::string doc2 = ai::buildWolframDoc(
            "seed-derivative", "derivative of x^3", "d/dx x^3",
            "https://www.wolframalpha.com/input?i=derivative+of+x%5E3", "test/model");
        std::string p3;
        check(ai::saveWolframDoc(c, "seed-derivative", doc2, &p3), "save: second query overwrites");
        expectStr(p3, p1, "save: still one file — a check, not a log");
        check(!LittleFS.exists("/ai/results/seed-derivative_Wolfram2.md"),
              "save: no _Wolfram2 is ever created");
        check(has(ai::readTextFile(p3), "derivative of x^3"),
              "save: the file on disk holds the newer query");
        check(!has(ai::readTextFile(p3), "derivative of x^2"),
              "save: the older query is gone, not appended");
    }

    // ── 7. The scanner now reads the schema fields the hop depends on ──────
    {
        // Build a REAL stream: ONE JSON payload, sliced at arbitrary byte
        // boundaries, each slice JSON-escaped into its own SSE envelope — which
        // is what a provider actually sends.
        //
        // A hand-written payload-per-delta is not a valid split of one document:
        // the deltas are fragments of a single JSON text, and the scanner is
        // stateful across them for exactly that reason. Feeding it whole
        // well-formed fragments proves nothing, and (learned here) appears to
        // fail for a reason that has nothing to do with the scanner.
        const std::string payload =
            "{\"title\":\"Derivative\",\"answer\":\"x^2\","
            "\"followups\":[\"one\",\"two\",\"three\"],\"confidence\":0.9,"
            "\"tool_query\":\"derivative of x^2\",\"tool_server\":\"wolfram\","
            "\"transcribed_question\":\"d/dx x^2\"}";

        auto jsonEsc = [](const std::string& s) {
            std::string o;
            for (const char c : s) {
                if (c == '"' || c == '\\') { o += '\\'; o += c; }
                else if (c == '\n')        o += "\\n";
                else                       o += c;
            }
            return o;
        };
        auto enclose = [&](const std::string& frag) {
            return std::string("data: {\"choices\":[{\"delta\":{\"content\":\"") +
                   jsonEsc(frag) + "\"}}]}\n";
        };

        std::string sse;
        const size_t step = 11;                 // splits tokens AND escapes mid-way
        for (size_t i = 0; i < payload.size(); i += step) {
            sse += enclose(payload.substr(i, step));
            if (i == 0) sse += ": OPENROUTER PROCESSING\n";   // the classic crash
        }
        sse += "data: [DONE]\n";

        ai::AiScanner sc;
        for (size_t i = 0; i < sse.size(); i += 3) sc.feed(sse.substr(i, 3));
        sc.finish();

        expectStr(sc.title(), "Derivative", "scanner: title");
        expectStr(sc.answer(), "x^2", "scanner: answer");
        expectStr(sc.toolQuery(), "derivative of x^2",
                  "scanner: tool_query is READ (in the schema since rev 1, unread until now)");
        expectStr(sc.toolServer(), "wolfram", "scanner: tool_server is READ");
        expectStr(sc.transcribedQuestion(), "d/dx x^2",
                  "scanner: transcribed_question is READ — this is what the user judges");
        check(sc.sawDone(), "scanner: [DONE] seen despite the interleaved comment line");
        check(!has(sc.answer(), "tool_query"), "scanner: the new fields did not fold into the answer");
        check(!has(sc.title(), "answer"), "scanner: the title did not pick up the next key");

        // An answer with no external check is the COMMON case: empty, not garbage.
        const std::string pay2 =
            "{\"title\":\"T\",\"answer\":\"A\",\"tool_query\":\"\",\"tool_server\":\"\"}";
        std::string sse2;
        for (size_t i = 0; i < pay2.size(); i += 7) sse2 += enclose(pay2.substr(i, 7));
        sse2 += "data: [DONE]\n";
        ai::AiScanner sc2;
        for (size_t i = 0; i < sse2.size(); i += 5) sc2.feed(sse2.substr(i, 5));
        sc2.finish();
        expectStr(sc2.title(), "T", "scanner: no-check answer still decodes");
        expectStr(sc2.answer(), "A", "scanner: no-check answer body");
        expectStr(sc2.toolQuery(), "", "scanner: an empty tool_query stays empty");
        expectStr(sc2.transcribedQuestion(), "", "scanner: an absent field stays empty");

        // The real fixture, so the test and the app agree on the same bytes.
        const std::string raw = ai::readTextFile("/ai/replay/answer.sse");
        if (!raw.empty()) {
            ai::AiScanner sc3;
            for (size_t i = 0; i < raw.size(); i += 37) sc3.feed(raw.substr(i, 37));
            sc3.finish();
            expectStr(sc3.title(), "Quadratic roots", "fixture: title still decodes");
            expectStr(sc3.toolQuery(), "", "fixture: this answer proposes no external check");
            check(!has(sc3.answer(), "transcribed_question"),
                  "fixture: the new fields stay out of the answer");
        }
    }

    // ── 8. Config: the AppID resolves like the provider key, and never compiles in
    {
        const ai::AiConfig loaded = ai::AiConfig::load("/ai/config.json");
        check(loaded.waAppId.empty(), "config: keyless on the host (no compiled AppID)");
        expectStr(loaded.waKeySource(), "none", "config: wa key source reports none");
        expectStr(loaded.waHost, "https://www.wolframalpha.com", "config: default host");
        expectInt(loaded.waMaxChars, 1200, "config: maxchars default");
        check(loaded.waUnits.empty(), "config: units unset until the picker lands");
    }

    // ── 9. The digits-only query edit (the check screen's model) ─────────────
    {
        ai::WaQueryEdit e;
        e.reset("derivative of x^3");
        expectStr(e.text(), "derivative of x^3", "edit: starts as the model's proposal");
        expectStr(e.original(), "derivative of x^3", "edit: the original is kept for AC");
        expectInt(e.caret(), 17, "edit: the caret starts at the end");
        check(!e.edited(), "edit: untouched text is not an edit");

        // A digit typed at the caret.
        check(e.insert('4'), "edit: a digit is accepted");
        expectStr(e.text(), "derivative of x^34", "edit: the digit lands at the caret");
        check(e.edited(), "edit: it now counts as an edit");

        // Letters are not on the keypad, so they are not accepted here either.
        check(!e.insert('a'), "edit: a letter is refused");
        check(!e.insert(' '), "edit: a space is refused");
        check(!e.insert('-'), "edit: a sign is refused (digits only)");

        // The caret walks, and an insert goes where it stands.
        check(e.moveLeft(), "edit: the caret moves left");
        check(e.insert('9'), "edit: a digit mid-string is accepted");
        expectStr(e.text(), "derivative of x^394", "edit: the digit went in AT the caret");

        // Rub-out takes the character BEFORE the caret.
        check(e.backspace(), "edit: DEL rubs out");
        expectStr(e.text(), "derivative of x^34", "edit: DEL removed the character before the caret");

        // AC: back to what the model proposed, caret at the end.
        e.restore();
        expectStr(e.text(), "derivative of x^3", "edit: AC restores the model's original");
        check(!e.edited(), "edit: restored means not edited");
        expectInt(e.caret(), 17, "edit: restore puts the caret back at the end");

        // Edges: nothing to rub out at 0, and the caret cannot go negative.
        ai::WaQueryEdit z;
        z.reset(std::string());
        check(!z.backspace(), "edit: DEL at an empty query is refused");
        check(!z.moveLeft(), "edit: the caret will not go below 0");
        check(z.insert('2'), "edit: the first digit into an empty query is accepted");
        expectStr(z.text(), "2", "edit: and it is the whole query");
        check(!z.moveRight(), "edit: the caret will not go past the end");
        check(!z.empty(), "edit: a typed query is not empty");

        // The cap: past it, nothing more goes in.
        ai::WaQueryEdit big;
        big.reset(std::string(ai::WaQueryEdit::kMaxChars, '7'));
        check(!big.insert('1'), "edit: the character cap is enforced");

        // The edit is what gets SENT: it must be the URL's input.
        ai::AiConfig c2;
        ai::WaQueryEdit sent;
        sent.reset("derivative of x^3");
        sent.backspace();          // "derivative of x^"
        sent.insert('4');
        expectStr(ai::buildWolframUrl(c2, sent.text()),
                  "https://www.wolframalpha.com/api/v1/llm-api?input=derivative%20of%20x%5E4&maxchars=1200",
                  "edit: the checked query is the edited one, not the proposal");
    }

    // ── 10. The %%ai: metadata line — encode, decode, read back ─────────────
    {
        // Values with spaces and '=' cannot ride raw: the line is space-separated
        // key=value tokens, so a raw value would be read as several tokens.
        expectStr(ai::encodeMetaValue("derivative of x^3"), "derivative%20of%20x%5E3",
                  "meta: spaces and '^' are encoded");
        expectStr(ai::encodeMetaValue("solve x=2"), "solve%20x%3D2",
                  "meta: '=' is encoded (or the token splits in two)");
        expectStr(ai::encodeMetaValue("a/b_c.d-e:f+g"), "a/b_c.d-e:f+g",
                  "meta: the safe alphabet passes through");
        expectStr(ai::encodeMetaValue("100%"), "100%25", "meta: '%' itself is encoded");

        std::string back;
        const std::string nasty = "d/dx x^3 = 3x^2 (100% of it)";
        check(ai::decodeMetaValue(ai::encodeMetaValue(nasty), &back),
              "meta: a nasty value decodes");
        expectStr(back, nasty, "meta: and it is byte-identical to what went in");

        // A malformed escape is reported, never half-decoded.
        check(!ai::decodeMetaValue("%2", &back), "meta: a truncated escape fails");
        check(!ai::decodeMetaValue("100%ZZ", &back), "meta: a non-hex escape fails");

        // The real seed answer: this is what a REOPENED answer has to offer.
        const ai::AnswerMeta m = ai::readAnswerMeta("/ai/results/seed-derivative.md");
        check(m.found, "meta: the seed answer's line is parsed");
        expectStr(m.toolQuery, "derivative of x^3", "meta: the query round-trips");
        expectStr(m.toolServer, "wolfram", "meta: the server round-trips");
        expectStr(m.transcribed, "d/dx x^3", "meta: the transcription round-trips");
        expectStr(m.model, "google/gemini-2.5-flash-lite", "meta: the model still parses");
        expectInt(m.pages, 2, "meta: pages still parses");

        // A check document parses too — it carries `hash=` and `model=` — but it
        // proposes no query, so no check is ever offered off its own back.
        const ai::AnswerMeta k = ai::readAnswerMeta("/ai/results/seed-derivative_Wolfram.md");
        expectStr(k.toolQuery, "", "meta: a _Wolfram.md carries no query to check");
        expectStr(k.toolServer, "", "meta: and no server to send one to");

        // An answer saved BEFORE this hop: parses, with no check to offer.
        const ai::AnswerMeta old = ai::parseAnswerMeta(
            "# Old answer\n\nbody\n\n%%ai: model=m hash=abc pages=1 image=-%%\n");
        check(old.found, "meta: an older answer's line still parses");
        expectStr(old.toolQuery, "", "meta: with no query offered");
        expectStr(old.hash, "abc", "meta: and its hash is read");

        // The LAST line wins: a body that quotes one cannot shadow the real one.
        const ai::AnswerMeta two = ai::parseAnswerMeta(
            "body mentioning %%ai: model=decoy%%%%\nmore\n"
            "\n%%ai: model=real hash=def pages=1 image=- tool_query=pi tool_server=wolfram%%\n");
        expectStr(two.model, "real", "meta: the last line wins");
        expectStr(two.toolQuery, "pi", "meta: and its query is the one read");

        // No line at all.
        const ai::AnswerMeta none = ai::parseAnswerMeta("# Just an answer\n");
        check(!none.found, "meta: an answer with no line parses to found == false");
    }

    std::printf("== %d checks, %d failures ==\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
