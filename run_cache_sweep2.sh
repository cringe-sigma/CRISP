#!/usr/bin/env bash
# Extended CACHE-bench parameter sweep, focused on the regimes that the
# first sweep flagged as the most disruptive:
#   * write-heavy traffic (low CACHE_RW_RATIO)
#   * stride that walks one access per cache line
#   * working set near LLC / DRAM
#   * forward-vs-reverse split
#
# huff_dec is excluded (not safe to re-init within one process).
#
# Usage: sudo ./run_cache_sweep2.sh [-n SAMPLES] [-f FREQ_KHZ]
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

VICTIMS=(adpcm_dec binarysearch fir2dim fmref iir insertsort)
BIN=./multi_proc_pmu
OUTDIR=results/cache_sweep2
mkdir -p "$OUTDIR"

# Each entry: "tag | -DCACHE_MEM_SIZE=... -DCACHE_STRIDE=... -DCACHE_RW_RATIO=... -DCACHE_FR_RATIO=..."
CONFIGS=(
  # --- pure-write sweep (rw_ratio = read%, so 0 = all writes) ----------------
  "wrPure_LLC4M_s16        | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=16  -DCACHE_RW_RATIO=0   -DCACHE_FR_RATIO=50"
  "wr95_LLC4M_s16          | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=16  -DCACHE_RW_RATIO=5   -DCACHE_FR_RATIO=50"
  "wr80_LLC4M_s16          | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=16  -DCACHE_RW_RATIO=20  -DCACHE_FR_RATIO=50"
  "wrPure_LLC2M_s16        | -DCACHE_MEM_SIZE=2097152  -DCACHE_STRIDE=16  -DCACHE_RW_RATIO=0   -DCACHE_FR_RATIO=50"
  "wrPure_DRAM16M_s16      | -DCACHE_MEM_SIZE=16777216 -DCACHE_STRIDE=16  -DCACHE_RW_RATIO=0   -DCACHE_FR_RATIO=50"

  # --- stride sweep at write-heavy LLC working set ---------------------------
  "wr10_LLC4M_s8           | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=8   -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_LLC4M_s32          | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=32  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_LLC4M_s64          | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_LLC4M_s128         | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=128 -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"

  # --- forward/reverse split sweep at the worst point above ------------------
  "wr10_LLC4M_s64_fr10     | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=10"
  "wr10_LLC4M_s64_fr90     | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=90"
  "wr10_LLC4M_s64_fr0      | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=0"

  # --- working-set sweep at write-heavy stride=64 ----------------------------
  "wr10_L2_256K_s64        | -DCACHE_MEM_SIZE=262144   -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_L2_512K_s64        | -DCACHE_MEM_SIZE=524288   -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_LLC1M_s64          | -DCACHE_MEM_SIZE=1048576  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_LLC8M_s64          | -DCACHE_MEM_SIZE=8388608  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_DRAM16M_s64        | -DCACHE_MEM_SIZE=16777216 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
  "wr10_DRAM64M_s64        | -DCACHE_MEM_SIZE=67108864 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=10  -DCACHE_FR_RATIO=50"
)

DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'

extract_median_cycles() {
    awk '/^  cycles / { print $5 }' "$1"
}

# ---- Solo baselines (CACHE config doesn't matter for solo runs) ----
echo "== Building default binary for solo baselines =="
make -s >/dev/null
declare -A SOLO_MED
for v in "${VICTIMS[@]}"; do
    log="$OUTDIR/solo_${v}.txt"
    "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$v" >"$log" 2>&1
    SOLO_MED["$v"]=$(extract_median_cycles "$log")
done

printf '\n# Solo baselines (cycles, median over %d samples)\n' "$SAMPLES"
for v in "${VICTIMS[@]}"; do
    printf '  %-14s %18s\n' "$v" "${SOLO_MED[$v]}"
done

# ---- Summary table accumulator (CSV) ----
SUMMARY="$OUTDIR/summary.csv"
{
    printf 'config'
    for v in "${VICTIMS[@]}"; do printf ',%s' "$v"; done
    printf '\n'
} >"$SUMMARY"

# ---- Per-config rebuild + measurements ----
for entry in "${CONFIGS[@]}"; do
    tag="${entry%%|*}"; tag="${tag## }"; tag="${tag%% }"
    defs="${entry#*|}"; defs="${defs## }"; defs="${defs%% }"

    echo
    echo "================================================================"
    echo "== CACHE config: $tag"
    echo "==   defines:  $defs"
    echo "================================================================"

    rm -f build/CACHE/CACHE.o build/CACHE.o build/CACHE.o.tmp multi_proc_pmu
    make -s CFLAGS="$DEFAULT_CFLAGS $defs" >/dev/null

    printf '%-14s %18s %18s %10s\n' "victim" "solo_cyc(median)" "pair_cyc(median)" "ratio"
    printf '%-14s %18s %18s %10s\n' "------" "----------------" "----------------" "-----"

    csv_line="$tag"
    for v in "${VICTIMS[@]}"; do
        pair_log="$OUTDIR/pair_${v}_vs_CACHE_${tag}.txt"
        "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$v" CACHE >"$pair_log" 2>&1 || true
        pair=$(extract_median_cycles "$pair_log")
        [[ -z "$pair" ]] && pair="N/A"
        solo="${SOLO_MED[$v]}"
        ratio=$(awk -v s="$solo" -v p="$pair" \
                'BEGIN{ if (s+0>0 && p+0>0) printf "%.4f", p/s; else print "N/A"; }')
        printf '%-14s %18s %18s %10s\n' "$v" "$solo" "$pair" "$ratio"
        csv_line+=",$ratio"
    done
    echo "$csv_line" >>"$SUMMARY"
done

# ---- Restore default config ----
echo
echo "== Restoring default CACHE configuration =="
rm -f build/CACHE/CACHE.o build/CACHE.o build/CACHE.o.tmp multi_proc_pmu
make -s >/dev/null

echo
echo "== Combined slowdown ratio table =="
column -t -s, "$SUMMARY"
echo
echo "Per-run logs: $OUTDIR/  ;  CSV: $SUMMARY"
