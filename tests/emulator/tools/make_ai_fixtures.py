#!/usr/bin/env python3
"""
make_ai_fixtures.py — generate the emulator fixtures the AI wrapper needs.

Everything lands under tests/emulator/fs/, which is the sandbox root the
emulator passes as --fs-root, so the app sees /ai/... paths.

  fs/ai/config.json           transport=replay (sends NOTHING), prompts/results dirs
  fs/ai/prompts/*.jpg         placeholder prompt images (the name is what lists)
  fs/ai/results/seed.md       one pre-existing answer so "Recent" is not empty
  fs/ai/replay/answer.sse     a recorded streaming response

The .sse fixture is the interesting one: it is a real SSE stream in the same
shape OpenRouter sends (comment lines, `data:` payloads, a final [DONE]), whose
concatenated `delta.content` fields spell one JSON object with `title` and
`answer`. Chunks are split at awkward boundaries ON PURPOSE — including mid
escape sequence and mid `\\u` — because that is exactly what the scanner's
one-pending-byte rule exists for.

Run:  python3 tests/emulator/tools/make_ai_fixtures.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]      # repo root
FS = ROOT / "tests" / "emulator" / "fs" / "ai"

# ── the sample answer (markdown, pages split on a line of exactly ---) ───────
PAGE1 = """# Solving x^2 - 5x + 6 = 0

This is a monic quadratic, so factor it: look for two numbers that multiply to
**6** and add to **-5**. They are -2 and -3.

So the left side factors as:

(x - 2)(x - 3) = 0

A product is zero when either factor is zero, which gives x = 2 or x = 3.
"""

PAGE2 = """# Checking the roots

Substitute each root back into the original expression.

For x = 2: 4 - 10 + 6 = 0. Good.

For x = 3: 9 - 15 + 6 = 0. Good.

Both roots check out, so the solution set is {2, 3}.
"""

PAGE3 = """# What this tells you

- The discriminant is 25 - 24 = 1, a perfect square, so the roots are rational.
- The vertex sits at x = 2.5, exactly between the two roots.
- The parabola opens upward because the leading coefficient is positive.

```
b^2 - 4ac = (-5)^2 - 4*1*6 = 1
```
"""

ANSWER = PAGE1 + "\n---\n\n" + PAGE2 + "\n---\n\n" + PAGE3

PAYLOAD = {
    "title": "Quadratic roots",
    "answer": ANSWER,
    "followups": [
        "graph y = x^2 - 5x + 6",
        "solve x^2 + x - 6 = 0",
        "what is the discriminant for?",
    ],
    "confidence": 0.92,
    "tool_query": "",
    "tool_server": "",
    "transcribed_question": "solve x^2 - 5x + 6 = 0 and check the roots",
}

# ── SSE framing ──────────────────────────────────────────────────────────────
def sse_lines(chunks):
    """Wrap JSON chunks into SSE `data:` lines, content escaped like the API."""
    out = [": OPENROUTER PROCESSING", ""]
    for ch in chunks:
        payload = json.dumps({"choices": [{"delta": {"content": ch}}]}, separators=(",", ":"))
        out.append("data: " + payload)
        out.append("")
    out.append("data: [DONE]")
    out.append("")
    return "\n".join(out)


def main():
    FS.mkdir(parents=True, exist_ok=True)

    # 1. config.json — replay transport: the app sends nothing anywhere.
    (FS / "config.json").write_text(json.dumps({
        "base_url": "https://openrouter.ai/api/v1",
        "models_url": "https://openrouter.ai/api/v1/models",
        "api_key": "",
        "model": "google/gemini-2.5-flash-lite",
        "timeout_ms": 60000,
        "prompts_dir": "/ai/prompts",
        "results_dir": "/ai/results",
        "transport": "replay",
        "retention_max_files": 200,
        "retention_max_bytes": 33554432,
        "sys_prompt": (
            "You are a calculator assistant on a 320x200 keypad device. "
            "Answer in Markdown that the device can draw: headings, short "
            "paragraphs, - and 1. lists, bold, italic, inline and fenced code, "
            "> quotes. No tables, no images, no HTML, no footnotes, no URLs. "
            "The reader takes the answer one page at a time, so split it into "
            "pages with a line containing exactly ---, and put each break "
            "where the topic changes: one topic per page, the way a chapter is "
            "split into sections. Open every page with a heading. Never squeeze "
            "a multi-part answer onto one page to keep it short - length is "
            "fine, but keep each page on a single topic. Write maths inside "
            "$...$ (inline) or $$...$$ (display). ONLY these LaTeX names render; "
            "use no other LaTeX (no \\frac, no \\begin, no \\left): "
            "\\alpha \\beta \\gamma \\delta \\epsilon \\theta \\lambda \\mu "
            "\\nu \\pi \\rho \\sigma \\tau \\phi \\chi \\psi \\omega "
            "\\Gamma \\Delta \\Theta \\Lambda \\Sigma \\Phi \\Psi \\Omega; "
            "\\int \\iint \\iiint \\oint \\sum \\sqrt \\infty \\partial "
            "\\nabla \\times \\mp \\circ \\ast \\oplus \\otimes; "
            "\\leq \\geq \\neq \\equiv \\approx \\sim \\cong \\propto "
            "\\subset \\subseteq \\in \\notin \\ll \\gg; "
            "\\cup \\cap \\setminus \\emptyset \\forall \\exists \\neg "
            "\\land \\lor \\implies \\iff \\therefore \\because; "
            "\\to \\leftarrow \\leftrightarrow; \\lfloor \\rfloor \\lceil "
            "\\rceil; \\mathbbN \\mathbbZ \\mathbbQ \\mathbbR \\mathbbC "
            "\\mathbbH. For anything not in that list write plain ASCII inside "
            "$...$: x^2, a/b, x_i, sqrt(x), pi, <=, >=, !=, +-."
        ),
    }, indent=2) + "\n")

    # 2. prompts — placeholders. Structurally valid (SOI + APP0 + EOI); the
    #    filename is what the Capture screen lists today and what the camera
    #    replaces later.
    prompts = FS / "prompts"
    prompts.mkdir(exist_ok=True)
    jpeg = b"\xff\xd8\xff\xe0" + b"\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00" + \
           b"\xff\xfe" + b"\x00\x16" + b"NumOS AI prompt placeholder" + b"\xff\xd9"
    for name in ("factor_quadratic.jpg", "check_my_working.jpg", "explain_steps.jpg"):
        (prompts / name).write_bytes(jpeg)

    # 3. results — one pre-existing answer, so Recent has content on first open.
    results = FS / "results"
    results.mkdir(exist_ok=True)
    (results / "seed-derivative.md").write_text(
        "# Derivative of x^3\n\n"
        "Bring the power down and reduce it by one.\n\n"
        "d/dx x^3 = 3x^2\n\n"
        "---\n\n"
        "# Why\n\n"
        "- The rule is d/dx x^n = n*x^(n-1).\n"
        "- Here n = 3, so you get 3*x^2.\n"
        "\n%%ai: model=google/gemini-2.5-flash-lite hash=seed0000 pages=2 image=-%%\n"
    )

    # 4. the replay stream. Chunks cut at deliberately hostile boundaries.
    blob = json.dumps(PAYLOAD, separators=(",", ":"))
    chunks, i, step = [], 0, 37
    while i < len(blob):
        chunks.append(blob[i:i + step])
        i += step
    replay = FS / "replay"
    replay.mkdir(exist_ok=True)
    (replay / "answer.sse").write_text(sse_lines(chunks))

    print(f"wrote {FS}")
    print(f"  answer.sse: {len(blob)} bytes of JSON in {len(chunks)} chunks "
          f"of {step}; {len(ANSWER)} answer chars")
    for p in sorted(FS.rglob("*")):
        if p.is_file():
            print(f"  {p.relative_to(FS)}  {p.stat().st_size} B")


if __name__ == "__main__":
    main()
