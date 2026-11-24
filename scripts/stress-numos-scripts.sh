#!/usr/bin/env bash
# Stress-run every .numos emulator script headless+deterministic and record
# exit code + crash markers. Evidence only: no fixes.
set -u
 cd "$(dirname "$0")/.." || exit 1
BIN=./.pio/build/emulator_pc/program
LOGDIR=/tmp/numos-stress
mkdir -p "$LOGDIR" out
: > "$LOGDIR/results.tsv"

run_one() {
  local s="$1"
  local base
  base=$(basename "$s" .numos)
  local log="$LOGDIR/$base.log"
  local t0 t1 rc
  t0=$(date +%s)
  timeout 120 "$BIN" --headless --deterministic --script "$s" --frames 1600 --quiet \
      > "$log" 2>&1
  rc=$?
  t1=$(date +%s)
  local verdict="ok"
  [ "$rc" -eq 124 ] && verdict="HANG(timeout)"
  [ "$rc" -eq 4 ]   && verdict="ASSERT_FAIL"
  [ "$rc" -eq 2 ]   && verdict="PARSE_ERR"
  [ "$rc" -eq 139 ] && verdict="SEGV"
  [ "$rc" -eq 134 ] && verdict="ABORT"
  if grep -qiE "segmentation|assertion .* failed|stack smashing|corrupt|Guru Meditation|terminate called" "$log"; then
    verdict="$verdict+CRASHMARK"
  fi
  printf "%s\t%s\t%s\t%s\n" "$verdict" "$rc" "$((t1-t0))s" "$base" >> "$LOGDIR/results.tsv"
}
export -f run_one
export BIN LOGDIR

ls tests/emulator/scripts/*.numos | xargs -P 4 -I{} bash -c 'run_one "$@"' _ {}

echo "=== non-ok ==="
awk -F'\t' '$1!="ok" || $2!=0' "$LOGDIR/results.tsv" | sort
echo "=== counts ==="
cut -f1 "$LOGDIR/results.tsv" | sort | uniq -c | sort -rn
echo "=== total: $(wc -l < "$LOGDIR/results.tsv") scripts ==="
