#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

# Generate the NumOS UI type ladder (15 / 11 px) for the numos theme.
# Requires: lv_font_conv on PATH.
#
# WHY TWO SOURCE FONTS IN ONE FACE
#   The ladder is Montserrat, but UI text is not pure ASCII. Screen copy already
#   carries - × ÷ ° µ · — … → ↑ ↓, and a Giac result can put - × ÷ ∫ √ π ∞ ⁻ ¹ ⁺
#   ∑ ∂ ∆ ≤ ≥ ≠ into a label as *data*. Montserrat has no maths glyphs, so a
#   single-font face renders every one of those as tofu (the same failure that
#   made the battery an empty rectangle).
#   lv_font_conv draws one output face from several sources: Montserrat for the
#   text runs, STIX Two Math for the maths/typographic runs. One face, no
#   runtime fallback logic, no per-label font juggling.
#
# Output:
#   src/fonts/lv_font_montserrat_15.c   (display / headline)
#   src/fonts/lv_font_montserrat_11.c   (body)
# Provenance headers (OFL-1.1) are emitted in the same run, never patched later.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LATIN="assets/fonts/Montserrat-Regular.ttf"
MATH="assets/fonts/STIXTwoMath-Regular.ttf"
OUT_DIR="src/fonts"

cd "${ROOT_DIR}"

for f in "${LATIN}" "${MATH}"; do
    if [[ ! -f "${f}" ]]; then
        echo "Missing font source: ${f}" >&2
        exit 1
    fi
done

command -v lv_font_conv >/dev/null || { echo "lv_font_conv not on PATH" >&2; exit 1; }

mkdir -p "${OUT_DIR}"

# Exactly the non-ASCII characters that UI copy and Giac results can emit.
# An explicit list, not a Unicode block: 0x2200-0x22FF alone is 256 glyphs and
# tripled the face size for codepoints nothing renders.
MATH_SYMBOLS="0x2013,0x2014,0x2026,0x2032,0x2033,0x00B0,0x00B7,0x00B1,0x00B5,\
0x00B9,0x00B2,0x00B3,0x00BC,0x00BD,0x00BE,0x00D7,0x00F7,0x00F3,\
0x2070,0x2074,0x2075,0x2076,0x2077,0x2078,0x2079,0x207A,0x207B,0x207C,0x207D,0x207E,0x207F,\
0x2190,0x2191,0x2192,0x2193,\
0x2202,0x2205,0x2206,0x2207,0x2208,0x220F,0x2211,0x2212,0x2213,0x221A,0x221E,\
0x222B,0x222C,0x2248,0x2260,0x2261,0x2264,0x2265,\
0x03B1,0x03B2,0x03B3,0x03B4,0x03B5,0x03B8,0x03BB,0x03BC,0x03C0,0x03C3,0x03C6,0x03C9,\
0x0394,0x0398,0x039B,0x03A3,0x03A9,0x03A6"

gen_font() {
    local size="$1"
    local output="$2"
    lv_font_conv \
        --font "${LATIN}" --range 0x20-0x7E \
        --font "${MATH}"  --range 0x00B0-0x00B7 \
        --font "${MATH}"  --range 0x00D7 \
        --font "${MATH}"  --range 0x00F3 \
        --font "${MATH}"  --range 0x00F7 \
        --font "${MATH}"  --range 0x2013-0x2033 \
        --font "${MATH}"  --range 0x2070-0x207F \
        --font "${MATH}"  --range 0x2190-0x2193 \
        --size "${size}" \
        --bpp 4 \
        --format lvgl \
        --no-compress \
        --lv-include lvgl.h \
        -o "${output}"
    # lv_font_conv emits "lvgl/lvgl.h"; the repo includes plain lvgl.h
    sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|g' "${output}"
}

# The splash wordmark needs one size above the ladder, and only the 5 letters of
# "NumOS": ASCII only, so the maths ranges do not inflate it.
gen_wordmark() {
    local size="$1"
    local output="$2"
    # Exactly the five codepoints of "NumOS": 76 KB at full ASCII, 12 KB this way.
    lv_font_conv --font "${LATIN}" --symbols "0x4E,0x75,0x6D,0x4F,0x53" \
        --size "${size}" --bpp 4 --format lvgl --no-compress \
        --lv-include lvgl.h -o "${output}"
    sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|g' "${output}"
}
gen_wordmark 26 "${OUT_DIR}/lv_font_montserrat_26.c"
# 26px wordmark face for the splash logo: ASCII only, ~20 KB, and the only
# place a size above the ladder is allowed.
# The splash wordmark needs one size above the ladder, and only the 5 letters of
# "NumOS": ASCII only, so the maths ranges do not inflate it.
gen_wordmark() {
    local size="$1"
    local output="$2"
    # Exactly the five codepoints of "NumOS": 76 KB at full ASCII, 12 KB this way.
    lv_font_conv --font "${LATIN}" --symbols "0x4E,0x75,0x6D,0x4F,0x53" \
        --size "${size}" --bpp 4 --format lvgl --no-compress \
        --lv-include lvgl.h -o "${output}"
    sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|g' "${output}"
}
gen_wordmark 26 "${OUT_DIR}/lv_font_montserrat_26.c"
gen_font 11 "${OUT_DIR}/lv_font_montserrat_11.c"

python3 scripts/add_font_provenance_header.py montserrat "${OUT_DIR}/lv_font_montserrat_15.c"
python3 scripts/add_font_provenance_header.py montserrat "${OUT_DIR}/lv_font_montserrat_11.c"
python3 scripts/add_font_provenance_header.py montserrat "${OUT_DIR}/lv_font_montserrat_26.c"

# The 26px wordmark face must not keep LVGL's own symbol name. LVGL ships
# lv_font_montserrat_26 itself, so two definitions collide at link, and while
# lv_conf keeps LV_FONT_MONTSERRAT_26 at 0 this file's `#if LV_FONT_MONTSERRAT_26`
# guard would compile the whole font out. Rename the symbol and make the block
# unconditional so the repo owns the only definition.
python3 - "${OUT_DIR}/lv_font_montserrat_26.c" <<'RENAME_WORDMARK'
import sys
p = sys.argv[1]
s = open(p).read()
s = s.replace('lv_font_montserrat_26', 'lv_font_numos_wordmark_26')
s = s.replace('#if LV_FONT_MONTSERRAT_26\n',
              '#if 1 /* renamed to lv_font_numos_wordmark_26: LVGL ships its own 26px face under the old name */\n', 1)
s = s.replace('#endif /*#if LV_FONT_MONTSERRAT_26*/', '#endif /*repo-owned wordmark face*/')
s = s.replace('#ifndef LV_FONT_MONTSERRAT_26\n#define LV_FONT_MONTSERRAT_26 1\n#endif\n', '')
open(p, 'w').write(s)
RENAME_WORDMARK


echo "Generated:"
echo "  ${OUT_DIR}/lv_font_montserrat_15.c  $(wc -c < "${OUT_DIR}/lv_font_montserrat_15.c") bytes"
echo "  ${OUT_DIR}/lv_font_montserrat_11.c  $(wc -c < "${OUT_DIR}/lv_font_montserrat_11.c") bytes"
