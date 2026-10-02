#!/usr/bin/env python3
"""Generate wasm/numos-keycontext.js from the C++ context tables.

WHY GENERATED
-------------
The web pad and the on-device soft-key bar must agree on which keys matter in
which app. Two hand-maintained copies is exactly how they drift apart, and the
drift is silent: the web shows a key as dead while the firmware still routes it.
So src/ui/KeyContext.h is the single source of truth and this script derives the
JavaScript from it.

`--check` regenerates into memory and compares against the file on disk, exiting
non-zero on a difference. Wire that into CI or a pre-commit hook: a stale
generated file is then a build failure instead of a mystery.

Pure stdlib. Parses the header with anchored regexes on a deliberately regular
layout, and FAILS LOUDLY if anything stops matching — a generator that returns
an empty table on a parse miss would quietly disable every key in the UI.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import NoReturn

REPO = Path(__file__).resolve().parent.parent
APP_CONTEXT_H = REPO / "src" / "ui" / "AppContext.h"
KEY_CONTEXT_H = REPO / "src" / "ui" / "KeyContext.h"
OUTPUT = REPO / "wasm" / "numos-keycontext.js"

BANNER = """// GENERATED FILE — DO NOT EDIT BY HAND.
//
// Source of truth: src/ui/KeyContext.h (the key roles) and src/ui/AppContext.h
// (the context slugs). Regenerate with:
//     python3 scripts/gen_key_context.py
// Verify without writing with:
//     python3 scripts/gen_key_context.py --check
//
// Hand edits are reverted by the next run, and --check turns the drift into a
// failure rather than a silent disagreement with the firmware.
"""


def fail(message: str) -> NoReturn:
    print(f"gen_key_context: ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        fail(f"cannot read {path}: {error}")


def parse_names(app_context: str) -> dict[str, str]:
    """Ctx name -> human display name, read out of ctxName() only.

    The same scoping rule as parse_slugs(), in the opposite direction: the strip
    must show the firmware's own word for the app ("Game Boy", "AI") rather than
    a prettified slug invented in JavaScript.
    """
    marker = "inline const char* ctxName(Ctx ctx)"
    start = app_context.find(marker)
    if start < 0:
        fail("ctxName() not found in AppContext.h")
    body = app_context[start:]
    end = body.find("\n}")
    if end < 0:
        fail("ctxName() body is not terminated as expected")
    pairs = re.findall(r'case\s+Ctx::(\w+)\s*:\s*return\s+"([^"]+)"\s*;', body[:end])
    if not pairs:
        fail("no display names parsed from ctxName()")
    return {name: label for name, label in pairs}


def parse_slugs(app_context: str) -> dict[str, str]:
    """Ctx name -> slug, read out of ctxSlug() only.

    ctxName() has the same shape but different strings, so the parse is scoped
    to the ctxSlug() body — mistaking one for the other would ship CSS selectors
    with spaces in them.
    """
    marker = "inline const char* ctxSlug(Ctx ctx)"
    start = app_context.find(marker)
    if start < 0:
        fail("ctxSlug() not found in AppContext.h")
    body = app_context[start:]
    end = body.find("\n}")
    if end < 0:
        fail("ctxSlug() body is not terminated as expected")
    pairs = re.findall(r'case\s+Ctx::(\w+)\s*:\s*return\s+"([^"]+)"\s*;', body[:end])
    if not pairs:
        fail("no slugs parsed from ctxSlug()")
    slugs = {name: slug for name, slug in pairs}
    bad = [s for s in slugs.values() if not re.fullmatch(r"[a-z0-9]+", s)]
    if bad:
        fail(f"slugs must be lowercase ASCII for CSS selectors: {bad}")
    return slugs


def parse_defaults(key_context: str) -> dict[str, str]:
    """KeyCode -> role for keys no context mentions (defaultRole()).

    Line-based on purpose. The switch groups its labels:

        case KeyCode::HOME:
        case KeyCode::MODE:
        case KeyCode::ON:
            return Role::Secondary;

    A regex pairing a label with its return on the same construct silently reads
    only the LAST label of the group, which would emit a JS default table missing
    HOME and MODE — the universal escapes, i.e. exactly the keys that must never
    go dead. So collect pending labels and attach them all to the next return,
    and fail if a label never gets one.
    """
    start = key_context.find("inline Role defaultRole(KeyCode key)")
    if start < 0:
        fail("defaultRole() not found in KeyContext.h")
    body = key_context[start:]
    end = body.find("\n}")
    if end < 0:
        fail("defaultRole() body is not terminated as expected")

    defaults: dict[str, str] = {}
    pending: list[str] = []
    saw_default = False
    for raw in body[:end].splitlines():
        line = raw.strip()
        if line == "default:":
            # The catch-all arm names no key, so its role is not a per-key
            # default. It IS the fallback the table relies on, so record that we
            # saw it and let the bare return through.
            saw_default = True
            continue
        label = re.fullmatch(r"case\s+KeyCode::(\w+)\s*:", line)
        if label:
            pending.append(label.group(1))
            continue
        returned = re.fullmatch(r"return\s+Role::(\w+)\s*;", line)
        if returned:
            if pending:
                for key in pending:
                    defaults[key] = returned.group(1).lower()
                pending = []
                continue
            if not saw_default:
                fail(f"defaultRole(): bare return before any case: {line!r}")
            continue
    if pending:
        fail(f"defaultRole(): cases with no return: {pending}")
    if not defaults:
        fail("no defaults parsed from defaultRole()")
    return defaults


def parse_digit_macro(key_context: str) -> list[tuple[str, str, str]]:
    """Expand the NUMOS_CTX_DIGITS() helper macro into explicit entries.

    Expanding here rather than emitting a macro reference keeps the JS dumber:
    the generator resolves the indirection once, and the output is a flat table
    a human can read.
    """
    marker = "#define NUMOS_CTX_DIGITS()"
    start = key_context.find(marker)
    if start < 0:
        fail("NUMOS_CTX_DIGITS() macro not found in KeyContext.h")
    body = key_context[start + len(marker):]
    end = body.find("\n\n")
    if end < 0:
        fail("NUMOS_CTX_DIGITS() macro is not terminated by a blank line")
    entries = re.findall(
        r"\{\s*KeyCode::(\w+)\s*,\s*Role::(\w+)\s*,\s*\"([^\"]*)\"\s*\}", body[:end])
    if not entries:
        fail("no entries parsed from NUMOS_CTX_DIGITS()")
    return [(key, role.lower(), legend) for key, role, legend in entries]


def parse_tables(key_context: str) -> dict[str, list[tuple[str, str, str]]]:
    """kCtxXxx[] array name -> ordered [(key id, role, legend)]."""
    digits = parse_digit_macro(key_context)
    tables: dict[str, list[tuple[str, str, str]]] = {}
    pattern = re.compile(
        r"inline constexpr KeyRole\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\n\};", re.S)
    for match in pattern.finditer(key_context):
        name, body = match.group(1), match.group(2)
        entries: list[tuple[str, str, str]] = []
        for line in body.splitlines():
            line = line.strip().rstrip(",")
            if not line or line.startswith("//"):
                continue
            if line.startswith("NUMOS_CTX_DIGITS()"):
                entries.extend(digits)
                continue
            entry = re.fullmatch(
                r"\{\s*KeyCode::(\w+)\s*,\s*Role::(\w+)\s*,\s*\"([^\"]*)\"\s*\}", line)
            if entry is None:
                fail(f"unparsed entry in {name}: {line!r}")
            entries.append((entry.group(1), entry.group(2).lower(), entry.group(3)))
        if not entries:
            fail(f"table {name} parsed as empty")
        tables[name] = entries
    if not tables:
        fail("no kCtx* tables parsed from KeyContext.h")
    return tables


def parse_context_table(key_context: str) -> list[tuple[str, str]]:
    """[(Ctx name, kCtx array name)] in declared order."""
    start = key_context.find("inline constexpr ContextMap kContextTable[]")
    if start < 0:
        fail("kContextTable[] not found in KeyContext.h")
    body = key_context[start:]
    end = body.find("\n};")
    if end < 0:
        fail("kContextTable[] is not terminated as expected")
    pairs = re.findall(r"NUMOS_CTX_ENTRY\((\w+),\s*(\w+)\)", body[:end])
    if not pairs:
        fail("no entries parsed from kContextTable[]")
    return pairs


def js_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def generate() -> str:
    app_context = read(APP_CONTEXT_H)
    key_context = read(KEY_CONTEXT_H)
    slugs = parse_slugs(app_context)
    names = parse_names(app_context)
    defaults = parse_defaults(key_context)
    tables = parse_tables(key_context)
    context_table = parse_context_table(key_context)

    lines: list[str] = [BANNER]
    lines.append("export const NUMOS_KEY_CONTEXT_SCHEMA = 1;")
    lines.append("")
    lines.append("/** Context slug -> the firmware's own display name for the app. */")
    lines.append("export const NUMOS_KEY_CONTEXT_NAMES = Object.freeze({")
    for ctx_name, _ in context_table:
        if ctx_name not in slugs or ctx_name not in names:
            fail(f"context {ctx_name} is missing a slug or a display name")
        lines.append(f"  {js_string(slugs[ctx_name])}: {js_string(names[ctx_name])},")
    lines.append("});")
    lines.append("")
    lines.append("/** Roles for keys no context lists (mirrors defaultRole()). */")
    lines.append("export const NUMOS_KEY_CONTEXT_DEFAULT_ROLE = Object.freeze({")
    for key in sorted(defaults):
        lines.append(f"  {key}: {js_string(defaults[key])},")
    lines.append("});")
    lines.append("")
    lines.append("/**")
    lines.append(" * Context slug -> ordered [{ id, role, legend }].")
    lines.append(" *")
    lines.append(" * ORDER IS SIGNIFICANCE: it is the display order of the soft-key")
    lines.append(" * bar and of the web 'relevant now' strip. `id` is a logical key id")
    lines.append(" * from src/input/KeyCodes.h — resolve it to a numeric code through")
    lines.append(" * the keypad catalog, never by re-declaring numbers here.")
    lines.append(" */")
    lines.append("export const NUMOS_KEY_CONTEXTS = Object.freeze({")
    seen: set[str] = set()
    for ctx_name, array_name in context_table:
        if ctx_name not in slugs:
            fail(f"context {ctx_name} has no slug in ctxSlug()")
        slug = slugs[ctx_name]
        if slug in seen:
            fail(f"duplicate slug {slug}")
        seen.add(slug)
        entries = tables.get(array_name)
        if entries is None:
            fail(f"context {ctx_name} references unknown table {array_name}")
        lines.append(f"  {slug}: Object.freeze([")
        for key, role, legend in entries:
            lines.append(
                f"    Object.freeze({{ id: {js_string(key)}, role: "
                f"{js_string(role)}, legend: {js_string(legend)} }}),")
        lines.append("  ]),")
    lines.append("});")
    lines.append("")
    lines.append("/** The context slugs this build knows, in declaration order. */")
    lines.append("export const NUMOS_KEY_CONTEXT_SLUGS = Object.freeze([")
    for ctx_name, _ in context_table:
        lines.append(f"  {js_string(slugs[ctx_name])},")
    lines.append("]);")
    lines.append("")
    lines.append("""/**
 * Role of one logical key id in one context.
 *
 * Unlisted keys fall back to the shared defaults, so the JS agrees with
 * ctxRoleFor() in the firmware instead of inventing a second policy. An unknown
 * slug returns "disabled" for everything: a context the web does not know about
 * must not light keys up.
 */
export function contextRoleFor(slug, id) {
  const entries = NUMOS_KEY_CONTEXTS[slug];
  if (!entries) return NUMOS_KEY_CONTEXT_DEFAULT_ROLE[id] || "disabled";
  for (const entry of entries) {
    if (entry.id === id) return entry.role;
  }
  return NUMOS_KEY_CONTEXT_DEFAULT_ROLE[id] || "disabled";
}

/** The ordered primary keys of a context — what the strip and the bar show. */
export function contextPrimaryKeys(slug) {
  const entries = NUMOS_KEY_CONTEXTS[slug];
  if (!entries) return [];
  return entries.filter((entry) => entry.role === "primary");
}

/**
 * The firmware's display name for a context ("Game Boy", "AI").
 *
 * Falls back to the slug, never to a prettified guess: an unknown context should
 * look obviously unknown rather than plausibly named.
 */
export function contextName(slug) {
  return NUMOS_KEY_CONTEXT_NAMES[slug] || slug;
}
""")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true",
        help="verify the checked-in file is current; write nothing")
    args = parser.parse_args()

    generated = generate()
    if args.check:
        try:
            current = OUTPUT.read_text(encoding="utf-8")
        except OSError as error:
            fail(f"cannot read {OUTPUT}: {error}")
        if current != generated:
            print(
                f"gen_key_context: {OUTPUT.relative_to(REPO)} is STALE.\n"
                "  The web pad and the firmware context table disagree.\n"
                "  Fix: python3 scripts/gen_key_context.py",
                file=sys.stderr)
            return 1
        print(f"gen_key_context: {OUTPUT.relative_to(REPO)} is current.")
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists() and OUTPUT.read_text(encoding="utf-8") == generated:
        print(f"gen_key_context: {OUTPUT.relative_to(REPO)} already current.")
        return 0
    OUTPUT.write_text(generated, encoding="utf-8")
    print(f"gen_key_context: wrote {OUTPUT.relative_to(REPO)}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
