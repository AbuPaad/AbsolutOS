# Greek, roots and powers

The display face carries the full Greek alphabet: 03b1 03b2 03b3 03b4 03b5 03b8 03bb 03bc 03bd 03c0 03c1 03c3 03c4 03c6 03c7 03c8 03c9
and the capitals 0393 0394 0398 039b 03a3 03a6 03a8 03a9.

Inline maths swaps LaTeX tokens for real glyphs, so $\sqrt{2}$, $\pi r^2$ and
$x^2 + y^2 = r^2$ come out as symbols rather than as command names.

---

# Display maths is drawn, not spelled

A $$...$$ block goes through the 2D renderer, so fractions stack and limits sit
over their operator:

$$\int_{0}^{1} x^2 dx = \frac{1}{3}$$

$$\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}$$

Those two fractions are laid out with a real horizontal bar.

---

# Comparisons, arrows and subscripts

Every comparison the face can draw: $x \leq 5$, $y \geq 3$, $a \neq b$.

Implication and arrows render too: $A \implies B$ and $f: X \to Y$.

Subscripts use the ten subscript digits: $a_0 a_1 a_2 a_3 a_4 a_5 a_6 a_7 a_8 a_9$.

---

# Symbols with no glyph

Some symbols have no glyph in the face. Write them in ASCII instead: "+-" for
plus-or-minus, the word "approximately" as a stand-in for the approx sign,
d/dx for a partial derivative, -> for a right arrow, <= and >= for
less-or-equal and greater-or-equal.

Use the plain ASCII hyphen - as a minus sign.
%%ai: model=google/gemini-2.5-flash-lite hash=36dd5873 pages=4 image=-%%
