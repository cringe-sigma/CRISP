#!/usr/bin/env bash
# Aggressive CACHE attack sweep, derived from sweep2 results.
# Focus regions:
#   * pure-write near the 1MB working-set sweet spot (worst on binarysearch)
#   * tiny stride for max write bandwidth / write-buffer pressure
#   * fr=0 forward-only
#   * multi-core attacks (CACHE on cpu1, cpu1+2, cpu1+2+3) with the
#     single-core best config and a few variants
#
# Excludes huff_dec.
#
# Usage: sudo ./run_cache_aggressive.sh [-n SAMPLES] [-f FREQ_KHZ]
set -euo pipefail

SAMPLES=100
FREQ_OPT=( -f 1600000 )
while getopts "n:f:" opt; do
    case "$opt" in
        n) SAMPLES="$OPTARG" ;;
        f) FREQ_OPT=( -f "$OPTARG" ) ;;
        *) echo "usage: $0 [-n SAMPLES] [-f FREQ_KHZ]" >&2; exit 1 ;;
    esac
done

VICTIMS=(adpcm_dec binarysearch fir2dim fmref iir insertsort)
BIN=./multi_proc_pmu
OUTDIR=results/cache_aggr
mkdir -p "$OUTDIR"

# Single-core (1 CACHE worker on cpu1) configs.
SINGLE_CONFIGS=(
  # Around the sweet spot: 1MB working set, stride=64, varying write %
  "wr0_1M_s64           | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr5_1M_s64           | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=5  -DCACHE_FR_RATIO=50"
  "wr0_1M_s64_fr0       | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=0"
  "wr0_1M_s32           | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=32  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_1M_s128          | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=128 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_1M_s256          | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=256 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_512K_s64         | -DCACHE_MEM_SIZE=524288  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_768K_s64         | -DCACHE_MEM_SIZE=786432  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_1.5M_s64         | -DCACHE_MEM_SIZE=1572864 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_2M_s64           | -DCACHE_MEM_SIZE=2097152 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"

  # Stride=1: every element, max touched lines per second
  "wr0_64K_s1           | -DCACHE_MEM_SIZE=65536   -DCACHE_STRIDE=1   -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_256K_s1          | -DCACHE_MEM_SIZE=262144  -DCACHE_STRIDE=1   -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_1M_s1            | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=1   -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"

  # TLB-busting: 4KB stride at large working set, write-heavy
  "wr0_16M_s1024_4K     | -DCACHE_MEM_SIZE=16777216 -DCACHE_STRIDE=1024 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
  "wr0_64M_s1024_4K     | -DCACHE_MEM_SIZE=67108864 -DCACHE_STRIDE=1024 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50"
)

# Multi-core attacks: re-use the best single-core config, vary number of CACHE workers.
# Format: tag | -D... defines | extra positional bg benches
MULTI_CONFIGS=(
  "wr10_1M_s64_x2 | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=10 -DCACHE_FR_RATIO=50 | CACHE CACHE"
  "wr10_1M_s64_x3 | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=10 -DCACHE_FR_RATIO=50 | CACHE CACHE CACHE"
  "wr0_1M_s64_x2  | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 | CACHE CACHE"
  "wr0_1M_s64_x3  | -DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 | CACHE CACHE CACHE"
)

DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'

extract_median_cycles() {
    awk '/^  cycles / { print $5 }' "$1"
}

# ---- Solo baselines ----
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

SUMMARY="$OUTDIR/summary.csv"
{
    printf 'config'
    for v in "${VICTIMS[@]}"; do printf ',%s' "$v"; done
    printf '\n'
} >"$SUMMARY"

run_one() {
    local tag="$1"; shift
    local defs="$1"; shift
    local bg_args=( "$@" )

    rm -f build/CACHE/CACHE.o build/CACHE.o build/CACHE.o.tmp multi_proc_pmu
    make -s CFLAGS="$DEFAULT_CFLAGS $defs" >/dev/null

    echo
    echo "================================================================"
    echo "== $tag    bg=[${bg_args[*]}]"
    echo "==   defines:  $defs"
    echo "================================================================"
    printf '%-14s %18s %18s %10s\n' "victim" "solo_cyc(median)" "pair_cyc(median)" "ratio"
    printf '%-14s %18s %18s %10s\n' "------" "----------------" "----------------" "-----"

    local csv_line="$tag"
    for v in "${VICTIMS[@]}"; do
        pair_log="$OUTDIR/pair_${v}_vs_${tag}.txt"
        "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$v" "${bg_args[@]}" \
            >"$pair_log" 2>&1 || true
        pair=$(extract_median_cycles "$pair_log")
        [[ -z "$pair" ]] && pair="N/A"
        local solo="${SOLO_MED[$v]}"
        ratio=$(awk -v s="$solo" -v p="$pair" \
                'BEGIN{ if (s+0>0 && p+0>0) printf "%.4f", p/s; else print "N/A"; }')
        printf '%-14s %18s %18s %10s\n' "$v" "$solo" "$pair" "$ratio"
        csv_line+=",$ratio"
    done
    echo "$csv_line" >>"$SUMMARY"
}

# ---- Single-core attack sweep ----
for entry in "${SINGLE_CONFIGS[@]}"; do
    tag="${entry%%|*}";  tag="${tag## }"; tag="${tag%% }"
    defs="${entry#*|}";  defs="${defs## }"; defs="${defs%% }"
    run_one "$tag" "$defs" CACHE
done

# ---- Multi-core attack sweep ----
for entry in "${MULTI_CONFIGS[@]}"; do
    IFS='|' read -r tag defs bg <<<"$entry"
    tag="${tag## }"; tag="${tag%% }"
    defs="${defs## }"; defs="${defs%% }"
    bg="${bg## }";   bg="${bg%% }"
    # shellcheck disable=SC2206
    bg_args=( $bg )
    run_one "$tag" "$defs" "${bg_args[@]}"
done

# ---- Restore default ----
echo
echo "== Restoring default CACHE configuration =="
rm -f build/CACHE/CACHE.o build/CACHE.o build/CACHE.o.tmp multi_proc_pmu
make -s >/dev/null

echo
echo "== Combined slowdown ratio table =="
column -t -s, "$SUMMARY"
echo
echo "Per-run logs: $OUTDIR/  ;  CSV: $SUMMARY"
