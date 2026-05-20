#!/usr/bin/env bash
set -euo pipefail

RUN_ROOT="${RUN_ROOT:-paper_runs/imx8mm/imx8mm_20260520_sat3_paper}"
OUT="$RUN_ROOT/profile_events_3x3"
PAIR="$RUN_ROOT/crisp_data/e24_pair_additivity_summary.csv"

tmp="$OUT/profile_events_summary.csv.tmp"
echo "bench,event,count" > "$tmp"

awk -F, 'NR>1 {print $2}' "$PAIR" | sort -u | while IFS= read -r bench; do
  err="$OUT/raw/${bench}__solo_perf.txt.err"
  if [[ ! -f "$err" ]]; then
    echo "missing $err" >&2
    exit 2
  fi
  awk -F, -v bench="$bench" '
    NF >= 3 && $1 ~ /^[0-9.]+$/ {
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $3);
      print bench "," $3 "," $1
    }' "$err" >> "$tmp"
done

mv "$tmp" "$OUT/profile_events_summary.csv"
wc -l "$OUT/profile_events_summary.csv"
cut -d, -f1 "$OUT/profile_events_summary.csv" | tail -n +2 | sort | uniq -c | sort -n | tail -5
cut -d, -f2 "$OUT/profile_events_summary.csv" | tail -n +2 | sort | uniq -c
