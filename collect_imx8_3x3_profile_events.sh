#!/usr/bin/env bash
set -euo pipefail

# Collect the missing 3+3 taxonomy profile events for the current i.MX8MM
# paper run. This does not replace the p99 interference matrix. It only adds
# solo profile events needed to classify each complete TACLeBench victim.

REPEATS="${REPEATS:-1000}"
FREQ_KHZ="${FREQ_KHZ:-1600000}"
RUN_ROOT="${RUN_ROOT:-paper_runs/imx8mm/imx8mm_20260520_sat3_paper}"
OUT_DIR="${OUT_DIR:-$RUN_ROOT/profile_events_3x3}"
EVENTS="cycles,instructions,cache-misses,branch-instructions,branch-misses,dTLB-load-misses,iTLB-load-misses"

mkdir -p "$OUT_DIR/raw"

PAIR_CSV="$RUN_ROOT/crisp_data/e24_pair_additivity_summary.csv"
if [[ ! -f "$PAIR_CSV" ]]; then
  echo "missing $PAIR_CSV" >&2
  exit 2
fi

awk -F, 'NR>1 {print $2}' "$PAIR_CSV" | sort -u > "$OUT_DIR/complete_benches.txt"

SUMMARY="$OUT_DIR/profile_events_summary.csv"
echo "bench,event,count" > "$SUMMARY.tmp"

while IFS= read -r bench; do
  [[ -n "$bench" ]] || continue
  raw="$OUT_DIR/raw/${bench}__solo_perf.txt"
  if [[ "${RESUME:-0}" == "1" && -s "$raw.err" ]]; then
    echo "[profile] $bench (reuse)" >&2
    cat "$raw.out" "$raw.err" > "$raw"
    awk -F, -v bench="$bench" '
      NF >= 3 && $1 ~ /^[0-9.]+$/ {
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", $3);
        print bench "," $3 "," $1
      }' "$raw.err" >> "$SUMMARY.tmp"
    continue
  fi
  echo "[profile] $bench" >&2
  if [[ -n "${SUDO_PW:-}" ]]; then
    printf '%s\n' "$SUDO_PW" | sudo -S -- perf stat -x, -e "$EVENTS" -- \
      ./multi_proc_pmu -n "$REPEATS" -f "$FREQ_KHZ" "$bench" \
      >"$raw.out" 2>"$raw.err"
  else
    sudo -- perf stat -x, -e "$EVENTS" -- \
      ./multi_proc_pmu -n "$REPEATS" -f "$FREQ_KHZ" "$bench" \
      >"$raw.out" 2>"$raw.err"
  fi
  cat "$raw.out" "$raw.err" > "$raw"
  awk -F, -v bench="$bench" '
    NF >= 3 && $1 ~ /^[0-9.]+$/ {
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $3);
      print bench "," $3 "," $1
    }' "$raw.err" >> "$SUMMARY.tmp"
done < "$OUT_DIR/complete_benches.txt"

mv "$SUMMARY.tmp" "$SUMMARY"
{
  echo "run_root=$RUN_ROOT"
  echo "repeats=$REPEATS"
  echo "freq_khz=$FREQ_KHZ"
  echo "events=$EVENTS"
  echo "bench_count=$(wc -l < "$OUT_DIR/complete_benches.txt")"
  echo "generated_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$OUT_DIR/profile_events_manifest.txt"

echo "wrote $SUMMARY"
