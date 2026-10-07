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
PAGE1 = r"""# Greek, roots and powers

The display face carries the full Greek alphabet: α β γ δ ε θ λ μ ν π ρ σ τ φ χ ψ ω
and the capitals Γ Δ Θ Λ Σ Φ Ψ Ω.

Inline maths swaps LaTeX tokens for real glyphs, so $\sqrt{2}$, $\pi r^2$ and
$x^2 + y^2 = r^2$ come out as symbols rather than as command names.
"""

PAGE2 = r"""# Display maths is drawn, not spelled

A $$...$$ block goes through the 2D renderer, so fractions stack and limits sit
over their operator:

$$\int_{0}^{1} x^2 dx = \frac{1}{3}$$

$$\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}$$

Those two fractions are laid out with a real horizontal bar.
"""

PAGE3 = r"""# Comparisons, arrows and subscripts

Every comparison the face can draw: $x \leq 5$, $y \geq 3$, $a \neq b$.

Implication and arrows render too: $A \implies B$ and $f: X \to Y$.

Subscripts use the ten subscript digits: $a_0 a_1 a_2 a_3 a_4 a_5 a_6 a_7 a_8 a_9$.
"""

PAGE4 = r"""# Symbols with no glyph

Some symbols have no glyph in the face. Write them in ASCII instead: "+-" for
plus-or-minus, the word "approximately" as a stand-in for the approx sign,
d/dx for a partial derivative, -> for a right arrow, <= and >= for
less-or-equal and greater-or-equal.

Use the plain ASCII hyphen - as a minus sign.
"""

ANSWER = PAGE1 + "\n---\n\n" + PAGE2 + "\n---\n\n" + PAGE3 + "\n---\n\n" + PAGE4

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
            "fine, but keep each page on a single topic. "
            "Write maths inside $...$ (inline) or $$...$$ (display). Inside "
            "$$...$$ you may use \\frac{a}{b}, a^b and a_b: they are drawn as "
            "real stacked notation. Inline, write a/b, x^2 and x_i instead. "
            "The display font carries only 292 glyphs. ONLY these LaTeX names "
            "render; use no other LaTeX (no \\begin, no \\left, no \\overline): "
            "\\alpha \\beta \\gamma \\delta \\epsilon \\theta \\lambda \\mu "
            "\\nu \\pi \\rho \\sigma \\tau \\phi \\chi \\psi \\omega "
            "\\Gamma \\Delta \\Theta \\Lambda \\Sigma \\Phi \\Psi \\Omega; "
            "\\int \\sum \\sqrt \\infty \\times; "
            "\\leq \\geq \\neq \\implies \\to \\leftarrow. "
            "For anything outside that list write plain ASCII inside $...$: "
            "x^2, a/b, x_i, sqrt(x), pi, <=, >=, !=, +-. "
            "There is NO glyph for any of these, so never emit them or the LaTeX "
            "names that produce them: the plus-minus and minus-or-plus signs, the "
            "approx sign, smallcircle, asterisk operator, partial, nabla, oplus, "
            "otimes, equiv, tilde-relation, congruent, propto, subset, subseteq, "
            "element-of, not-element-of, much-less, much-greater, union, "
            "intersection, set-minus, empty-set, for-all, exists, not, and, or, "
            "iff, therefore, because, left-right arrow, floor and ceiling "
            "brackets, iint, iiint, oint, the blackboard-bold sets, and the "
            "Unicode minus sign U+2212. Write them in ASCII instead: \"+\" for "
            "plus-or-minus, the word approximately for the approx sign, d/dx for "
            "a partial derivative, grad for nabla, in and not-in for set "
            "membership, subset for the subset sign, union and intersect for the "
            "set operators, and and or for the logical ones, <= and >= for "
            "less-or-equal and greater-or-equal, != for not-equal, -> for a "
            "right arrow, and R, Z, Q, N for the number sets. Use ONLY the "
            "standard ASCII hyphen-minus - as a minus sign, never U+2212."
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
        "\n%%ai: model=google/gemini-2.5-flash-lite hash=seed0000 pages=2 image=- "
        "tool_query=derivative%20of%20x%5E3 tool_server=wolfram "
        "transcribed=d/dx%20x%5E3%%\n"
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

    # The Wolfram check's recorded body: the check hop's happy path has to be
    # reachable with no network and nothing spent, exactly like the answer
    # stream's. NOTE these fixtures live in the EMULATOR's fs root only — the
    # firmware image carries no /ai/replay, so a device has no recorded body to
    # find, and the fixture transport is compiled out of a device build.
    (replay / "wolfram-llm-api.txt").write_text(
        'Query:\n'
        '"derivative of x^3"\n'
        '\n'
        'Input interpretation:\n'
        'd/dx (x^3)\n'
        '\n'
        'Derivative:\n'
        'd/dx (x^3) = 3 x^2\n'
        '\n'
        'Plots:\n'
        'image: https://www6b3.wolframalpha.com/Calculate/MSP/MSP9999h0000000000000000000?MSPStoreType=image/png&s=1\n'
        '\n'
        'Wolfram|Alpha website result for "derivative of x^3":\n'
        'https://www.wolframalpha.com/input?i=derivative+of+x%5E3\n'
    )

    print(f"wrote {FS}")
    print(f"  answer.sse: {len(blob)} bytes of JSON in {len(chunks)} chunks "
          f"of {step}; {len(ANSWER)} answer chars")
    for p in sorted(FS.rglob("*")):
        if p.is_file():
            print(f"  {p.relative_to(FS)}  {p.stat().st_size} B")


if __name__ == "__main__":
    main()
