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
 * MdRenderer.h — shared Markdown render module (NumOS notes / AI wrapper).
 *
 * ONE module, four stages inside it (renderer.md §2.3):
 *   pre-strip -> md4c -> layout -> paginate -> LVGL draw.
 *
 * The module is the seam shared by the notes reader (FileSource: a .md file)
 * and the AI wrapper (BufferSource: a growing RAM buffer). It never opens a
 * file itself (only FileSource does), never touches the global theme or the
 * shared StatusBar, and never fetches a URL.
 *
 * Portability: this header is LVGL-free. Fonts are carried as opaque
 * `const void*` (cast to `const lv_font_t*` on LVGL builds) and the render
 * parent is an opaque `void*` (cast to `lv_obj_t*`). The core (parse /
 * layout / paginate) compiles on a plain g++ host; the LVGL draw stage is
 * compiled only when <lvgl.h> is on the include path (see MdRenderer.cpp).
 *
 * Geometry is named constants, never literals (renderer.md §1). A literal
 * 240 already caused a bug in GameBoyApp — do not introduce a new one.
 */

#pragma once
// SCREEN_WIDTH / SCREEN_HEIGHT = the logical canvas declared once in Config.h
#include "../Config.h"
// Font accessors (ui::fontUi() …) are LVGL-only. Guarded so this header stays
// compilable on a plain host (tests/host/notes_mdrender_test.cpp) — the core
// contract is that nothing here needs <lvgl.h>.
#if defined(ARDUINO) || defined(NATIVE_SIM)
#include "../ui/ThemeFonts.h"
#endif

#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mdrender {

// ═══════════════════════════════════════════════════════════════════════════
// Geometry — the constraint that drives everything (renderer.md §1)
// ═══════════════════════════════════════════════════════════════════════════
constexpr int SCREEN_W     = SCREEN_WIDTH;           ///< canvas width (Config.h)
constexpr int SCREEN_H     = SCREEN_HEIGHT;          ///< canvas height (Config.h)
// The fx-82 letterbox is applied by the display driver as a flush offset, so the
// renderer's canvas IS the visible area and crops nothing itself.
constexpr int SHELL_CROP_H = 0;                      ///< px hidden by the fx-82 shell (offset lives in DisplayDriver)
constexpr int VISIBLE_H    = SCREEN_H - SHELL_CROP_H; ///< 156 px actually visible
constexpr int STATUS_BAR_H = 24;                     ///< ui::StatusBar::HEIGHT
constexpr int CONTENT_H    = VISIBLE_H - STATUS_BAR_H; ///< 132 reading px

// ═══════════════════════════════════════════════════════════════════════════
// NoteSource — the byte seam (parser.md §2, renderer.md §2)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * An interface that yields bytes. The module's parse() takes either a raw
 * (buf,len) or a NoteSource; nothing else in the module may take a file path.
 */
class NoteSource {
public:
    virtual ~NoteSource() = default;
    /// Read up to len bytes into buf; returns bytes read (0 = EOF).
    virtual size_t read(uint8_t* buf, size_t len) = 0;
    /// Total size in bytes when known, else 0.
    virtual size_t size() const = 0;
};

/** In-memory source: used by host tests and later by the AI wrapper. */
class BufferSource : public NoteSource {
public:
    BufferSource() = default;
    BufferSource(const uint8_t* data, size_t len) : _data(data), _len(len), _remaining(len) {}

    size_t read(uint8_t* buf, size_t len) override {
        const size_t n = (len < _remaining) ? len : _remaining;
        for (size_t i = 0; i < n; ++i) buf[i] = _data[_pos + i];
        _pos += n;
        _remaining -= n;
        return n;
    }
    size_t size() const override { return _len; }

private:
    const uint8_t* _data = nullptr;
    size_t         _len = 0;
    size_t         _pos = 0;
    size_t         _remaining = 0;
};

/**
 * Path source: used by the notes app. Opens through LittleFS on device and
 * through the native hal/FileSystem.h shim on the emulator, so the module
 * itself stays I/O-free. The path is owned here and nowhere else.
 */
class FileSource : public NoteSource {
public:
    explicit FileSource(const char* path);
    ~FileSource() override;

    size_t read(uint8_t* buf, size_t len) override;
    size_t size() const override;

    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

private:
    struct Impl;
    Impl* _impl = nullptr;
};

/** Read a whole NoteSource into a byte vector (host/emulator). */
bool readAll(NoteSource& src, std::vector<uint8_t>& out);

// ═══════════════════════════════════════════════════════════════════════════
// Block / span model (parser.md §2)
// ═══════════════════════════════════════════════════════════════════════════

enum class SpanType : uint8_t {
    Text,       ///< normal text
    Code,       ///< `code` span / code block line
    Emph,       ///< *em*
    Strong,     ///< **strong**
    Strike,     ///< ~~strike~~
    Highlight,  ///< ==highlight==
    Math,       ///< $...$ inline math
    Link,       ///< [text](url)
    Wikilink,   ///< [[Note]] / [[Note|Display]]
    Tag,        ///< #tag
    Super,      ///< ^super^   (extension: MD_FLAG_SUPERSCRIPTS)
    Sub,        ///< ~sub~     (extension: MD_FLAG_SUBSCRIPTS)
};

/**
 * One span: (offset,length) into the document's single text pool. No per-span
 * heap strings. NOTE: parser.md §2 shows uint16_t offsets, but §5 caps notes
 * at 512 KB — uint16_t would silently truncate the pool at 64 KB. We use
 * uint32_t offsets to honour the 512 KB cap (see final report).
 */
struct Span {
    SpanType  type;
    uint32_t  off;      ///< byte offset into MdDocument::pool
    uint32_t  len;      ///< byte length
    uint32_t  target;   /// link/wikilink target index (into linkTargets), else 0
};

enum class BlockType : uint8_t {
    Heading, Paragraph, ListItem, Code, Quote, Callout, Table, Hr, MathBlock
};

enum class Align : uint8_t { None, Left, Center, Right };

struct Cell {
    uint32_t spans;      ///< index into the span vector (inline spans only)
    uint16_t spanCount;
    Align    align;
};

struct TableRow {
    uint32_t cells;      ///< index into the cell vector
    uint16_t cellCount;
    bool     header;
};

struct Table {
    uint32_t rows;       ///< index into the row vector
    uint16_t rowCount;
    uint8_t  columns;
};

struct Block {
    BlockType type;
    uint8_t   level;     ///< heading level / list depth / quote depth
    bool      ordered;   ///< list is ordered
    bool      checked;   ///< task list item is checked
    uint32_t  spans;     ///< index into the span vector
    uint16_t  spanCount;
    uint16_t  indent;    ///< px, resolved by the renderer from level
    Table     table;     ///< valid when type == Table
    bool      callout = false; ///< inside an admonition container
    uint8_t   calloutType = 0; ///< valid when callout: 0=note,1=tip,2=important,3=warning,4=caution
};

/** Lite frontmatter (parser.md §3): a whitelist of keys, not a YAML parser. */
struct Frontmatter {
    std::string              title;
    std::vector<std::string> tags;
    std::vector<std::string> aliases;
    int                      order = 0;
    bool                     calc = true;   ///< calc:false hides from the browser
    bool                     present = false;
};

struct MdDocument {
    std::vector<uint8_t> pool;       ///< the clean (pre-stripped) text
    std::vector<Span>    spans;
    std::vector<Block>   blocks;
    std::vector<Cell>    cells;      ///< table cells
    std::vector<TableRow> rows;      ///< table rows
    /// Link/wikilink targets as (off,len) into pool — no heap strings.
    std::vector<std::pair<uint32_t, uint32_t>> linkTargets;
    Frontmatter          frontmatter;
    bool                 truncated = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// Styles — app-owned tokens (renderer.md §3). Never the global theme.
// ═══════════════════════════════════════════════════════════════════════════

enum class StyleId : uint8_t {
    Body, Heading1, Heading2, Heading3,
    Emph, Strong, Strike, Highlight, Math, MathBlock,
    Code, Quote, Callout, Wikilink, Link, Tag, Super, Sub,
    Table, ListBullet, Truncated,
    Count
};

/**
 * A struct of app-owned style/colour/font-size values. The notes app fills
 * one with its own fonts/colours; the AI app fills one with AiTheme. The
 * module never touches the global theme or the shared StatusBar.
 *
 * Fonts are opaque `const void*` (cast to `const lv_font_t*` on LVGL builds)
 * so this header stays LVGL-free. Sizes are px. Colors are 0xRRGGBB.
 */
struct MdStyles {
    const void* bodyFont    = nullptr;   ///< e.g. ui::fontUi()
    const void* headingFont = nullptr;   ///< e.g. ui::fontDisplay()
    const void* codeFont    = nullptr;   ///< monospace (deferred; body for now)
    const void* mathFont    = nullptr;   ///< STIX Two Math (math runs)

    // Sizes must be compiled into lv_conf.h. That set is 10/12/14/20: there is
    // NO Montserrat 16 (LV_FONT_MONTSERRAT_16 == 0), so the "12/14/16" toggle
    // in renderer.md §4 is implemented as 12/14/20. Never pass 16 here.
    int bodySize    = 14;
    int headingSize = 14;
    int codeSize    = 12;
    int lineHeight  = 0;                 ///< 0 = auto (derived from bodySize)
    int lineGap     = 2;                 ///< extra px between wrapped lines
    int paraGap     = 4;                 ///< extra px between blocks

    uint32_t colorText      = 0xFFFFFF;
    uint32_t colorDim       = 0x9E9E9E;
    uint32_t colorAccent    = 0x4FC3F7;
    uint32_t colorCodeBg    = 0x1C1C1C;
    uint32_t colorQuote     = 0x9E9E9E;
    uint32_t colorHighlight = 0xFFEB3B;
    uint32_t colorWikilink  = 0x4FC3F7;

    int contentW   = 0;                  ///< 0 = default (SCREEN_W - 2*marginX)
    int contentH   = 0;                  ///< 0 = default (CONTENT_H)
    int marginX    = 4;
    int indentUnit = 12;
};

// ═══════════════════════════════════════════════════════════════════════════
// Metrics — the seam that lets layout run on host, emulator and device
// (parser.md §2, README "Environments")
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Font metrics for the layout pass. On device/emulator the implementation
 * wraps lv_txt_get_size / lv_font_get_glyph_width; on host a deterministic
 * stub lets tests assert page breaks and line text as plain text.
 */
class MdMetrics {
public:
    virtual ~MdMetrics() = default;
    /// Width of a single-line run of len bytes (no wrapping).
    virtual int textWidth(const char* text, uint32_t len, const void* font, int size) const = 0;
    /// Line height for a font/size.
    virtual int lineHeight(const void* font, int size) const = 0;
    /// Advance width of one codepoint (for char-level wrapping of code).
    virtual int glyphWidth(const void* font, int size, uint32_t codepoint) const = 0;
};

/**
 * Host metrics: a deterministic, LVGL-free stub. Widths are a fixed advance
 * per character class (0.6em for regular chars, 0.3em for spaces), so wrapping
 * and pagination are reproducible on any g++ host. This is NOT the device
 * metric — it exists so tests/host can assert the fiddly wrapping/paging
 * logic as plain text (parser.md §7).
 */
class HostMetrics : public MdMetrics {
public:
    int textWidth(const char* text, uint32_t len, const void* font, int size) const override;
    int lineHeight(const void* font, int size) const override;
    int glyphWidth(const void* font, int size, uint32_t codepoint) const override;
};

#if defined(ARDUINO) || defined(NATIVE_SIM)
/**
 * Real metrics for the device and the emulator: wraps lv_txt_get_size /
 * lv_font_get_glyph_width. Declared only when LVGL is on the include path, so
 * the host test TU never sees an lvgl.h dependency. When `font` is null the
 * implementation falls back to a compiled-in Montserrat size (12/14/20 only —
 * 16 is compiled out).
 */
class LvglMetrics : public MdMetrics {
public:
    int textWidth(const char* text, uint32_t len, const void* font, int size) const override;
    int lineHeight(const void* font, int size) const override;
    int glyphWidth(const void* font, int size, uint32_t codepoint) const override;
};
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Display list (renderer.md §2.1) — the seam a future precompiled bundle
// or a custom draw renderer would consume.
// ═══════════════════════════════════════════════════════════════════════════

struct Run {
    StyleId   style;
    uint32_t  off;       ///< byte offset into MdDocument::pool
    uint32_t  len;       ///< byte length
    uint32_t  target;    /// link target index, else 0
    int16_t   x, y;      ///< position within the page
    int16_t   w;         ///< measured width
};

struct Line {
    int16_t   y;         ///< top of the line within the page
    int16_t   h;         ///< line height
    uint32_t  firstRun;  ///< index into the run vector
    uint16_t  runCount;
    bool      hardBreak; ///< a thematic break follows this line (page boundary)
    bool      tableHeader; ///< this line is a table header row (repeat on overflow)
    int32_t   tableId = -1; ///< owning table block index, else -1
};

struct DisplayList {
    std::vector<Run>  runs;
    std::vector<Line> lines;
};

// ═══════════════════════════════════════════════════════════════════════════
// Pages (renderer.md §2.3)
// ═══════════════════════════════════════════════════════════════════════════

struct Page {
    uint32_t firstLine;  ///< index into DisplayList::lines
    uint16_t lineCount;
    uint16_t height;     ///< total px; every emitted page fits CONTENT_H
    /// A line was clipped to keep the line above true — its bottom is cut at the
    /// page edge and it is drawn dimmed. Clipping is a display rule only: the
    /// note's own text is never altered by it. The app's chrome owns the marker.
    bool     truncated = false;
};

// ═══════════════════════════════════════════════════════════════════════════
// The engine
// ═══════════════════════════════════════════════════════════════════════════

class MdRenderer {
public:
    MdRenderer();
    ~MdRenderer();

    MdRenderer(const MdRenderer&) = delete;
    MdRenderer& operator=(const MdRenderer&) = delete;

    // ── Stage 1+2: pre-strip + md4c ──────────────────────────────────────
    /// Parse a raw buffer (the clean bytes). Fills doc().
    bool parse(const uint8_t* buf, size_t len);
    /// Read a whole NoteSource, then parse it. Fills doc().
    bool parse(NoteSource& src);

    // ── Stage 3: layout ──────────────────────────────────────────────────
    /// Walk blocks and emit a flat list of positioned, styled runs grouped
    /// into lines, measured against real (or stub) font metrics.
    bool layout(const MdMetrics& metrics, const MdStyles& styles);

    // ── Stage 4: paginate ────────────────────────────────────────────────
    /// Emit a page table (first-line index + height per page). Every emitted
    /// page fits CONTENT_H; a thematic break is a hard page break. A line that
    /// cannot fit even on a fresh page is clipped to the page and the page is
    /// flagged — see pageTruncated().
    bool paginate(const MdStyles& styles);

    // ── Stage 5: LVGL draw (LVGL builds only) ────────────────────────────
    /// Build LVGL objects for ONE page only. A page change destroys and
    /// rebuilds them. Never call off the UI task.
    void render(int page, void* parent, const MdStyles& styles);

    // ── Accessors (tests / apps) ─────────────────────────────────────────
    const MdDocument&   doc() const     { return _doc; }
    const DisplayList&  display() const  { return _display; }
    const std::vector<Page>& pages() const { return _pages; }
    int  pageCount() const { return static_cast<int>(_pages.size()); }
    /// True if a note-level cap was hit (size/block/span limits), not pagination.
    bool isTruncated() const { return _doc.truncated; }
    /// True if this page had to clip a line to keep the fits invariant. The
    /// clipped line is drawn dimmed; the app's chrome owns the fuller indicator.
    /// Clipping never alters the note's text.
    bool pageTruncated(int page) const {
        return page >= 0 && page < static_cast<int>(_pages.size()) &&
               _pages[static_cast<size_t>(page)].truncated;
    }

    /// Resolve a run's text out of the pool (for tests / debugging).
    std::string runText(const Run& run) const;
    /// Resolve a line's concatenated text (for tests / debugging).
    std::string lineText(const Line& line) const;

private:
    MdDocument               _doc;
    DisplayList              _display;
    std::vector<Page>        _pages;

    /// Pool size at the end of parse(). layout() truncates back to this before
    /// appending synthetic table keys/markers, so re-layout on a font change is
    /// idempotent and never grows the pool without bound.
    size_t                   _poolBase = 0;

    // LVGL draw state (LVGL builds only).
    void*                    _pageObj = nullptr;
};

}  // namespace mdrender
