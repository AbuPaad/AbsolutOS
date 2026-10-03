#!/usr/bin/env python3
"""gen_casio_slots.py — generate the Casio launcher slot table.

Single source of truth: the slot table in
    ~/musings/sserialprintthing/casio-cpp-transplant/IMPLEMENTATION.md   (§1)
The C++ side must never be hand-edited: this script rewrites
    src/ui/generated/CasioSlots.generated.h
so the operator edits the markdown table in exactly one place.

Why a slot table at all: the Casio launcher is positional (`N:LABEL`), and its
order + 5-character labels differ from the numos card grid. Keeping that out of
APPS[] means numos' order, names and pixels are untouched (the byte-identity
baseline stays a valid gate), while app launching still resolves to the same
APPS[] ids through the same launch callback.

Usage:
    python3 scripts/gen_casio_slots.py                 # rewrite the header
    python3 scripts/gen_casio_slots.py --check         # verify only (CI/agent gate)
    python3 scripts/gen_casio_slots.py --table PATH    # non-default markdown

Exit codes: 0 ok · 1 validation error · 2 the file does not match (--check).

Stdlib only, cross-platform (same style as scripts/compare-ppm.py).
"""

import argparse
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TABLE = os.path.normpath(os.path.join(
    REPO, "..", "..", "..", "sserialprintthing", "casio-cpp-transplant",
    "IMPLEMENTATION.md"))
OUT = os.path.join(REPO, "src", "ui", "generated", "CasioSlots.generated.h")

SLOTS_PER_PAGE = 8
LABEL_MAX = 5
ID_MAX = 255


def parse_table(path):
    """Return [(page, slot, label, app_id, name, status)] from the §1 md table."""
    rows = []
    with open(path, "r", encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            s = line.strip()
            if not s.startswith("|"):
                continue
            cells = [c.strip() for c in s.strip("|").split("|")]
            if len(cells) < 5:
                continue
            if cells[0] in ("Page", "") or set(cells[0]) <= set("-: "):
                continue
            try:
                page = int(cells[0])
                slot = int(cells[1])
                app_id = int(cells[3])
            except ValueError:
                continue
            rows.append({
                "line": lineno,
                "page": page,
                "slot": slot,
                "label": cells[2],
                "id": app_id,
                "name": cells[4],
                "status": cells[5] if len(cells) > 5 else "",
            })
    return rows


def validate(rows):
    errs = []
    if not rows:
        return ["no table rows found — is §1 still a markdown table?"]
    seen = {}
    pages = {}
    for r in rows:
        tag = "line %d (%s)" % (r["line"], r["label"] or "?")
        if not 1 <= r["slot"] <= SLOTS_PER_PAGE:
            errs.append("%s: slot %d out of 1..%d" % (tag, r["slot"], SLOTS_PER_PAGE))
        if not 0 <= r["id"] <= ID_MAX:
            errs.append("%s: id %d out of range" % (tag, r["id"]))
        if not r["label"]:
            errs.append("%s: empty label" % tag)
        if len(r["label"]) > LABEL_MAX:
            errs.append("%s: label '%s' is %d chars, max %d"
                        % (tag, r["label"], len(r["label"]), LABEL_MAX))
        key = (r["page"], r["slot"])
        if key in seen:
            errs.append("%s: duplicate page %d slot %d (also line %d)"
                        % (tag, r["page"], r["slot"], seen[key]))
        seen[key] = r["line"]
        pages.setdefault(r["page"], []).append(r["slot"])
        if r["id"] in [x["id"] for x in rows if x is not r]:
            errs.append("%s: id %d appears more than once" % (tag, r["id"]))
    # each page must be contiguous from slot 1
    for page in sorted(pages):
        want = list(range(1, len(pages[page]) + 1))
        if sorted(pages[page]) != want:
            errs.append("page %d slots %s are not contiguous from 1 (expected %s)"
                        % (page, sorted(pages[page]), want))
    # pages must be contiguous from 1
    if sorted(pages) != list(range(1, len(pages) + 1)):
        errs.append("pages %s are not contiguous from 1" % sorted(pages))
    return errs


def render(rows, table_path):
    rows = sorted(rows, key=lambda r: (r["page"], r["slot"]))
    rel = os.path.relpath(table_path, REPO)
    out = []
    out.append("/*")
    out.append(" * CasioSlots.generated.h — GENERATED FILE, DO NOT EDIT BY HAND.")
    out.append(" *")
    out.append(" * Source of truth: the §1 slot table in")
    out.append(" *   %s" % rel)
    out.append(" * Regenerate:      python3 scripts/gen_casio_slots.py")
    out.append(" *")
    out.append(" * Edit the markdown table, not this file. The Casio launcher is positional")
    out.append(" * (N:LABEL); `id` is the real MainMenu::APPS[] app id, so launching still goes")
    out.append(" * through the one launch callback and APPS[] stays the single source of truth")
    out.append(" * for app identity. numos' order/names/pixels are not affected.")
    out.append(" */")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ui {")
    out.append("")
    out.append("struct CasioSlot {")
    out.append("    uint8_t     id;     // MainMenu::APPS[] app id")
    out.append("    const char* label;  // N:LABEL, <= 5 chars")
    out.append("};")
    out.append("")
    out.append("inline constexpr CasioSlot kCasioSlots[] = {")
    page = None
    for r in rows:
        if r["page"] != page:
            page = r["page"]
            out.append("    // ── page %d ──" % page)
        out.append('    { %3d, "%-5s" },   // %s' % (r["id"], r["label"], r["name"]))
    out.append("};")
    out.append("inline constexpr int kCasioSlotCount =")
    out.append("    static_cast<int>(sizeof(kCasioSlots) / sizeof(kCasioSlots[0]));")
    out.append("")
    out.append("} // namespace ui")
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="verify the generated header is up to date; do not write")
    ap.add_argument("--table", default=DEFAULT_TABLE, help="markdown table path")
    args = ap.parse_args()

    if not os.path.exists(args.table):
        print("table not found: %s" % args.table, file=sys.stderr)
        return 1
    rows = parse_table(args.table)
    errs = validate(rows)
    if errs:
        print("gen_casio_slots: %d problem(s):" % len(errs), file=sys.stderr)
        for e in errs:
            print("  - %s" % e, file=sys.stderr)
        return 1

    text = render(rows, args.table)
    if args.check:
        cur = ""
        if os.path.exists(OUT):
            with open(OUT, "r", encoding="utf-8") as fh:
                cur = fh.read()
        if cur != text:
            print("gen_casio_slots: %s is stale — rerun without --check" % OUT,
                  file=sys.stderr)
            return 2
        print("gen_casio_slots: up to date (%d slots)" % len(rows))
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("gen_casio_slots: wrote %s (%d slots)"
          % (os.path.relpath(OUT, REPO), len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
