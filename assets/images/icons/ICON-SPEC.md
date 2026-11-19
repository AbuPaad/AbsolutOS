# Icon spec — what the pipeline needs

Drop files here → `firmware/AbsolutOS/assets/images/icons/` with these **exact** names.
Nothing gets resized on our side: the mask generator copies the pixels 1:1.

## Hard requirements (non-negotiable)

| Rule | Why |
|---|---|
| **PNG, exactly the pixel size in the table** | The generator does no scaling. If it's 32px wide it ships 32px wide. |
| **RGBA, alpha is 0 or 255 only** — no soft edges | A half-transparent edge becomes a half-ink pixel at 4bpp and reads as grey mush. |
| **Ink is pure white `#FFFFFF`**, everything else transparent | Only alpha is read; colour is thrown away and re-applied from the theme at draw time. |
| **≥1px fully transparent margin on all four sides** | Masks are cropped to the ink bbox, so a glyph touching the edge gets its outer stroke clipped. |
| **Strokes exactly 1px wide** | 2px strokes at these sizes merge into blobs. |
| **Diagonals step 1px per row** (45°, no doubles) | A 2px-then-1px staircase is what made the current chart-line read as a blob. |
| Licence must allow redistribution — OFL / MIT / Apache-2.0 / CC0 / CC-BY | We commit the PNG to the repo. CC-BY needs attribution in `THIRD_PARTY_NOTICES.md`. |

If you pull an SVG instead: it must be drawn on the target grid with 1-unit strokes
(`viewBox="0 0 16 14"` etc.), no `stroke-width` above 1, no filters. Rasterise with
`rsvg-convert -w 16 -h 14`. Anything drawn at 24–32px and scaled down fails the
"strokes exactly 1px" rule and will look like what we have now.

## The list

| File | Size (px) | Role | Notes |
|---|---|---|---|
| `tab_table.png` | **16×14** | Expressions section | grid / rows of lines |
| `tab_graph.png` | **16×14** | Graph section | axes + rising curve |
| `tab_symbols.png` | **16×14** | Symbols / f(x) section | a radical, or a bare ƒ |
| `mode_ddx.png` | **26×40** | Calculus, derivative mode | `d` over a fraction bar over `dx`, 1px bar |
| `mode_integral.png` | **24×24** | Calculus, integral mode | ∫ with a small × (tabler style) |
| `battery_low.png` | **16×10** | Status bar, <33% | shell + 1 bar |
| `battery_half.png` | **16×10** | Status bar, 33–66% | shell + 2 bars |
| `battery_full.png` | **16×10** | Status bar, >66% | shell + 3 bars |
| `help_question.png` *(optional)* | **8×12** | The `?` help chip | today it's a text glyph; an icon would let it match the set |

The three tab glyphs at **16×14** are the tightest constraint in the whole UI — that
is the box, not a preference. If a design can't survive 16×14, tell me and I'll widen
the chip instead of mangling the art.

## After you drop them

```bash
python3 scripts/check_icon_assets.py            # validates size / 1-bit alpha / margin
python3 scripts/gen_grapher_tab_icons.py        # rebuild the mask header
python3 scripts/gen_calculus_mode_icons.py
python3 scripts/gen_statusbar_icons.py
```

Each generator has `--check`, which fails if a committed header has drifted from its
source PNG — so a hand-edited header can't sneak through.
