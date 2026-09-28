/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * MdRenderer.cpp — the shared Markdown render module.
 *
 *   pre-strip -> md4c -> layout -> paginate -> LVGL draw (one page only)
 *
 * The core (pre-strip/parse/layout/paginate) is LVGL-free and compiles with a
 * plain g++ host compiler; the draw stage is compiled only when <lvgl.h> is on
 * the include path. See MdRenderer.h for the contract and the named geometry.
 *
 * Design notes that the code relies on:
 *  · One text pool (`MdDocument::pool`); every span/run is (off,len) into it.
 *  · A thematic break is a HARD page break and is never drawn as a rule.
 *  · Every emitted page fits CONTENT_H; overflow reflow guarantees it. There
 *    is no intra-page scrolling.
 *  · Raw HTML is disabled and no URL is ever fetched.
 */

#include "mdrender/MdRenderer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <LittleFS.h>
#elif defined(NATIVE_SIM)
  #include "hal/FileSystem.h"
#endif

#include "md4c.h"

#if defined(ARDUINO) || defined(NATIVE_SIM)
  #include <lvgl.h>
  #define MDR_HAVE_LVGL 1
#else
  #define MDR_HAVE_LVGL 0
#endif

namespace mdrender {

// ═══════════════════════════════════════════════════════════════════════════
// Limits (parser.md §5) — overflow truncates with a visible block.
// ═══════════════════════════════════════════════════════════════════════════
static constexpr size_t   kMaxNoteBytes   = 512u * 1024u;
static constexpr uint32_t kMaxBlocks      = 8192u;
static constexpr uint32_t kMaxSpans       = 131072u;
static constexpr uint32_t kMaxLinkTargets = 1024u;
static constexpr uint32_t kMaxLinkLen     = 512u;

// ═══════════════════════════════════════════════════════════════════════════
// HostMetrics — deterministic, LVGL-free stub (parser.md §7)
// ═══════════════════════════════════════════════════════════════════════════
int HostMetrics::textWidth(const char* text, uint32_t len, const void* /*font*/, int size) const {
    // One fixed advance per UTF-8 codepoint: 0.6em regular, 0.3em space.
    int w = 0;
    uint32_t i = 0;
    while (i < len) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        int clen = 1;
        if ((c & 0x80) == 0)       clen = 1;
        else if ((c & 0xE0) == 0xC0) clen = 2;
        else if ((c & 0xF0) == 0xE0) clen = 3;
        else if ((c & 0xF8) == 0xF0) clen = 4;
        if (i + static_cast<uint32_t>(clen) > len) clen = 1;
        w += (c == ' ') ? (size * 3 / 10) : (size * 6 / 10);
        i += static_cast<uint32_t>(clen);
    }
    return w;
}

int HostMetrics::lineHeight(const void* /*font*/, int size) const {
    return size * 4 / 3;
}

int HostMetrics::glyphWidth(const void* /*font*/, int size, uint32_t codepoint) const {
    return (codepoint == ' ') ? (size * 3 / 10) : (size * 6 / 10);
}

/** Decode one UTF-8 codepoint; returns its byte length (>= 1). */
static uint32_t decodeUtf8(const char* s, uint32_t n, uint32_t& cp) {
    const unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) { cp = c; return 1; }
    uint32_t extra = 0, v = 0;
    if ((c & 0xE0) == 0xC0) { v = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { v = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { v = c & 0x07; extra = 3; }
    else { cp = 0xFFFD; return 1; }
    if (1 + extra > n) { cp = 0xFFFD; return 1; }
    for (uint32_t k = 0; k < extra; ++k)
        v = (v << 6) | (static_cast<unsigned char>(s[1 + k]) & 0x3F);
    cp = v;
    return 1 + extra;
}

#if MDR_HAVE_LVGL
// Compiled-in Montserrat sizes only. 16 is NOT compiled in lv_conf.h; never
// reference it. Fall back to the nearest available size.
static const lv_font_t* defaultFontForSize(int size) {
    switch (size) {
        case 10: return &lv_font_montserrat_10;
        case 12: return &lv_font_montserrat_12;
        case 20: return &lv_font_montserrat_20;
        case 14:
        default: return &lv_font_montserrat_14;
    }
}

static const lv_font_t* asFont(const void* font, int size) {
    return font ? static_cast<const lv_font_t*>(font) : defaultFontForSize(size);
}

int LvglMetrics::textWidth(const char* text, uint32_t len, const void* font, int size) const {
    if (len == 0) return 0;
    const lv_font_t* f = asFont(font, size);
    // lv_txt_get_size() needs a NUL-terminated string; copy the slice.
    std::string tmp(text, len);
    lv_point_t p{0, 0};
    lv_txt_get_size(&p, tmp.c_str(), f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

int LvglMetrics::lineHeight(const void* font, int size) const {
    return lv_font_get_line_height(asFont(font, size));
}

int LvglMetrics::glyphWidth(const void* font, int size, uint32_t codepoint) const {
    return lv_font_get_glyph_width(asFont(font, size), codepoint, 0);
}
#endif  // MDR_HAVE_LVGL

// ═══════════════════════════════════════════════════════════════════════════
// FileSource — the only place a file path is allowed to appear
// ═══════════════════════════════════════════════════════════════════════════
struct FileSource::Impl {
#if defined(ARDUINO) || defined(NATIVE_SIM)
    File   file;
#else
    // Plain host build (tests/host): no framework, no emulated-FS shim. The
    // module's own host test does not open files, but FileSource must still
    // link so the module compiles with a bare g++ command.
    std::FILE* fp = nullptr;
#endif
    size_t size = 0;
};

FileSource::FileSource(const char* path) : _impl(new Impl()) {
#if defined(ARDUINO) || defined(NATIVE_SIM)
    _impl->file = LittleFS.open(path, "r");
    if (_impl->file) _impl->size = _impl->file.size();
#else
    _impl->fp = std::fopen(path, "rb");
    if (_impl->fp) {
        std::fseek(_impl->fp, 0, SEEK_END);
        const long s = std::ftell(_impl->fp);
        std::fseek(_impl->fp, 0, SEEK_SET);
        _impl->size = (s > 0) ? static_cast<size_t>(s) : 0;
    }
#endif
}

FileSource::~FileSource() {
    if (!_impl) return;
#if !defined(ARDUINO) && !defined(NATIVE_SIM)
    if (_impl->fp) std::fclose(_impl->fp);
#endif
    delete _impl;
}

size_t FileSource::read(uint8_t* buf, size_t len) {
    if (!_impl) return 0;
#if defined(ARDUINO) || defined(NATIVE_SIM)
    if (!_impl->file) return 0;
    return _impl->file.read(buf, len);
#else
    if (!_impl->fp) return 0;
    return std::fread(buf, 1, len, _impl->fp);
#endif
}

size_t FileSource::size() const { return _impl ? _impl->size : 0; }

bool readAll(NoteSource& src, std::vector<uint8_t>& out) {
    out.clear();
    const size_t total = src.size();
    if (total > 0 && total <= kMaxNoteBytes) out.reserve(total);
    uint8_t buf[512];
    size_t n;
    while ((n = src.read(buf, sizeof(buf))) > 0) out.insert(out.end(), buf, buf + n);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Style resolution
// ═══════════════════════════════════════════════════════════════════════════
static const void* styleFont(const MdStyles& s, StyleId id) {
    switch (id) {
        case StyleId::Heading1:
        case StyleId::Heading2:
        case StyleId::Heading3: return s.headingFont ? s.headingFont : s.bodyFont;
        case StyleId::Code:     return s.codeFont ? s.codeFont : s.bodyFont;
        case StyleId::Math:
        case StyleId::MathBlock: return s.mathFont ? s.mathFont : s.bodyFont;
        default:                return s.bodyFont;
    }
}

static int styleSize(const MdStyles& s, StyleId id) {
    switch (id) {
        case StyleId::Heading1:
        case StyleId::Heading2:
        case StyleId::Heading3: return s.headingSize > 0 ? s.headingSize : s.bodySize;
        case StyleId::Code:     return s.codeSize > 0 ? s.codeSize : s.bodySize;
        case StyleId::Math:
        case StyleId::MathBlock: return s.bodySize;
        default:                return s.bodySize;
    }
}

static StyleId spanTypeToStyle(SpanType t, StyleId base) {
    switch (t) {
        case SpanType::Text:      return base;
        case SpanType::Code:      return StyleId::Code;
        case SpanType::Emph:      return StyleId::Emph;
        case SpanType::Strong:    return StyleId::Strong;
        case SpanType::Strike:    return StyleId::Strike;
        case SpanType::Highlight: return StyleId::Highlight;
        case SpanType::Math:      return StyleId::Math;
        case SpanType::Link:      return StyleId::Link;
        case SpanType::Wikilink:  return StyleId::Wikilink;
        case SpanType::Tag:       return StyleId::Tag;
        case SpanType::Super:     return StyleId::Super;
        case SpanType::Sub:       return StyleId::Sub;
    }
    return base;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 1 — pre-strip (parser.md §4)
//   frontmatter -> %%comments%% -> ![[embed]] -> ^block-ids
// Copies into a clean buffer so (off,len) always refer to the clean bytes.
// ═══════════════════════════════════════════════════════════════════════════
static size_t lineEndOffset(const uint8_t* src, size_t len, size_t p) {
    while (p < len && src[p] != '\n' && src[p] != '\r') ++p;
    if (p < len) {
        if (src[p] == '\r' && p + 1 < len && src[p + 1] == '\n') p += 2;
        else ++p;
    }
    return p;
}

// A line that is exactly "---" (allowing a trailing \r).
static bool isFrontmatterDelimiter(const uint8_t* src, size_t len, size_t lineStart) {
    size_t p = lineStart;
    if (p + 3 > len) return false;
    if (src[p] != '-' || src[p + 1] != '-' || src[p + 2] != '-') return false;
    p += 3;
    if (p < len && src[p] == '\r') ++p;
    return p >= len || src[p] == '\n';
}

static void parseFrontmatterLine(const std::string& line, Frontmatter& fm) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return;
    std::string key = line.substr(0, colon);
    std::string val = line.substr(colon + 1);
    auto trim = [](std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        s = (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };
    trim(key);
    trim(val);
    if (key == "title") {
        fm.title = val;
    } else if (key == "tags" || key == "aliases") {
        std::vector<std::string>& dst = (key == "tags") ? fm.tags : fm.aliases;
        std::string cur;
        for (char c : val) {
            if (c == ',' || c == ' ') {
                if (!cur.empty()) { dst.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) dst.push_back(cur);
    } else if (key == "order") {
        try { fm.order = std::stoi(val); } catch (...) { fm.order = 0; }
    } else if (key == "calc") {
        fm.calc = (val != "false" && val != "0");
    }
}

// Strips leading frontmatter into `clean`. Returns true if frontmatter present.
static bool stripFrontmatter(const uint8_t* src, size_t len, Frontmatter& fm,
                             std::vector<uint8_t>& clean) {
    if (len < 3 || src[0] != '-' || src[1] != '-' || src[2] != '-') return false;
    if (!isFrontmatterDelimiter(src, len, 0)) return false;
    const size_t firstLineEnd = lineEndOffset(src, len, 0);

    size_t p = firstLineEnd;
    size_t bodyStart = 0;
    bool closed = false;
    while (p < len) {
        const size_t le = lineEndOffset(src, len, p);
        if (isFrontmatterDelimiter(src, len, p)) { bodyStart = le; closed = true; break; }
        p = le;
    }
    if (!closed) return false;  // malformed: whole file is body (parser.md §3)

    fm.present = true;
    size_t p2 = firstLineEnd;
    while (p2 < bodyStart) {
        const size_t le = lineEndOffset(src, len, p2);
        std::string line(reinterpret_cast<const char*>(src + p2), le - p2);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        parseFrontmatterLine(line, fm);
        p2 = le;
    }
    clean.assign(src + bodyStart, src + len);
    return true;
}

// Removes %%...%% comments (multi-line). Unclosed %% drops to EOF.
static void stripComments(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    size_t i = 0;
    while (i < len) {
        if (i + 1 < len && src[i] == '%' && src[i + 1] == '%') {
            size_t j = i + 2;
            bool closed = false;
            while (j + 1 < len) {
                if (src[j] == '%' && src[j + 1] == '%') { closed = true; break; }
                ++j;
            }
            i = closed ? j + 2 : len;
        } else {
            out.push_back(src[i]);
            ++i;
        }
    }
}

// Rewrites "![[X]]" to "[[X]]" (embed -> link stub; parser.md §4).
static void stripEmbeds(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    for (size_t i = 0; i < len; ++i) {
        if (src[i] == '!' && i + 2 < len && src[i + 1] == '[' && src[i + 2] == '[') {
            continue;   // drop the '!'
        }
        out.push_back(src[i]);
    }
}

// Removes trailing ^block-id tokens (parser.md §4).
static void stripBlockIds(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    size_t i = 0;
    while (i < len) {
        const size_t le = lineEndOffset(src, len, i);
        size_t contentEnd = le;
        if (contentEnd > i && src[contentEnd - 1] == '\n') --contentEnd;
        if (contentEnd > i && src[contentEnd - 1] == '\r') --contentEnd;
        size_t q = contentEnd;
        while (q > i && (std::isalnum(static_cast<unsigned char>(src[q - 1])) ||
                         src[q - 1] == '_' || src[q - 1] == '-')) {
            --q;
        }
        bool removed = false;
        if (q > i && q < contentEnd && src[q - 1] == '^' && (q == i + 1 || src[q - 2] == ' ')) {
            size_t removeStart = q - 1;
            if (removeStart > i && src[removeStart - 1] == ' ') --removeStart;
            out.insert(out.end(), src + i, src + removeStart);
            out.insert(out.end(), src + contentEnd, src + le);
            removed = true;
        }
        if (!removed) out.insert(out.end(), src + i, src + le);
        i = le;
    }
}

static std::vector<uint8_t> preStrip(const uint8_t* src, size_t len, Frontmatter& fm) {
    std::vector<uint8_t> buf0, buf1, buf2, buf3, buf4;
    bool fmPresent = stripFrontmatter(src, len, fm, buf0);
    if (fmPresent) {
        stripComments(buf0.data(), buf0.size(), buf1);
    } else {
        stripComments(src, len, buf1);
    }
    stripEmbeds(buf1.data(), buf1.size(), buf2);
    stripBlockIds(buf2.data(), buf2.size(), buf3);
    // Normalize newlines: collapse \r\n and lone \r to \n so offsets and the
    // text pool are consistent (md4c tolerates both, but we want one form).
    buf4.reserve(buf3.size());
    for (size_t i = 0; i < buf3.size(); ++i) {
        if (buf3[i] == '\r') {
            if (i + 1 < buf3.size() && buf3[i + 1] == '\n') continue;
            buf4.push_back('\n');
        } else {
            buf4.push_back(buf3[i]);
        }
    }
    return buf4;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 2 — md4c callbacks -> block/span stream
// ═══════════════════════════════════════════════════════════════════════════
namespace {

struct Frame {
    BlockType type = BlockType::Paragraph;
    uint8_t   level = 0;
    bool      ordered = false;
    bool      checked = false;
    bool      callout = false;
    uint8_t   calloutType = 0;
    uint32_t  blockIndex = 0;   // placeholder already inserted in doc->blocks
    uint32_t  spanStart = 0;
    uint16_t  directCount = 0;
    bool      childP = false;
    uint32_t  childPStart = 0;
    uint32_t  childPEnd = 0;
    int       displayMath = 0;
};

struct QuoteFrame {
    bool    callout = false;
    uint8_t type = 0;
    bool    firstP = true;
};

struct ParseState {
    MdDocument*  doc = nullptr;
    std::vector<int> spanStack;          // MD_SPANTYPE values currently open
    std::vector<std::pair<uint32_t, bool>> targetStack; // (target index, valid)
    std::vector<Frame> stack;
    std::vector<bool> listOrdered;
    std::vector<QuoteFrame> quoteFrames;
    int  listDepth = 0;
    int  quoteDepth = 0;
    bool inCallout = false;
    uint8_t calloutType = 0;
    bool probePending = false;
    int  probeQuoteFrame = -1;
    // Table sub-state.
    bool     tableActive = false;
    uint8_t  tableCols = 0;
    uint32_t tableRowsStart = 0;
    uint32_t tableBlockSpans = 0;
    bool     inHeader = false;
    uint32_t rowCellsStart = 0;
    uint16_t rowCellCount = 0;
    bool     cellActive = false;
    uint32_t cellSpanStart = 0;
    Align    cellAlign = Align::None;
    bool     aborted = false;
};

static uint8_t mapCalloutType(const std::string& t) {
    if (t == "tip") return 1;
    if (t == "important") return 2;
    if (t == "warning") return 3;
    if (t == "caution") return 4;
    return 0;  // note
}

static SpanType mapMdSpan(int mdSpan) {
    switch (mdSpan) {
        case MD_SPAN_EM: return SpanType::Emph;
        case MD_SPAN_STRONG: return SpanType::Strong;
        case MD_SPAN_CODE: return SpanType::Code;
        case MD_SPAN_DEL: return SpanType::Strike;
        case MD_SPAN_MARK: return SpanType::Highlight;
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY: return SpanType::Math;
        case MD_SPAN_WIKILINK: return SpanType::Wikilink;
        case MD_SPAN_A: return SpanType::Link;
        case MD_SPAN_IMG: return SpanType::Link;   // stub as a link span
        case MD_SPAN_SUPERSCRIPT: return SpanType::Super;
        case MD_SPAN_SUBSCRIPT: return SpanType::Sub;
        default: return SpanType::Text;
    }
}

// target = index+1 into linkTargets so 0 stays "no target".
static uint32_t addLinkTarget(ParseState& st, const MD_ATTRIBUTE& attr, bool wiki) {
    if (attr.size == 0 || attr.size > kMaxLinkLen) return 0;
    if (st.doc->linkTargets.size() >= kMaxLinkTargets) return 0;
    MdDocument* doc = st.doc;
    const uint32_t off = static_cast<uint32_t>(doc->pool.size());
    for (MD_SIZE i = 0; i < attr.size; ++i) doc->pool.push_back(static_cast<uint8_t>(attr.text[i]));
    doc->linkTargets.push_back({off, static_cast<uint32_t>(attr.size)});
    (void)wiki;
    return static_cast<uint32_t>(doc->linkTargets.size());  // 1-based
}

static void appendPoolSpan(ParseState& st, SpanType type, const char* s, uint32_t n,
                           uint32_t target) {
    MdDocument* doc = st.doc;
    const uint32_t off = static_cast<uint32_t>(doc->pool.size());
    for (uint32_t i = 0; i < n; ++i) doc->pool.push_back(static_cast<uint8_t>(s[i]));
    Span sp;
    sp.type = type;
    sp.off = off;
    sp.len = n;
    sp.target = target;
    doc->spans.push_back(sp);
}

// Handle the "> [!note] Title" form that md4c does NOT promote to an
// admonition (its extension only recognises a bare "[!note]" line). We detect
// the marker on the first text chunk of the first paragraph in the quote.
static void probeObsidianCallout(ParseState& st, const char*& s, MD_SIZE& n) {
    st.probePending = false;
    if (n < 3 || s[0] != '[' || s[1] != '!') return;
    MD_SIZE k = 2;
    while (k < n && std::isalpha(static_cast<unsigned char>(s[k]))) ++k;
    if (k >= n || s[k] != ']') return;

    const uint8_t ty = mapCalloutType(std::string(s + 2, k - 2));
    if (st.probeQuoteFrame >= 0 && st.probeQuoteFrame < static_cast<int>(st.quoteFrames.size())) {
        st.quoteFrames[st.probeQuoteFrame].callout = true;
        st.quoteFrames[st.probeQuoteFrame].type = ty;
    }
    st.inCallout = true;
    st.calloutType = ty;
    if (!st.stack.empty()) {
        st.stack.back().type = BlockType::Callout;
        st.stack.back().callout = true;
        st.stack.back().calloutType = ty;
    }
    MD_SIZE skip = k + 1;
    if (skip < n && s[skip] == ' ') ++skip;
    s += skip;
    n -= skip;
}

static int emitText(ParseState& st, MD_TEXTTYPE ttype, const char* text, MD_SIZE size) {
    MdDocument* doc = st.doc;
    if (doc->spans.size() >= kMaxSpans) { st.aborted = true; return 1; }

    if (st.probePending) probeObsidianCallout(st, text, size);
    if (size == 0) return 0;

    SpanType stype = SpanType::Text;
    if (!st.spanStack.empty()) stype = mapMdSpan(st.spanStack.back());
    if (ttype == MD_TEXT_CODE) stype = SpanType::Code;
    if (ttype == MD_TEXT_LATEXMATH) stype = SpanType::Math;

    uint32_t target = 0;
    if (!st.targetStack.empty() && st.targetStack.back().second) target = st.targetStack.back().first;

    switch (ttype) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY:
        case MD_TEXT_CODE:
        case MD_TEXT_LATEXMATH:
            appendPoolSpan(st, stype, text, size, target);
            break;
        case MD_TEXT_SOFTBR:
            appendPoolSpan(st, SpanType::Text, " ", 1, 0);
            break;
        case MD_TEXT_BR:
            appendPoolSpan(st, SpanType::Text, "\n", 1, 0);
            break;
        case MD_TEXT_NULLCHAR:
            appendPoolSpan(st, SpanType::Text, "\xEF\xBF\xBD", 3, 0);
            break;
        default:
            break;
    }
    if (!st.cellActive && !st.stack.empty()) st.stack.back().directCount++;
    return 0;
}

static int mdEnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    ParseState& st = *static_cast<ParseState*>(userdata);
    MdDocument* doc = st.doc;
    if (doc->blocks.size() >= kMaxBlocks) { st.aborted = true; return 1; }

    Frame f;
    f.spanStart = static_cast<uint32_t>(doc->spans.size());

    switch (type) {
        case MD_BLOCK_DOC:
        case MD_BLOCK_HTML:
            return 0;

        case MD_BLOCK_H: {
            auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            f.type = BlockType::Heading;
            f.level = static_cast<uint8_t>(d->level);
            break;
        }
        case MD_BLOCK_P: {
            f.type = BlockType::Paragraph;
            if (!st.quoteFrames.empty()) {
                QuoteFrame& qf = st.quoteFrames.back();
                if (qf.callout) { f.type = BlockType::Callout; f.callout = true; f.calloutType = qf.type; }
                else { f.type = BlockType::Quote; }
                if (qf.firstP) {
                    qf.firstP = false;
                    st.probePending = true;
                    st.probeQuoteFrame = static_cast<int>(st.quoteFrames.size() - 1);
                }
            }
            break;
        }
        case MD_BLOCK_CODE:
            f.type = BlockType::Code;
            break;
        case MD_BLOCK_LI: {
            auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            f.type = BlockType::ListItem;
            f.ordered = !st.listOrdered.empty() && st.listOrdered.back();
            f.checked = d->is_task && (d->task_mark == 'x' || d->task_mark == 'X');
            break;
        }
        case MD_BLOCK_QUOTE:
            st.quoteFrames.push_back(QuoteFrame{false, 0, true});
            st.quoteDepth++;
            return 0;
        case MD_BLOCK_ADMONITION: {
            auto* d = static_cast<MD_BLOCK_ADMONITION_DETAIL*>(detail);
            const uint8_t ty = mapCalloutType(std::string(d->type.text, d->type.size));
            st.quoteFrames.push_back(QuoteFrame{true, ty, false});
            st.quoteDepth++;
            st.inCallout = true;
            st.calloutType = ty;
            return 0;
        }
        case MD_BLOCK_UL:
            st.listOrdered.push_back(false);
            st.listDepth++;
            return 0;
        case MD_BLOCK_OL:
            st.listOrdered.push_back(true);
            st.listDepth++;
            return 0;
        case MD_BLOCK_HR: {
            Block b{};
            b.type = BlockType::Hr;
            b.level = 0;
            b.spans = 0;
            b.spanCount = 0;
            doc->blocks.push_back(b);
            return 0;
        }
        case MD_BLOCK_TABLE: {
            auto* d = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
            st.tableActive = true;
            st.tableCols = static_cast<uint8_t>(d->col_count);
            st.tableRowsStart = static_cast<uint32_t>(doc->rows.size());
            st.tableBlockSpans = static_cast<uint32_t>(doc->spans.size());
            st.inHeader = false;
            return 0;
        }
        case MD_BLOCK_THEAD:
            st.inHeader = true;
            return 0;
        case MD_BLOCK_TBODY:
            st.inHeader = false;
            return 0;
        case MD_BLOCK_TR:
            st.rowCellsStart = static_cast<uint32_t>(doc->cells.size());
            st.rowCellCount = 0;
            return 0;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
            st.cellActive = true;
            st.cellSpanStart = static_cast<uint32_t>(doc->spans.size());
            st.cellAlign = static_cast<Align>(d->align);
            return 0;
        }
        case MD_BLOCK_FOOTNOTE_DEF:
            f.type = BlockType::Paragraph;
            break;
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
            return 0;
        default:
            return 0;   // other containers are not part of the v1 model
    }

    if (type != MD_BLOCK_H) f.level = static_cast<uint8_t>(st.quoteDepth + st.listDepth);

    // Insert a placeholder now so blocks land in DOCUMENT order (inner list
    // items must not jump ahead of their parent item). Filled in on leave.
    Block placeholder{};
    placeholder.type = f.type;
    placeholder.level = f.level;
    placeholder.spans = f.spanStart;
    placeholder.spanCount = 0;
    doc->blocks.push_back(placeholder);
    f.blockIndex = static_cast<uint32_t>(doc->blocks.size() - 1);

    st.stack.push_back(f);
    return 0;
}

static void finalizeTable(ParseState& st) {
    MdDocument* doc = st.doc;
    Block b{};
    b.type = BlockType::Table;
    b.level = 0;
    b.spans = st.tableBlockSpans;
    b.spanCount = static_cast<uint16_t>(doc->spans.size() - st.tableBlockSpans);
    b.table.rows = st.tableRowsStart;
    b.table.rowCount = static_cast<uint16_t>(doc->rows.size() - st.tableRowsStart);
    b.table.columns = st.tableCols;
    doc->blocks.push_back(b);
    st.tableActive = false;
    st.tableCols = 0;
}

static int mdLeaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    ParseState& st = *static_cast<ParseState*>(userdata);
    MdDocument* doc = st.doc;

    // Containers / cells were handled without pushing a text frame.
    switch (type) {
        case MD_BLOCK_DOC:
        case MD_BLOCK_HTML:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
        case MD_BLOCK_HR:
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
            return 0;
        case MD_BLOCK_QUOTE:
        case MD_BLOCK_ADMONITION:
            if (!st.quoteFrames.empty()) st.quoteFrames.pop_back();
            st.quoteDepth = std::max(0, st.quoteDepth - 1);
            st.inCallout = !st.quoteFrames.empty() && st.quoteFrames.back().callout;
            st.calloutType = st.inCallout ? st.quoteFrames.back().type : 0;
            return 0;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (!st.listOrdered.empty()) st.listOrdered.pop_back();
            st.listDepth = std::max(0, st.listDepth - 1);
            return 0;
        case MD_BLOCK_TABLE:
            finalizeTable(st);
            return 0;
        case MD_BLOCK_TR: {
            TableRow row;
            row.cells = st.rowCellsStart;
            row.cellCount = st.rowCellCount;
            row.header = st.inHeader;
            doc->rows.push_back(row);
            return 0;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            Cell c;
            c.spans = st.cellSpanStart;
            c.spanCount = static_cast<uint16_t>(doc->spans.size() - st.cellSpanStart);
            c.align = st.cellAlign;
            doc->cells.push_back(c);
            st.rowCellCount++;
            st.cellActive = false;
            return 0;
        }
        default:
            break;
    }

    if (st.stack.empty()) return 0;
    Frame f = st.stack.back();
    st.stack.pop_back();
    const uint32_t spanEnd = static_cast<uint32_t>(doc->spans.size());

    if (st.probePending) st.probePending = false;

    Block& slot = doc->blocks[f.blockIndex];
    auto clearSlot = [&]() { slot.type = BlockType::Paragraph; slot.spanCount = 0; slot.spans = 0; };

    // A P that is the direct child of a span-less list item / footnote def is
    // merged into the parent (loose lists, multi-line footnote defs).
    if (type == MD_BLOCK_P && !st.stack.empty()) {
        Frame& parent = st.stack.back();
        if ((parent.type == BlockType::ListItem || parent.type == BlockType::Paragraph) &&
            parent.directCount == 0 && !parent.childP && f.directCount > 0) {
            parent.childP = true;
            parent.childPStart = f.spanStart;
            parent.childPEnd = spanEnd;
            clearSlot();
            return 0;
        }
    }

    uint32_t bStart = f.spanStart;
    uint32_t bCount = f.directCount;
    if (f.directCount == 0 && f.childP) {
        bStart = f.childPStart;
        bCount = f.childPEnd - f.childPStart;
    }
    if (bCount == 0) { clearSlot(); return 0; }

    slot.type = f.type;
    if (type == MD_BLOCK_P && f.displayMath > 0) slot.type = BlockType::MathBlock;
    slot.level = f.level;
    slot.ordered = f.ordered;
    slot.checked = f.checked;
    slot.spans = bStart;
    slot.spanCount = static_cast<uint16_t>(bCount);
    slot.indent = 0;
    slot.callout = f.callout;
    slot.calloutType = f.calloutType;
    return 0;
}

static int mdEnterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
    ParseState& st = *static_cast<ParseState*>(userdata);
    st.spanStack.push_back(static_cast<int>(type));

    uint32_t target = 0;
    bool have = false;
    if (type == MD_SPAN_WIKILINK) {
        auto* d = static_cast<MD_SPAN_WIKILINK_DETAIL*>(detail);
        target = addLinkTarget(st, d->target, true);
        have = target != 0;
    } else if (type == MD_SPAN_A) {
        auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
        target = addLinkTarget(st, d->href, false);
        have = target != 0;
    } else if (type == MD_SPAN_IMG) {
        auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        target = addLinkTarget(st, d->src, false);
        have = target != 0;
    } else if (!st.targetStack.empty()) {
        target = st.targetStack.back().first;
        have = st.targetStack.back().second;
    }
    st.targetStack.push_back({target, have});

    if (type == MD_SPAN_LATEXMATH_DISPLAY && !st.stack.empty()) st.stack.back().displayMath++;
    return 0;
}

static int mdLeaveSpan(MD_SPANTYPE type, void* /*detail*/, void* userdata) {
    ParseState& st = *static_cast<ParseState*>(userdata);
    if (!st.spanStack.empty()) st.spanStack.pop_back();
    if (!st.targetStack.empty()) st.targetStack.pop_back();
    if (type == MD_SPAN_LATEXMATH_DISPLAY && !st.stack.empty() && st.stack.back().displayMath > 0)
        st.stack.back().displayMath--;
    return 0;
}

static int mdText(MD_TEXTTYPE type, const char* text, MD_SIZE size, void* userdata) {
    return emitText(*static_cast<ParseState*>(userdata), type, text, size);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// MdRenderer — public API
// ═══════════════════════════════════════════════════════════════════════════
MdRenderer::MdRenderer() = default;
MdRenderer::~MdRenderer() = default;

bool MdRenderer::parse(const uint8_t* buf, size_t len) {
    _doc = MdDocument();
    _display = DisplayList();
    _pages.clear();

    if (len > kMaxNoteBytes) {
        len = kMaxNoteBytes;
        _doc.truncated = true;
    }

    Frontmatter fm;
    std::vector<uint8_t> clean = preStrip(buf, len, fm);
    _doc.frontmatter = fm;
    _doc.pool = clean;

    MD_PARSER parser;
    std::memset(&parser, 0, sizeof(parser));
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH |
                   MD_FLAG_HIGHLIGHT | MD_FLAG_WIKILINKS | MD_FLAG_ADMONITIONS |
                   MD_FLAG_FOOTNOTES | MD_FLAG_SUBSCRIPTS | MD_FLAG_SUPERSCRIPTS |
                   MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS;
    parser.enter_block = mdEnterBlock;
    parser.leave_block = mdLeaveBlock;
    parser.enter_span = mdEnterSpan;
    parser.leave_span = mdLeaveSpan;
    parser.text = mdText;

    ParseState st;
    st.doc = &_doc;

    const int rc = md_parse(reinterpret_cast<const char*>(clean.data()), clean.size(), &parser, &st);
    if (st.aborted || rc != 0) _doc.truncated = true;

    if (_doc.truncated) {
        const char* marker = "[truncated]";
        const uint32_t off = static_cast<uint32_t>(_doc.pool.size());
        for (const char* p = marker; *p; ++p) _doc.pool.push_back(static_cast<uint8_t>(*p));
        Span sp;
        sp.type = SpanType::Text;
        sp.off = off;
        sp.len = static_cast<uint32_t>(std::strlen(marker));
        sp.target = 0;
        _doc.spans.push_back(sp);
        Block b{};
        b.type = BlockType::Paragraph;
        b.level = 0;
        b.spans = static_cast<uint32_t>(_doc.spans.size() - 1);
        b.spanCount = 1;
        _doc.blocks.push_back(b);
    }

    _poolBase = _doc.pool.size();
    return !_doc.truncated;
}

bool MdRenderer::parse(NoteSource& src) {
    std::vector<uint8_t> buf;
    readAll(src, buf);
    return parse(buf.data(), buf.size());
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 3 — layout: blocks -> wrapped, measured DisplayList
// ═══════════════════════════════════════════════════════════════════════════
std::string MdRenderer::runText(const Run& run) const {
    if (static_cast<size_t>(run.off) + run.len > _doc.pool.size()) return std::string();
    return std::string(reinterpret_cast<const char*>(_doc.pool.data() + run.off), run.len);
}

std::string MdRenderer::lineText(const Line& line) const {
    std::string out;
    for (uint32_t i = 0; i < line.runCount; ++i) {
        if (line.firstRun + i >= _display.runs.size()) break;
        out += runText(_display.runs[line.firstRun + i]);
    }
    return out;
}

namespace {

struct LayoutCtx {
    MdDocument*       doc = nullptr;
    const MdMetrics*  metrics = nullptr;
    const MdStyles*   styles = nullptr;
    DisplayList*      out = nullptr;
    int contentW = 0;
    int baseX = 0;
    int limit = 0;
    int y = 0;
    uint32_t lineIdx = 0;
    int curX = 0;
    bool hasContent = false;
    int maxH = 0;

    const char* pool() const { return reinterpret_cast<const char*>(doc->pool.data()); }
};

static int styleLineH(const LayoutCtx& lc, StyleId id) {
    const MdStyles& s = *lc.styles;
    const int sz = styleSize(s, id);
    int lh = lc.metrics->lineHeight(styleFont(s, id), sz);
    if (s.lineHeight > 0) lh = s.lineHeight;
    return lh > 0 ? lh : sz;
}

static int styleW(const LayoutCtx& lc, const char* text, uint32_t len, StyleId id) {
    const MdStyles& s = *lc.styles;
    return lc.metrics->textWidth(text, len, styleFont(s, id), styleSize(s, id));
}

static void openLine(LayoutCtx& lc, int y, StyleId base) {
    Line l;
    l.y = static_cast<int16_t>(y);
    l.h = 0;
    l.firstRun = static_cast<uint32_t>(lc.out->runs.size());
    l.runCount = 0;
    l.hardBreak = false;
    l.tableHeader = false;
    l.tableId = -1;
    lc.out->lines.push_back(l);
    lc.lineIdx = static_cast<uint32_t>(lc.out->lines.size() - 1);
    lc.curX = lc.baseX;
    lc.hasContent = false;
    lc.maxH = styleLineH(lc, base);
}

static void emitRun(LayoutCtx& lc, StyleId style, uint32_t off, uint32_t len,
                    uint32_t target, int w) {
    Run r;
    r.style = style;
    r.off = off;
    r.len = len;
    r.target = target;
    r.x = static_cast<int16_t>(lc.curX);
    r.y = lc.out->lines[lc.lineIdx].y;
    r.w = static_cast<int16_t>(w);
    lc.out->runs.push_back(r);
    lc.out->lines[lc.lineIdx].runCount++;
    lc.curX += w;
    const int lh = styleLineH(lc, style);
    if (lh > lc.maxH) lc.maxH = lh;
    lc.hasContent = true;
}

static void closeLine(LayoutCtx& lc) {
    Line& L = lc.out->lines[lc.lineIdx];
    L.h = static_cast<int16_t>(lc.maxH);
    lc.y = L.y + lc.maxH + lc.styles->lineGap;
}

// Wrap a raw text slice (normal prose): collapse whitespace, break at spaces,
// and char-wrap a token that is itself wider than the content area.
//
// A separator space is EMITTED as a run, not just accounted for in `curX`, so
// `lineText()`/copying round-trip the source words. `pendingSpace`/`spaceOff`
// carry a separator across span boundaries (md4c splits inline styles into
// separate spans with no space between them).
static void wrapText(LayoutCtx& lc, const char* s, uint32_t n, StyleId style,
                     uint32_t target, bool& pendingSpace, uint32_t& spaceOff) {
    const uint32_t baseOff = static_cast<uint32_t>(s - lc.pool());
    const int spaceW = styleW(lc, " ", 1, style);
    uint32_t i = 0;
    while (i < n) {
        if (s[i] == ' ' || s[i] == '\t') {
            pendingSpace = true;
            spaceOff = baseOff + i;
            ++i;
            continue;
        }
        uint32_t j = i;
        while (j < n && s[j] != ' ' && s[j] != '\t') ++j;
        const uint32_t wlen = j - i;
        const int w = styleW(lc, s + i, wlen, style);

        const bool sep = lc.hasContent && pendingSpace;
        if (sep && lc.curX + spaceW + w > lc.limit) {
            closeLine(lc);
            openLine(lc, lc.y, style);
            pendingSpace = false;
        } else if (sep) {
            emitRun(lc, style, spaceOff, 1, target, spaceW);
            pendingSpace = false;
        } else {
            pendingSpace = false;
        }

        if (w > lc.limit - lc.baseX) {
            // Unbreakable token wider than a line: wrap by codepoint.
            uint32_t k = 0;
            while (k < wlen) {
                uint32_t cp = 0;
                const uint32_t cl = decodeUtf8(s + i + k, wlen - k, cp);
                const int gw = styleW(lc, s + i + k, cl, style);
                if (lc.hasContent && lc.curX + gw > lc.limit) {
                    closeLine(lc);
                    openLine(lc, lc.y, style);
                }
                emitRun(lc, style, baseOff + i + k, cl, target, gw);
                k += cl;
            }
        } else {
            emitRun(lc, style, baseOff + i, wlen, target, w);
        }
        i = j;
    }
}

// Lay out a block's inline spans. Assumes a line is already open.
static void wrapSpans(LayoutCtx& lc, uint32_t spanStart, uint16_t spanCount, StyleId base) {
    const MdDocument& doc = *lc.doc;
    const uint32_t spanEnd = spanStart + spanCount;
    bool pendingSpace = false;
    uint32_t spaceOff = 0;
    for (uint32_t i = spanStart; i < spanEnd; ++i) {
        const Span& sp = doc.spans[i];
        const char* t = lc.pool() + sp.off;
        if (sp.len == 1 && t[0] == '\n') {
            closeLine(lc);
            openLine(lc, lc.y, base);
            pendingSpace = false;
            continue;
        }
        const StyleId sid = spanTypeToStyle(sp.type, base);
        wrapText(lc, t, sp.len, sid, sp.target, pendingSpace, spaceOff);
    }
}

// One code source line, char-wrapped at the content width (spaces preserved).
static void wrapCodeLine(LayoutCtx& lc, uint32_t off, uint32_t len) {
    const char* s = lc.pool() + off;
    uint32_t k = 0, runStart = 0;
    while (k < len) {
        uint32_t cp = 0;
        const uint32_t cl = decodeUtf8(s + k, len - k, cp);
        const int gw = styleW(lc, s + k, cl, StyleId::Code);
        if (lc.hasContent && lc.curX + gw > lc.limit) {
            if (k > runStart)
                emitRun(lc, StyleId::Code, off + runStart, k - runStart, 0,
                        styleW(lc, s + runStart, k - runStart, StyleId::Code));
            closeLine(lc);
            openLine(lc, lc.y, StyleId::Code);
            runStart = k;
        }
        k += cl;
    }
    if (len > runStart)
        emitRun(lc, StyleId::Code, off + runStart, len - runStart, 0,
                styleW(lc, s + runStart, len - runStart, StyleId::Code));
}

static void layoutCodeSpans(LayoutCtx& lc, uint32_t spanStart, uint16_t spanCount,
                            int x, int y, int width) {
    const MdDocument& doc = *lc.doc;
    lc.baseX = x;
    lc.limit = x + width;
    lc.y = y;
    openLine(lc, y, StyleId::Code);

    const uint32_t spanEnd = spanStart + spanCount;
    for (uint32_t i = spanStart; i < spanEnd; ++i) {
        const Span& sp = doc.spans[i];
        const char* t = lc.pool() + sp.off;
        if (sp.len == 1 && t[0] == '\n') {
            closeLine(lc);
            openLine(lc, lc.y, StyleId::Code);
            continue;
        }
        // A CODE span may itself contain '\n' (md4c emits whole code blocks).
        uint32_t seg = 0;
        for (uint32_t k = 0; k < sp.len; ++k) {
            if (t[k] == '\n') {
                if (k > seg) wrapCodeLine(lc, sp.off + seg, k - seg);
                closeLine(lc);
                openLine(lc, lc.y, StyleId::Code);
                seg = k + 1;
            }
        }
        if (sp.len > seg) wrapCodeLine(lc, sp.off + seg, sp.len - seg);
    }
    closeLine(lc);
}

// Append synthetic text to the pool (layout-owned; truncated by _poolBase).
static uint32_t appendPool(LayoutCtx& lc, const char* text, uint32_t len) {
    const uint32_t off = static_cast<uint32_t>(lc.doc->pool.size());
    for (uint32_t i = 0; i < len; ++i)
        lc.doc->pool.push_back(static_cast<uint8_t>(text[i]));
    return off;
}

// Table v1.5: cap + h-scroll is deferred (needs app focus), so every table is
// rendered with the documented STACK fallback: one "key: value" line per cell,
// which always fits the content width. No borders.
static void layoutTable(LayoutCtx& lc, const Block& block, int x, int y, int width) {
    const MdDocument& doc = *lc.doc;
    const Table& tbl = block.table;
    if (tbl.rowCount == 0 || tbl.columns == 0) return;

    lc.baseX = x;
    lc.limit = x + width;
    lc.y = y;

    const TableRow& firstRow = doc.rows[tbl.rows];
    const bool hasHeader = firstRow.header;

    const uint32_t rowBegin = tbl.rows + (hasHeader ? 1 : 0);
    const uint32_t rowEnd = tbl.rows + tbl.rowCount;

    for (uint32_t r = rowBegin; r < rowEnd; ++r) {
        const TableRow& row = doc.rows[r];
        for (uint16_t c = 0; c < row.cellCount && c < tbl.columns; ++c) {
            const Cell& cell = doc.cells[row.cells + c];

            openLine(lc, lc.y, StyleId::Table);
            bool pendingSpace = false;
            uint32_t spaceOff = 0;

            // Key (header cell, else "colN").
            if (hasHeader && firstRow.cellCount > c) {
                const Cell& key = doc.cells[firstRow.cells + c];
                for (uint16_t s = 0; s < key.spanCount; ++s) {
                    const Span& sp = doc.spans[key.spans + s];
                    wrapText(lc, lc.pool() + sp.off, sp.len, StyleId::Strong, sp.target,
                             pendingSpace, spaceOff);
                }
            } else {
                char buf[16];
                const int n = std::snprintf(buf, sizeof(buf), "col%u", static_cast<unsigned>(c + 1));
                const uint32_t off = appendPool(lc, buf, static_cast<uint32_t>(n));
                wrapText(lc, lc.pool() + off, static_cast<uint32_t>(n), StyleId::Strong, 0,
                         pendingSpace, spaceOff);
            }
            const uint32_t colon = appendPool(lc, ": ", 2);
            wrapText(lc, lc.pool() + colon, 2, StyleId::Table, 0, pendingSpace, spaceOff);
            for (uint16_t s = 0; s < cell.spanCount; ++s) {
                const Span& sp = doc.spans[cell.spans + s];
                wrapText(lc, lc.pool() + sp.off, sp.len, StyleId::Table, sp.target,
                         pendingSpace, spaceOff);
            }
            closeLine(lc);
        }
    }
}

static int layoutBlock(LayoutCtx& lc, const Block& block, int x, int y, int width) {
    const MdStyles& s = *lc.styles;
    // Headings store the heading level in `level` and use no container indent;
    // every other block stores the quote+list depth there.
    const int indent = (block.type == BlockType::Heading) ? 0 : block.level * s.indentUnit;
    const int bx = x + indent;
    const int bw = width - indent;
    if (bw <= 0) return y;

    switch (block.type) {
        case BlockType::Heading: {
            const StyleId hid = (block.level <= 1) ? StyleId::Heading1
                               : (block.level == 2) ? StyleId::Heading2
                                                    : StyleId::Heading3;
            lc.baseX = bx; lc.limit = bx + bw; lc.y = y;
            openLine(lc, y, hid);
            wrapSpans(lc, block.spans, block.spanCount, hid);
            closeLine(lc);
            return lc.y;
        }
        case BlockType::Paragraph:
        case BlockType::Quote:
        case BlockType::Callout:
        case BlockType::MathBlock: {
            const StyleId st = block.type == BlockType::Quote      ? StyleId::Quote
                             : block.type == BlockType::Callout    ? StyleId::Callout
                             : block.type == BlockType::MathBlock  ? StyleId::MathBlock
                                                                    : StyleId::Body;
            lc.baseX = bx; lc.limit = bx + bw; lc.y = y;
            openLine(lc, y, st);
            wrapSpans(lc, block.spans, block.spanCount, st);
            closeLine(lc);
            return lc.y;
        }
        case BlockType::ListItem: {
            lc.baseX = bx; lc.limit = bx + bw; lc.y = y;
            openLine(lc, y, StyleId::ListBullet);
            const char* marker = block.checked ? "[x] " : (block.ordered ? "1. " : "* ");
            const uint32_t mlen = static_cast<uint32_t>(std::strlen(marker));
            const uint32_t moff = appendPool(lc, marker, mlen);
            emitRun(lc, StyleId::ListBullet, moff, mlen, 0, styleW(lc, marker, mlen, StyleId::ListBullet));
            wrapSpans(lc, block.spans, block.spanCount, StyleId::Body);
            closeLine(lc);
            return lc.y;
        }
        case BlockType::Code:
            layoutCodeSpans(lc, block.spans, block.spanCount, bx, y, bw);
            return lc.y;
        case BlockType::Table:
            layoutTable(lc, block, bx, y, bw);
            return lc.y;
        case BlockType::Hr: {
            // Hard page break; never drawn as a rule.
            Line l;
            l.y = static_cast<int16_t>(y);
            l.h = 0;
            l.firstRun = static_cast<uint32_t>(lc.out->runs.size());
            l.runCount = 0;
            l.hardBreak = true;
            l.tableHeader = false;
            l.tableId = -1;
            lc.out->lines.push_back(l);
            return y;
        }
        default:
            return y;
    }
}

}  // namespace

bool MdRenderer::layout(const MdMetrics& metrics, const MdStyles& styles) {
    _display = DisplayList();
    _pages.clear();
    if (_doc.pool.size() > _poolBase) _doc.pool.resize(_poolBase);

    const int contentW = styles.contentW > 0 ? styles.contentW : (SCREEN_W - 2 * styles.marginX);

    LayoutCtx lc;
    lc.doc = &_doc;
    lc.metrics = &metrics;
    lc.styles = &styles;
    lc.out = &_display;
    lc.contentW = contentW;
    lc.baseX = styles.marginX;
    lc.limit = styles.marginX + contentW;
    lc.y = 0;

    bool first = true;
    for (const Block& block : _doc.blocks) {
        if (block.spanCount == 0 && block.type != BlockType::Hr) continue;
        if (!first) lc.y += styles.paraGap;
        first = false;
        lc.y = layoutBlock(lc, block, styles.marginX, lc.y, contentW);
    }
    _display.lines.shrink_to_fit();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 4 — paginate
// ═══════════════════════════════════════════════════════════════════════════
bool MdRenderer::paginate(const MdStyles& styles) {
    _pages.clear();
    const int contentH = styles.contentH > 0 ? styles.contentH : CONTENT_H;
    const auto& lines = _display.lines;

    // Map a table block id -> its header line (repeat on overflow).
    std::vector<int32_t> headerLineForTable;
    for (uint32_t i = 0; i < lines.size(); ++i) {
        if (lines[i].tableHeader && lines[i].tableId >= 0) {
            if (lines[i].tableId >= static_cast<int32_t>(headerLineForTable.size()))
                headerLineForTable.resize(lines[i].tableId + 1, -1);
            headerLineForTable[lines[i].tableId] = static_cast<int32_t>(i);
        }
    }

    Page page;
    page.firstLine = 0;
    page.lineCount = 0;
    page.height = 0;

    uint32_t i = 0;
    while (i < lines.size()) {
        const Line& line = lines[i];
        if (line.hardBreak) {
            // Explicit page boundary; the break itself is never drawn.
            if (page.lineCount > 0) { _pages.push_back(page); page = Page{}; page.firstLine = 0; }
            ++i;
            continue;
        }
        if (page.lineCount > 0 && page.height + line.h > contentH) {
            _pages.push_back(page);
            page = Page{};
            page.firstLine = i;
            page.lineCount = 0;
            page.height = 0;
            if (line.tableId >= 0 &&
                line.tableId < static_cast<int32_t>(headerLineForTable.size()) &&
                headerLineForTable[line.tableId] >= 0) {
                const Line& hdr = lines[static_cast<uint32_t>(headerLineForTable[line.tableId])];
                page.firstLine = static_cast<uint32_t>(headerLineForTable[line.tableId]);
                page.height = hdr.h;
                page.lineCount = 1;
            }
        }
        if (page.lineCount == 0) page.firstLine = i;

        // Clip guard — the fits invariant is absolute. A line that cannot fit even
        // on a freshly opened page (or cannot fit under a repeated table header) is
        // CLIPPED here instead of silently overflowing CONTENT_H: the page is
        // clamped and flagged, and render() draws that last line dimmed. The app's
        // chrome owns the visible indicator, no text is invented, and the note's
        // own text is untouched — clipping is a display rule only.
        if (page.height + line.h > contentH) {
            page.height    = static_cast<uint16_t>(contentH);
            page.truncated = true;
            page.lineCount++;
            ++i;
            continue;
        }

        page.height = static_cast<uint16_t>(page.height + line.h);
        page.lineCount++;
        ++i;
    }
    if (page.lineCount > 0) _pages.push_back(page);
    if (_pages.empty()) _pages.push_back(Page{0, 0, 0});
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 5 — LVGL draw: ONE page only
// ═══════════════════════════════════════════════════════════════════════════
#if MDR_HAVE_LVGL

static uint32_t styleColor(const MdStyles& s, StyleId id) {
    switch (id) {
        case StyleId::Heading1:
        case StyleId::Heading2:
        case StyleId::Heading3: return s.colorText;
        case StyleId::Emph:
        case StyleId::Strong:   return s.colorText;
        case StyleId::Strike:   return s.colorDim;
        case StyleId::Highlight:return s.colorText;
        case StyleId::Math:
        case StyleId::MathBlock:return s.colorAccent;
        case StyleId::Code:     return s.colorText;
        case StyleId::Quote:    return s.colorQuote;
        case StyleId::Callout:  return s.colorAccent;
        case StyleId::Wikilink: return s.colorWikilink;
        case StyleId::Link:     return s.colorAccent;
        case StyleId::Tag:      return s.colorDim;
        case StyleId::Table:    return s.colorText;
        case StyleId::ListBullet:return s.colorDim;
        case StyleId::Truncated:return s.colorDim;
        default:                return s.colorText;
    }
}

void MdRenderer::render(int page, void* parent, const MdStyles& styles) {
    lv_obj_t* parentObj = static_cast<lv_obj_t*>(parent);
    if (!parentObj) return;

    // A page change destroys and rebuilds the objects: flat pool pressure.
    if (_pageObj) {
        lv_obj_delete(static_cast<lv_obj_t*>(_pageObj));
        _pageObj = nullptr;
    }
    if (page < 0 || page >= static_cast<int>(_pages.size())) return;

    const int contentW = styles.contentW > 0 ? styles.contentW : (SCREEN_W - 2 * styles.marginX);
    const int contentH = styles.contentH > 0 ? styles.contentH : CONTENT_H;

    lv_obj_t* root = lv_obj_create(parentObj);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_pos(root, styles.marginX, 0);
    lv_obj_set_size(root, contentW, contentH);
    _pageObj = root;

    const Page& pg = _pages[static_cast<size_t>(page)];
    const auto& lines = _display.lines;
    if (pg.lineCount == 0) return;
    const int baseY = lines[pg.firstLine].y;

    // On a clipped page the last line is the one that could not fit, so draw it
    // with the dim Truncated style and no background: the clip is then visible on
    // the page itself. The app's chrome owns the fuller indicator, and no text is
    // invented here.
    const int32_t clippedLine = pg.truncated
        ? static_cast<int32_t>(pg.firstLine + pg.lineCount - 1)
        : -1;

    for (uint32_t li = pg.firstLine; li < pg.firstLine + pg.lineCount && li < lines.size(); ++li) {
        const Line& line = lines[li];
        if (line.hardBreak) continue;
        const bool isClipped = (static_cast<int32_t>(li) == clippedLine);
        for (uint32_t ri = line.firstRun; ri < line.firstRun + line.runCount; ++ri) {
            if (ri >= _display.runs.size()) break;
            const Run& run = _display.runs[ri];
            const std::string text = runText(run);
            if (text.empty()) continue;

            const StyleId effStyle = isClipped ? StyleId::Truncated : run.style;

            lv_obj_t* label = lv_label_create(root);
            lv_label_set_text(label, text.c_str());
            lv_obj_set_style_text_font(label, asFont(styleFont(styles, effStyle),
                                                     styleSize(styles, effStyle)),
                                       LV_PART_MAIN);
            lv_obj_set_style_text_color(label, lv_color_hex(styleColor(styles, effStyle)),
                                        LV_PART_MAIN);
            if (!isClipped && run.style == StyleId::Code) {
                lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_bg_color(label, lv_color_hex(styles.colorCodeBg), LV_PART_MAIN);
            } else if (!isClipped && run.style == StyleId::Highlight) {
                lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_bg_color(label, lv_color_hex(styles.colorHighlight), LV_PART_MAIN);
            }
            lv_obj_set_pos(label, run.x, static_cast<int>(line.y) - baseY);
        }
    }
}

#endif  // MDR_HAVE_LVGL

}  // namespace mdrender
