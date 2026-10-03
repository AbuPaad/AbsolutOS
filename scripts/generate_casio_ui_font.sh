#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════════════════
# generate_casio_ui_font.sh — CASIO FX-9860GII UI face subsetting for LVGL / NumOS
# ═══════════════════════════════════════════════════════════════════════════════
#
# Generates LVGL-compatible .c font files from casio-fx-9860gii.ttf at
# three UI sizes: 12pt fontLcdSm, 18pt fontLcd, 26pt fontLcdLg (CASIO_SPEC §3b).
#
# Requirements:
#   npm i -g lv_font_conv
#
# Usage:
#   ./scripts/generate_casio_ui_font.sh [path-to-casio-fx-9860gii.ttf]
#   ./scripts/generate_casio_ui_font.sh [path-to-casio-fx-9860gii.ttf] [bpp]
#
#   CASIO_SIZE=12 ./scripts/generate_casio_ui_font.sh ...   # single size
#   CASIO_SIZE=all ./scripts/generate_casio_ui_font.sh ...   # all sizes (default)
#
# Examples:
#   ./scripts/generate_casio_ui_font.sh
#   ./scripts/generate_casio_ui_font.sh "C:/.../casio-fx-9860gii.ttf"
#   ./scripts/generate_casio_ui_font.sh "C:/.../casio-fx-9860gii.ttf" 4
#   CASIO_BPP_12=2 CASIO_SIZE=12 ./scripts/generate_casio_ui_font.sh ...
#
# Output (default):
#   src/fonts/casio_ui_12.c   — fontLcdSm  (12pt, bpp=2)
#   src/fonts/casio_ui_18.c   — fontLcd    (18pt, bpp=4)
#   src/fonts/casio_ui_26.c   — fontLcdLg  (26pt, bpp=4)
#
# Unicode coverage: plain ASCII 0x20-0x7F plus the twelve UI symbols
#   × ÷ ± ° √ π ← → ↑ ↓ ▲ ▼   (all confirmed present; NOTES.md §0b)
# ═══════════════════════════════════════════════════════════════════════════════
# Convert route: this face is TrueType ('glyf'), so no conversion is needed;
# lv_font_conv 1.5.3 reads it directly.
# ═══════════════════════════════════════════════════════════════════════════════

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONT_FILE="${1:-assets/fonts/casio-fx-9860gii.ttf}"

cd "${ROOT_DIR}"

if command -v python3 >/dev/null 2>&1; then
  PYTHON_CMD=python3
elif command -v python >/dev/null 2>&1; then
  PYTHON_CMD=python
else
  PYTHON_CMD=python.exe
fi

# ── Normalize Windows paths to POSIX (for bash under WSL/Git Bash/MSYS2) ─────
if [[ "${FONT_FILE}" =~ ^([A-Za-z]):[\\/](.*)$ ]]; then
  drive_letter="${BASH_REMATCH[1],,}"
  path_tail="${BASH_REMATCH[2]//\\//}"
  FONT_FILE="/mnt/${drive_letter}/${path_tail}"
fi

# ── Font size selection ──────────────────────────────────────────────────────
# Sizes mirror the XML's fontLcdSm 12 / fontLcd 18 / fontLcdLg 26 (CASIO_SPEC §3b).
SIZE_MODE="${CASIO_SIZE:-all}"

case "${SIZE_MODE}" in
  all)   SIZES_TO_GEN=(12 18 26) ;;
  12)    SIZES_TO_GEN=(12)       ;;
  18)    SIZES_TO_GEN=(18)       ;;
  26)    SIZES_TO_GEN=(26)       ;;
  *)
    echo "Invalid CASIO_SIZE '${SIZE_MODE}'. Allowed: 12, 18, 26, all" >&2
    exit 1
    ;;
esac

# ── BPP (bit-per-pixel) selection ────────────────────────────────────────────
# Defaults per spec: 12→2, 18→4, 26→4.
BPP_RAW="${2:-${CASIO_BPP:-4}}"
case "${BPP_RAW}" in
  1|2|3|4|8) ;;
  *)
    echo "Invalid BPP '${BPP_RAW}'. Allowed values: 1,2,3,4,8" >&2
    exit 1
    ;;
esac

# Per-size BPP overrides (fixed mapping; env knobs exist for regeneration)
declare -A SIZE_BPP
SIZE_BPP[12]="${CASIO_BPP_12:-2}"   # fontLcdSm default 2bpp for size
SIZE_BPP[18]="${CASIO_BPP_18:-4}"   # fontLcd default 4bpp
SIZE_BPP[26]="${CASIO_BPP_26:-4}"   # fontLcdLg default 4bpp

# ── Validate font exists ─────────────────────────────────────────────────────
if [[ ! -f "${FONT_FILE}" ]]; then
  echo "Missing font file: ${FONT_FILE}" >&2
  echo "Pass casio-fx-9860gii.ttf path as first argument." >&2
  exit 1
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Unicode Range Definitions — casio UI face (CASIO_SPEC §3b / NOTES §0b)
#
# range: plain ASCII 0x20-0x7F (numbers, letters, operators)
# symbols: ×÷±°√π←→↑↓▲▼ (all twelve confirmed present in the face)
# ═══════════════════════════════════════════════════════════════════════════════

UI_RANGE="0x20-0x7F"
UI_SYMBOLS="×÷±°√π←→↑↓▲▼"

ALL_RANGES="${UI_RANGE}"
SYMBOLS="${UI_SYMBOLS}"

# Keep only codepoints the face actually carries (lv_font_conv hard-fails on a
# range that yields zero characters). For this face everything above is present,
# so the filter is a no-op guard.
FILTERED="$("${PYTHON_CMD}" - "${FONT_FILE}" "${ALL_RANGES}" "${SYMBOLS}" <<'PYEOF'
import sys
from fontTools.ttLib import TTFont

cmap = set(TTFont(sys.argv[1]).getBestCmap())
kept_chunks = []
for chunk in sys.argv[2].split(','):
    chunk = chunk.strip()
    if '-' in chunk:
        a, b = chunk.split('-', 1)
        los = int(a, 16)
        his = int(b, 16)
        if any(cp in cmap for cp in range(los, his + 1)):
            kept_chunks.append(chunk)
    else:
        cp = int(chunk, 16)
        if cp in cmap:
            kept_chunks.append(chunk)
kept_symbols = ''.join(ch for ch in sys.argv[3] if ord(ch) in cmap)
print(','.join(kept_chunks))
print(kept_symbols)
PYEOF
)"
ALL_RANGES="$(printf '%s\n' "${FILTERED}" | sed -n '1p')"
SYMBOLS="$(printf '%s\n' "${FILTERED}" | sed -n '2p')"

# ═══════════════════════════════════════════════════════════════════════════════
# Generation function
# ═══════════════════════════════════════════════════════════════════════════════

generate_font() {
  local size="$1"
  local bpp="$2"
  local out_file="src/fonts/casio_ui_${size}.c"

  echo ""
  echo "═══ Generating CASIO FX-9860GII UI face ${size}pt (bpp=${bpp}) ═══"
  echo "  Output: ${out_file}"

  # Run lv_font_conv with combined ranges
  if lv_font_conv \
      --font "${FONT_FILE}" \
      --size "${size}" \
      --bpp "${bpp}" \
      --format lvgl \
      --range "${ALL_RANGES}" \
      --symbols "${SYMBOLS}" \
      --no-compress \
      -o "${out_file}"; then
    echo "  ✓ Conversion succeeded"
  else
    echo "  ✗ Conversion FAILED (exit code $?)" >&2
    return 1
  fi

  # Patch PlatformIO include path
  sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|g' "${out_file}"
  "${PYTHON_CMD}" scripts/add_font_provenance_header.py casio-ui "${out_file}"
  echo "  ✓ Include path patched for PlatformIO"

  # Report glyph count from generated file
  local glyph_count
  glyph_count=$(grep -c '\.bitmap' "${out_file}" 2>/dev/null || echo "?")
  echo "  Glyphs: ~${glyph_count}"

  # Report file size
  local file_size
  file_size=$(wc -c < "${out_file}" 2>/dev/null || echo "?")
  echo "  Size:   ${file_size} bytes"
}

# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║  CASIO Calculator Font → LVGL Font Generator for NumOS               ║"
echo "║  Font:  ${FONT_FILE}"
echo "║  Sizes: ${SIZES_TO_GEN[*]}"
echo "║  BPP:   ${BPP_RAW} (with per-size overrides)"
echo "╚═══════════════════════════════════════════════════════════════╝"

mkdir -p "${ROOT_DIR}/src/fonts"

for sz in "${SIZES_TO_GEN[@]}"; do
  generate_font "${sz}" "${SIZE_BPP[$sz]}"
done

echo ""
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║  Generation complete!                                        ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""
echo "Generated files:"
for sz in "${SIZES_TO_GEN[@]}"; do
  out="${ROOT_DIR}/src/fonts/casio_ui_${sz}.c"
  if [[ -f "${out}" ]]; then
    sz_kb=$(du -k "${out}" 2>/dev/null | cut -f1 || echo "?")
    echo "  src/fonts/casio_ui_${sz}.c  (${sz_kb} KB)"
  fi
done
echo ""
echo "Next steps:"
echo "1. Run:  pio run -e emulator_pc   (fonts are linked by build_src_filter)"
