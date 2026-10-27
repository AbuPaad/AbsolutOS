#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

# Generate the Montserrat UI type ladder (15 / 11 / 9 px) used by the
# numos theme after the 75% shrink. Requires: lv_font_conv on PATH.
#
# Output:
#   src/fonts/lv_font_montserrat_15.c
#   src/fonts/lv_font_montserrat_11.c
#   src/fonts/lv_font_montserrat_9.c

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONT_FILE="assets/fonts/Montserrat-Regular.ttf"
OUT_DIR="src/fonts"

cd "${ROOT_DIR}"

if [[ ! -f "${FONT_FILE}" ]]; then
    echo "Missing font file: ${FONT_FILE}" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"

gen_font() {
    local size="$1"
    local output="$2"
    lv_font_conv \
        --font "${FONT_FILE}" \
        --size "${size}" \
        --bpp 4 \
        --format lvgl \
        --no-compress \
        --lv-include lvgl.h \
        --range 0x20-0x7F \
        -o "${output}"
}

gen_font 15 "${OUT_DIR}/lv_font_montserrat_15.c"
gen_font 11 "${OUT_DIR}/lv_font_montserrat_11.c"
gen_font  9 "${OUT_DIR}/lv_font_montserrat_9.c"

python3 scripts/add_font_provenance_header.py montserrat "${OUT_DIR}/lv_font_montserrat_15.c"
python3 scripts/add_font_provenance_header.py montserrat "${OUT_DIR}/lv_font_montserrat_11.c"
python3 scripts/add_font_provenance_header.py montserrat "${OUT_DIR}/lv_font_montserrat_9.c"

echo "Generated:"
echo "  ${OUT_DIR}/lv_font_montserrat_15.c"
echo "  ${OUT_DIR}/lv_font_montserrat_11.c"
echo "  ${OUT_DIR}/lv_font_montserrat_9.c"