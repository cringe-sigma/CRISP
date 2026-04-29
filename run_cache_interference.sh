#!/usr/bin/env bash
# Run a list of benches solo and under CACHE-bench co-runner interference,
# then print the slowdown ratio (cycles_pair / cycles_solo) per bench.
#
# Usage:
#   sudo ./run_cache_interference.sh [-n SAMPLES] [-f FREQ_KHZ]
set -euo pipefail

SAMPLES=20
FREQ_OPT=()
while getopts "n:f:" opt; do
    case "$opt" in
        n) SAMPLES="$OPTARG" ;;
        f) FREQ_OPT=( -f "$OPTARG" ) ;;
        *) echo "usage: $0 [-n SAMPLES] [-f FREQ_KHZ]" >&2; exit 1 ;;
    esac
done

BENCHES=(adpcm_dec binarysearch fir2dim fmref huff_dec iir insertsort)
INTERFERER=CACHE
BIN=./multi_proc_pmu
OUTDIR=results
mkdir -p "$OUTDIR"

# Extract the median 'cycles' value from a multi_proc_pmu output file.
extract_median_cycles() {
    awk '/^  cycles / { print $5 }' "$1"
}

printf '%-14s %18s %18s %10s\n' "bench" "solo_median_cyc" "pair_median_cyc" "ratio"
printf '%-14s %18s %18s %10s\n' "-----" "---------------" "---------------" "-----"

for b in "${BENCHES[@]}"; do
    solo_log="$OUTDIR/solo_${b}.txt"
    pair_log="$OUTDIR/pair_${b}_vs_${INTERFERER}.txt"

    "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$b"            >"$solo_log" 2>&1 || true
    "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$b" "$INTERFERER" >"$pair_log" 2>&1 || true

    solo=$(extract_median_cycles "$solo_log")
    pair=$(extract_median_cycles "$pair_log")
    [[ -z "$solo" ]] && solo="N/A"
    [[ -z "$pair" ]] && pair="N/A"
    ratio=$(awk -v s="$solo" -v p="$pair" 'BEGIN{ if (s+0>0 && p+0>0) printf "%.4f", p/s; else print "N/A"; }')

    printf '%-14s %18s %18s %10s\n' "$b" "$solo" "$pair" "$ratio"
done
