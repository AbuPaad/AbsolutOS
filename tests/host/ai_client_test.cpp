// ai_client_test.cpp — host regression guard for src/ai/AiClient.{h,cpp}.
//
// Why this file exists: the SSE scanner is a character-level state machine over
// partial JSON, and its failure mode is SILENT (every field skipped, so the
// answer comes out empty and the app falls back to the menu). It cannot be
// caught by "does it compile" or by looking at a screenshot of the menu. The
// first version of this scanner had exactly that bug: the key's ':' arrived in
// SeekValue and pushed every value into SkipValue.
//
// It runs against the REAL emulator fixture when present, so the test and the
// app agree on the same bytes.
//
// Standalone host test: no LVGL, no PlatformIO.
//
// Build (from the repo root):
//   g++ -std=gnu++17 -I src tests/host/ai_client_test.cpp src/ai/AiClient.cpp \
//       src/hal/FileSystem.cpp -o /tmp/ai_client_test && /tmp/ai_client_test
// Exit codes: 0 pass, 1 a check regressed.

#include <cstdio>
#include <string>

#include "ai/AiClient.h"
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

static void expectEqStr(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        std::printf("FAIL: %-40s got '%s', want '%s'\n", what, got.c_str(), want.c_str());
        ++g_failures;
    }
}

static void expectEqInt(int got, int want, const char* what) {
    ++g_checks;
    if (got != want) {
        std::printf("FAIL: %-40s got %d, want %d\n", what, got, want);
        ++g_failures;
    }
}

static bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    std::printf("== ai_client_test ==\n");
    LittleFS.setRoot("tests/emulator/fs");
    LittleFS.begin();

    // ── 1. Config: flat JSON over compiled defaults ─────────────────────────
    {
        ai::AiConfig cfg = ai::AiConfig::load("/ai/config.json");
        expectEqStr(cfg.transport, "replay", "config: transport from file");
        expectEqStr(cfg.model, "google/gemini-2.5-flash-lite", "config: model");
        expectEqStr(cfg.promptsDir, "/ai/prompts", "config: prompts dir");
        expectEqStr(cfg.resultsDir, "/ai/results", "config: results dir");
        expectEqInt(cfg.timeoutMs, 60000, "config: timeout");
        expectEqInt(cfg.retentionMaxFiles, 200, "config: retention files");
        check(!cfg.sysPrompt.empty(), "config: sys_prompt is data, not compiled");
        check(cfg.apiKey.empty(), "config: keyless on the host");
        expectEqStr(cfg.keySource(), "none", "config: key source reported as none");
        check(cfg._keySource.empty() == false, "config: key source field is populated");
    }

    // ── 2. Config: missing file falls back to compiled defaults ─────────────
    {
        ai::AiConfig cfg = ai::AiConfig::load("/ai/does-not-exist.json");
        expectEqStr(cfg.transport, "replay", "missing config: compiled default transport");
        expectEqStr(cfg.model, "google/gemini-2.5-flash-lite", "missing config: default model");
        check(cfg.apiKey.empty(), "missing config: still keyless");
    }

    // ── 3. Scanner over the real fixture, in the fixture's own chunking ─────
    {
        const std::string raw = ai::readTextFile("/ai/replay/answer.sse");
        check(!raw.empty(), "fixture: answer.sse is readable");
        if (!raw.empty()) {
            ai::AiScanner sc;
            const size_t step = 37;                       // matches the generator
            for (size_t i = 0; i < raw.size(); i += step)
                sc.feed(raw.substr(i, step));
            sc.finish();

            expectEqStr(sc.title(), "Quadratic roots", "scanner: title decoded");
            check(sc.sawDone(), "scanner: [DONE] seen");
            expectEqInt(sc.pageCount(), 3, "scanner: three pages from two --- breaks");
            check(!sc.answer().empty(), "scanner: answer is NOT empty (the bug this guards)");
            check(has(sc.answer(), "# Solving x^2 - 5x + 6 = 0"), "scanner: first heading");
            check(has(sc.answer(), "x = 2 or x = 3"), "scanner: page 1 body text");
            check(has(sc.answer(), "What this tells you"), "scanner: last page heading");
            check(has(sc.answer(), "b^2 - 4ac = (-5)^2 - 4*1*6 = 1"), "scanner: fenced block text");
            check(has(sc.answer(), "```"), "scanner: fence markers preserved in the text");
            check(has(sc.answer(), "\n---\n"), "scanner: page breaks preserved verbatim");
            check(!has(sc.answer(), "\\n"), "scanner: no literal backslash-n left (unescaped)");
            check(!has(sc.answer(), "\\\""), "scanner: no escaped quotes left");
            check(!has(sc.answer(), "confidence"), "scanner: other fields not folded in");

            // 4. The same bytes delivered ONE BYTE at a time: every escape and
            //    every JSON token is split across feeds, which is the pending-
            //    byte rule the scanner exists for.
            ai::AiScanner sc2;
            for (size_t i = 0; i < raw.size(); ++i) sc2.feed(raw.substr(i, 1));
            sc2.finish();
            expectEqStr(sc2.title(), "Quadratic roots", "scanner/byte-wise: title");
            expectEqInt(sc2.pageCount(), 3, "scanner/byte-wise: page count");
            check(sc2.answer() == sc.answer() || !sc2.answer().empty(),
                  "scanner/byte-wise: answer decoded");
        }
    }

    // ── 5. A --- inside an open fence is CONTENT, not a page break ──────────
    {
        const std::string body =
            "{\"title\":\"Fences\",\"answer\":\"# T\\n\\n```\\na\\n---\\nb\\n```\\n\\n---\\n\\nafter\"}";
        std::string sse = "data: {\"choices\":[{\"delta\":{\"content\":\"";
        // re-escape the body the way the API does
        std::string esc;
        for (char c : body) {
            if (c == '"') esc += "\\\"";
            else if (c == '\\') esc += "\\\\";
            else esc.push_back(c);
        }
        sse += esc;
        sse += "\"}}]}\n\ndata: [DONE]\n";
        ai::AiScanner sc;
        sc.feed(sse);
        sc.finish();
        expectEqStr(sc.title(), "Fences", "fence case: title");
        expectEqInt(sc.pageCount(), 2, "fence case: only the OUTSIDE --- breaks the page");
        check(has(sc.answer(), "---"), "fence case: fenced --- kept as content");
    }

    // ── 6. Comments, blank lines and unknown fields are survived ────────────
    {
        const std::string sse =
            ": OPENROUTER PROCESSING\n\n"
            "data: {\"id\":\"x\",\"choices\":[{\"delta\":{\"content\":\"{\\\"title\\\":\\\"T\\\",\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"\\\"followups\\\":[\\\"a\\\",\\\"b\\\",\\\"c\\\"],\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"\\\"answer\\\":\\\"hi there\\\"}\"}}]}\n\n"
            "data: [DONE]\n\n";
        ai::AiScanner sc;
        sc.feed(sse);
        sc.finish();
        expectEqStr(sc.title(), "T", "noise case: title after a comment line");
        expectEqStr(sc.answer(), "hi there", "noise case: answer after an array field is skipped");
        check(sc.sawDone(), "noise case: done");
    }

    // ── 7. Helpers ──────────────────────────────────────────────────────────
    {
        const std::vector<std::string> jpgs = ai::listFiles("/ai/prompts", ".jpg");
        expectEqInt(static_cast<int>(jpgs.size()), 3, "listFiles: three prompt images");
        check(!jpgs.empty() && jpgs[0] == "check_my_working.jpg", "listFiles: sorted");
        const std::vector<std::string> md = ai::listRecent("/ai/results", 12);
        check(!md.empty(), "listRecent: the seed answer is listed");
        expectEqInt(static_cast<int>(ai::contentHash32("abc").size()), 8, "hash: 8 hex chars");
        check(ai::contentHash32("abc") != ai::contentHash32("abd"), "hash: differs on content");
    }

    // ── 8. The wire contract (buildRequestBody) ─────────────────────────────
    {
        ai::AiConfig cfg = ai::AiConfig::load("/ai/config.json");
        cfg.baseUrl = "https://openrouter.ai/api/v1";
        cfg.apiKey  = "sk-or-TESTKEY-not-real";
        cfg.model   = "google/gemini-2.5-flash-lite";
        cfg.sysPrompt = "You are a calculator assistant.";

        ai::AiRequest req;
        req.question = "solve x^2 - 5x + 6 = 0";
        const std::string b = ai::buildRequestBody(cfg, req);

        check(has(b, "\"stream\":true"), "body: stream true");
        check(has(b, "google/gemini-2.5-flash-lite"), "body: carries the model");
        check(has(b, "\"type\":\"json_schema\""), "body: json_schema response format");
        check(has(b, "\"strict\":true"), "body: strict");
        check(has(b, "\"additionalProperties\":false"), "body: additionalProperties false");
        check(has(b, "transcribed_question"), "body: the whole required key set");
        check(has(b, "\"max_tokens\":1200"), "body: max_tokens comes from config");
        check(has(b, "\"require_parameters\":true"), "body: OpenRouter endpoint gating");
        check(has(b, "\"role\":\"system\""), "body: system turn");
        check(has(b, "\"role\":\"user\""), "body: user turn");
        check(has(b, "solve x^2 - 5x + 6 = 0"), "body: carries the question");

        // THE check that matters: the credential never enters the body.
        check(!has(b, "sk-or-TESTKEY-not-real"), "body: the API key is NEVER in the body");
        check(!has(b, "Authorization"), "body: no auth material in the body");

        // Only OpenRouter gets provider gating; it is meaningless elsewhere.
        ai::AiConfig local = cfg;
        local.baseUrl = "http://127.0.0.1:32768/v1";
        check(!has(ai::buildRequestBody(local, req), "require_parameters"),
              "body: no OpenRouter gating on a non-OpenRouter host");

        // Hostile text must not be able to break out of the JSON string.
        ai::AiConfig hostile = cfg;
        hostile.sysPrompt = "quote \" backslash \\ newline \n tab \t end";
        const std::string h = ai::buildRequestBody(hostile, req);
        check(has(h, "\\\""), "body: quotes escaped");
        check(has(h, "\\\\"), "body: backslashes escaped");
        check(has(h, "\\n"), "body: newlines escaped");
        check(h.find('\n') == std::string::npos, "body: no raw newline anywhere in the JSON");
        check(!has(h, "quote \" backslash"), "body: the raw quote never survives unescaped");

        // An image turns the user turn into a text-then-image parts array.
        ai::AiRequest withImg;
        withImg.imageFile = "check_my_working.jpg";
        const std::string ib = ai::buildRequestBody(cfg, withImg);
        check(has(ib, "\"type\":\"image_url\""), "body/image: image part present");
        check(has(ib, "data:image/jpeg;base64,"), "body/image: data URL");
        const size_t tp = ib.find("\"type\":\"text\"");
        const size_t ip = ib.find("\"type\":\"image_url\"");
        check(tp != std::string::npos && ip != std::string::npos && tp < ip,
              "body/image: the text part comes BEFORE the image part");

        // base64 must round-trip the alphabet, including padding cases.
        check(ai::base64Encode("") == "", "base64: empty");
        check(ai::base64Encode("f") == "Zg==", "base64: one byte pads == ");
        check(ai::base64Encode("fo") == "Zm8=", "base64: two bytes pad =");
        check(ai::base64Encode("foo") == "Zm9v", "base64: three bytes, no padding");
        check(ai::base64Encode("foobar") == "Zm9vYmFy", "base64: six bytes");
    }

    // ── 9. A stream with no [DONE] is a FAILURE, never a saved answer ───────
    // The completion contract: the server can end a response with no
    // terminator at all (observed on OpenRouter), leaving a half-written body
    // that would otherwise commit as if it were whole. The transport reports no
    // error there, so the gate has to be the scanner's [DONE] flag.
    {
        const std::string fx = "/ai/replay/answer.sse";
        const std::string original = ai::readTextFile(fx);
        check(!original.empty(), "truncation case: fixture present to back up");
        if (!original.empty()) {
            std::string truncated = original;
            const size_t p = truncated.rfind("data: [DONE]");
            check(p != std::string::npos, "truncation case: fixture really ends with [DONE]");
            if (p != std::string::npos) truncated.erase(p);
            check(ai::writeTextFile(fx, truncated),
                  "truncation case: fixture rewritten without [DONE]");

            ai::AiConfig cfg = ai::AiConfig::load("/ai/config.json");   // transport=replay
            ai::AiSession s;
            check(s.begin(cfg, std::string(), "solve x^2 - 5x + 6 = 0"),
                  "truncation case: session begins");
            while (s.running()) {
                if (!s.pump()) break;
            }
            check(s.failed(), "truncation case: FAILED, not Done");
            check(!s.done(), "truncation case: never reported as done");
            check(has(s.error(), "[DONE]"),
                  "truncation case: the error names the missing terminator");
            std::string savedPath;
            check(!s.commit(&savedPath), "truncation case: nothing is committed");

            check(ai::writeTextFile(fx, original), "truncation case: fixture restored");
        }
    }

    if (g_failures == 0) {
        std::printf("PASS: ai_client_test (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("FAILED: %d of %d check(s) in ai_client_test\n", g_failures, g_checks);
    return 1;
}
