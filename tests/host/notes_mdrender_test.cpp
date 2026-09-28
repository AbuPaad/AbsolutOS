// notes_mdrender_test.cpp — host regression guard for the shared Markdown
// render module (src/mdrender/MdRenderer.{h,cpp}).
//
// Feeds .md fixtures through pre-strip -> md4c -> layout -> paginate and
// asserts PAGE BREAKS AND LINE TEXT AS TEXT — the highest-value assertion,
// because wrapping and pagination are the fiddly part (parser.md §7,
// renderer.md §6). It also prints the paging result so a human can eyeball it.
//
// Standalone host test: no LVGL, no PlatformIO. The core compiles with any
// C++17 compiler and the metrics are the deterministic HostMetrics stub, so
// wrapping/paging are reproducible on any host.
//
// Build (md4c.c is C — compile it as C, do NOT hand it to g++):
//   gcc -std=gnu11 -c lib/md4c/md4c.c -o /tmp/md4c.o
//   g++ -std=gnu++17 -I src -I lib/md4c -I tests/host \
//       tests/host/notes_mdrender_test.cpp src/mdrender/MdRenderer.cpp \
//       /tmp/md4c.o -o /tmp/mdrender_test
//   /tmp/mdrender_test            # exit 0 = pass, 1 = a check regressed
//
// Precedent: tests/host/keycode_digit_test.cpp (same g_failures/g_checks
// shape, same "not part of any PlatformIO build env" contract).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mdrender/MdRenderer.h"

using namespace mdrender;

static int g_failures = 0;
static int g_checks   = 0;

static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static void expectEq(int got, int want, const char* what) {
    ++g_checks;
    if (got != want) {
        std::printf("FAIL: %-44s got %d, want %d\n", what, got, want);
        ++g_failures;
    }
}

// ── Helpers ─────────────────────────────────────────────────────────────────
static std::string pageText(const MdRenderer& r, int page) {
    std::string out;
    const auto& lines = r.display().lines;
    const Page& pg = r.pages()[static_cast<size_t>(page)];
    for (uint32_t i = 0; i < pg.lineCount; ++i) {
        const uint32_t li = pg.firstLine + i;
        if (li >= lines.size()) break;
        out += r.lineText(lines[li]);
        out += "\n";
    }
    return out;
}

static std::string allText(const MdRenderer& r) {
    std::string out;
    for (int p = 0; p < r.pageCount(); ++p) out += pageText(r, p);
    return out;
}

static bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

static std::string poolSlice(const MdDocument& doc, uint32_t off, uint32_t len) {
    if (off > doc.pool.size()) return {};
    const size_t n = (off + len <= doc.pool.size()) ? len : (doc.pool.size() - off);
    return std::string(reinterpret_cast<const char*>(doc.pool.data() + off), n);
}

static std::string linkTarget(const MdRenderer& r, uint32_t target) {
    if (target == 0) return {};
    const auto& lt = r.doc().linkTargets;
    if (target - 1 >= lt.size()) return {};
    return poolSlice(r.doc(), lt[target - 1].first, lt[target - 1].second);
}

static bool hasRun(const MdRenderer& r, StyleId style, const char* textExact) {
    for (const Run& run : r.display().runs) {
        if (run.style == style && r.runText(run) == textExact) return true;
    }
    return false;
}

static bool hasStyle(const MdRenderer& r, StyleId style) {
    for (const Run& run : r.display().runs) {
        if (run.style == style) return true;
    }
    return false;
}

static void printPages(const MdRenderer& r, const char* name) {
    std::printf("  [%s] %d page(s)\n", name, r.pageCount());
    for (int p = 0; p < r.pageCount(); ++p) {
        std::printf("    page %d:\n", p + 1);
        const auto& lines = r.display().lines;
        const Page& pg = r.pages()[static_cast<size_t>(p)];
        for (uint32_t i = 0; i < pg.lineCount; ++i) {
            const uint32_t li = pg.firstLine + i;
            if (li >= lines.size()) break;
            std::printf("      | %s\n", r.lineText(lines[li]).c_str());
        }
    }
}

// Parse + layout + paginate with the deterministic host metrics.
static void build(MdRenderer& r, const char* src) {
    r.parse(reinterpret_cast<const uint8_t*>(src), std::strlen(src));
    MdStyles styles;
    styles.bodySize = 14;
    styles.headingSize = 14;
    styles.codeSize = 12;
    HostMetrics metrics;
    r.layout(metrics, styles);
    r.paginate(styles);
}

static void everyPageFits(const MdRenderer& r, const char* what) {
    for (const Page& p : r.pages()) {
        if (p.height > CONTENT_H) {
            std::printf("FAIL: page height %u > CONTENT_H (%d) [%s]\n",
                        static_cast<unsigned>(p.height), CONTENT_H, what);
            ++g_failures;
        }
    }
    ++g_checks;
}

// ═══════════════════════════════════════════════════════════════════════════
int main() {
    std::printf("== notes_mdrender_test ==\n");

    // ── 1. Frontmatter is stripped, leading --- is NOT a page break ─────────
    {
        MdRenderer r;
        build(r, "---\ntitle: Hello\ntags: a, b\n---\n# Head\n\nBody text.\n");
        expectEq(r.pageCount(), 1, "frontmatter: page count");
        const std::string t = allText(r);
        check(has(t, "Head"), "frontmatter: body heading kept");
        check(has(t, "Body text."), "frontmatter: body paragraph kept");
        check(!has(t, "title:"), "frontmatter: title key not rendered");
        check(!has(t, "tags:"), "frontmatter: tags key not rendered");
        check(r.doc().frontmatter.present, "frontmatter: present flag");
        check(r.doc().frontmatter.title == "Hello", "frontmatter: title value");
        printPages(r, "frontmatter");
    }

    // ── 2. A thematic break is a HARD page break and is not drawn ───────────
    {
        MdRenderer r;
        build(r, "# A\n\n---\n\n# B\n");
        expectEq(r.pageCount(), 2, "hard break: page count");
        check(has(pageText(r, 0), "A"), "hard break: A on page 1");
        check(has(pageText(r, 1), "B"), "hard break: B on page 2");
        check(!has(allText(r), "---"), "hard break: not drawn as a rule");
        everyPageFits(r, "hard break");
        printPages(r, "hard-break");
    }

    // ── 3. *** and ___ are also hard breaks ─────────────────────────────────
    {
        MdRenderer r;
        build(r, "A\n\n***\n\nB\n\n___\n\nC\n");
        expectEq(r.pageCount(), 3, "hr variants: page count");
        check(has(pageText(r, 0), "A") && has(pageText(r, 1), "B") &&
              has(pageText(r, 2), "C"), "hr variants: each section on its page");
        everyPageFits(r, "hr variants");
        printPages(r, "hr-variants");
    }

    // ── 4. Nested lists keep DOCUMENT order (parent before child) ───────────
    {
        MdRenderer r;
        build(r, "- parent\n  - child\n    - grand\n");
        const std::string t = allText(r);
        check(has(t, "parent") && has(t, "child") && has(t, "grand"),
              "nested list: all levels present");
        const size_t pp = t.find("parent");
        const size_t cc = t.find("child");
        const size_t gg = t.find("grand");
        check(pp != std::string::npos && cc != std::string::npos && gg != std::string::npos &&
              pp < cc && cc < gg, "nested list: parent < child < grand order");
        printPages(r, "nested-list");
    }

    // ── 5. Loose list items keep their ordered/bullet marker ────────────────
    {
        MdRenderer r;
        build(r, "1. one\n\n2. two\n");
        const std::string t = allText(r);
        check(has(t, "one") && has(t, "two"), "ordered list: items present");
        check(has(t, "1."), "ordered list: ordered marker");
        printPages(r, "ordered-list");
    }

    // ── 6. Fenced code is preserved verbatim and styled Code ────────────────
    {
        MdRenderer r;
        build(r, "```\nline1 = 1;\nline2 = 2;\n```\n");
        const std::string t = allText(r);
        check(has(t, "line1 = 1;") && has(t, "line2 = 2;"), "code: lines preserved");
        check(hasStyle(r, StyleId::Code), "code: Code style emitted");
        printPages(r, "code");
    }

    // ── 7. Obsidian callout: marker stripped, body kept, Callout style ──────
    {
        MdRenderer r;
        build(r, "> [!note] A title\n> callout body\n");
        const std::string t = allText(r);
        check(has(t, "A title"), "callout: title kept");
        check(has(t, "callout body"), "callout: body kept");
        check(!has(t, "[!note]"), "callout: marker stripped");
        check(hasStyle(r, StyleId::Callout), "callout: Callout style");
        printPages(r, "callout");
    }

    // ── 8. Plain quote ──────────────────────────────────────────────────────
    {
        MdRenderer r;
        build(r, "> just a quote\n");
        check(has(allText(r), "just a quote"), "quote: text present");
        check(hasStyle(r, StyleId::Quote), "quote: Quote style");
        printPages(r, "quote");
    }

    // ── 9. Wikilinks: display text + resolvable target ──────────────────────
    {
        MdRenderer r;
        build(r, "See [[Note|Display]] now.\n");
        check(hasRun(r, StyleId::Wikilink, "Display"), "wikilink: display run");
        uint32_t target = 0;
        for (const Run& run : r.display().runs) {
            if (run.style == StyleId::Wikilink) target = run.target;
        }
        check(linkTarget(r, target) == "Note", "wikilink: target resolves to Note");
        printPages(r, "wikilink");
    }

    // ── 10. Tables: stacked key:value fallback, all cell text present ───────
    {
        MdRenderer r;
        build(r, "| name | qty |\n|------|-----|\n| bolt | 12  |\n| nut  | 3   |\n");
        const std::string t = allText(r);
        check(has(t, "name") && has(t, "qty"), "table: header keys present");
        check(has(t, "bolt") && has(t, "12"), "table: row 1 present");
        check(has(t, "nut") && has(t, "3"), "table: row 2 present");
        everyPageFits(r, "table");
        printPages(r, "table");
    }

    // ── 11. Task lists ──────────────────────────────────────────────────────
    {
        MdRenderer r;
        build(r, "- [x] done\n- [ ] todo\n");
        const std::string t = allText(r);
        check(has(t, "done") && has(t, "todo"), "tasks: item text present");
        check(has(t, "[x]"), "tasks: checked marker");
        printPages(r, "tasks");
    }

    // ── 12. Math spans get the Math style ───────────────────────────────────
    {
        MdRenderer r;
        build(r, "Inline $x^2$ here.\n");
        check(hasRun(r, StyleId::Math, "x^2"), "math: Math run for $x^2$");
        printPages(r, "math");
    }

    // ── 13. Unicode survives pre-strip -> md4c -> layout ────────────────────
    {
        MdRenderer r;
        build(r, "# Caf\xC3\xA9 \xE2\x98\x95\n\nNa\xC3\xAFve \xE2\x80\x94 text\n");
        const std::string t = allText(r);
        check(has(t, "Caf\xC3\xA9"), "unicode: heading text");
        check(has(t, "Na\xC3\xAFve"), "unicode: body text");
        printPages(r, "unicode");
    }

    // ── 14. %%comments%%, embeds and ^block-ids are pre-stripped ────────────
    {
        MdRenderer r;
        build(r, "a %%hidden%% b\n\n![[Pic]]\n\ntext ^abc\n");
        const std::string t = allText(r);
        check(has(t, "a") && has(t, "b"), "pre-strip: comment removed, text kept");
        check(!has(t, "hidden"), "pre-strip: comment content gone");
        check(has(t, "Pic"), "pre-strip: embed becomes a link stub");
        check(!has(t, "^abc"), "pre-strip: block id removed");
        printPages(r, "pre-strip");
    }

    // ── 15. Raw HTML is disabled: no crash, text survives ───────────────────
    {
        MdRenderer r;
        build(r, "<b>bold</b> and <script>alert(1)</script>\n");
        const std::string t = allText(r);
        check(has(t, "bold"), "no-html: text content survives");
        printPages(r, "no-html");
    }

    // ── 16. Several --- breaks: pages = breaks + 1 ──────────────────────────
    {
        MdRenderer r;
        build(r, "one\n\n---\n\ntwo\n\n---\n\nthree\n\n---\n\nfour\n");
        expectEq(r.pageCount(), 4, "multi-break: page count");
        everyPageFits(r, "multi-break");
        printPages(r, "multi-break");
    }

    // ── 17. Empty document yields exactly one (empty) page ──────────────────
    {
        MdRenderer r;
        build(r, "");
        expectEq(r.pageCount(), 1, "empty: page count");
        everyPageFits(r, "empty");
    }

    // ── 18. Pathological: one enormous unbreakable word wraps, no overflow ──
    {
        std::string note(4000, 'x');
        note += "\n";
        MdRenderer r;
        build(r, note.c_str());
        check(r.pageCount() > 1, "long word: spans multiple pages");
        check(r.display().lines.size() > 1, "long word: wrapped to multiple lines");
        everyPageFits(r, "long word");
        std::printf("  [long-word] %d pages, %zu lines\n", r.pageCount(),
                    r.display().lines.size());
    }

    // ── 19. Pathological: oversized code block paginates cleanly ────────────
    {
        std::string note = "```\n";
        for (int i = 0; i < 3000; ++i) {
            note += "code line ";
            note += std::to_string(i);
            note += "\n";
        }
        note += "```\n";
        MdRenderer r;
        build(r, note.c_str());
        check(r.pageCount() > 5, "oversized block: many pages");
        check(!r.isTruncated(), "oversized block: under caps, not truncated");
        everyPageFits(r, "oversized block");
        std::printf("  [oversized] %d pages, %zu lines\n", r.pageCount(),
                    r.display().lines.size());
    }

    // ── 20. Overflow reflow: no page ever exceeds CONTENT_H across fixtures ─
    const struct { const char* name; const char* src; } fixtures[] = {
        {"fm", "---\ntitle: T\n---\n# H\n\npara\n"},
        {"breaks", "a\n\n---\n\nb\n\n---\n\nc\n"},
        {"mixed",
         "# Title\n\nSome **bold** and *ital* and `code` and "
         "==mark== and ^sup^ and ~sub~.\n\n"
         "- [x] done\n- [ ] todo\n\n> quote\n\n> [!tip] Tip\n> body\n\n"
         "A [[Wiki|link]] and [ext](http://x).\n\n$e=mc^2$\n\n"
         "| a | b |\n|---|---|\n| 1 | 2 |\n"},
    };
    for (const auto& f : fixtures) {
        MdRenderer r;
        build(r, f.src);
        everyPageFits(r, f.name);
        printPages(r, f.name);
    }

    if (g_failures == 0) {
        std::printf("PASS: notes_mdrender_test (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("FAILED: %d of %d check(s) in notes_mdrender_test\n", g_failures, g_checks);
    return 1;
}
