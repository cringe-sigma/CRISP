#!/usr/bin/env bash
# Sweep several CACHE-bench parameter configurations and, for each one,
# measure the slowdown ratio (cycles_pair / cycles_solo) of a fixed list
# of victim benches when CACHE runs as a co-runner on cpu1.
#
# huff_dec is intentionally excluded because the upstream TACLeBench
# huff_dec kernel is not safe to re-init/_main inside the same process.
#
# Usage:
#   sudo ./run_cache_sweep.sh [-n SAMPLES] [-f FREQ_KHZ]
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
OUTDIR=results/cache_sweep
mkdir -p "$OUTDIR"

# Each config is "tag|extra-defines".
# Knobs: CACHE_MEM_SIZE (bytes) / CACHE_STRIDE / CACHE_RW_RATIO / CACHE_FR_RATIO
CONFIGS=(
  "L1_16K_s4_rw50            | -DCACHE_MEM_SIZE=16384       -DCACHE_STRIDE=4  -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50"
  "L2_256K_s16_rw50_default  | -DCACHE_MEM_SIZE=262144      -DCACHE_STRIDE=16 -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50"
  "LLC_4M_s16_rw50           | -DCACHE_MEM_SIZE=4194304     -DCACHE_STRIDE=16 -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50"
  "DRAM_32M_s16_rw50         | -DCACHE_MEM_SIZE=33554432    -DCACHE_STRIDE=16 -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50"
  "LLC_4M_stride64_rw50      | -DCACHE_MEM_SIZE=4194304     -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50"
  "LLC_4M_s16_read90         | -DCACHE_MEM_SIZE=4194304     -DCACHE_STRIDE=16 -DCACHE_RW_RATIO=90 -DCACHE_FR_RATIO=50"
  "LLC_4M_s16_write90        | -DCACHE_MEM_SIZE=4194304     -DCACHE_STRIDE=16 -DCACHE_RW_RATIO=10 -DCACHE_FR_RATIO=50"
)

DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'

extract_median_cycles() {
    awk '/^  cycles / { print $5 }' "$1"
}

# 1) Build solo baselines once (CACHE config doesn't matter for solo runs).
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

# 2) For each CACHE configuration, rebuild only build/CACHE.o + binary,
#    then run pair tests.
for entry in "${CONFIGS[@]}"; do
    tag="${entry%%|*}"
    defs="${entry#*|}"
    tag="${tag## }"; tag="${tag%% }"
    defs="${defs## }"; defs="${defs%% }"

    echo
    echo "================================================================"
    echo "== CACHE config: $tag"
    echo "==   defines:  $defs"
    echo "================================================================"

    rm -f build/CACHE/CACHE.o build/CACHE.o build/CACHE.o.tmp multi_proc_pmu
    make -s CFLAGS="$DEFAULT_CFLAGS $defs" >/dev/null

    printf '%-14s %18s %18s %10s\n' "victim" "solo_cyc(median)" "pair_cyc(median)" "ratio"
    printf '%-14s %18s %18s %10s\n' "------" "----------------" "----------------" "-----"
    for v in "${VICTIMS[@]}"; do
        pair_log="$OUTDIR/pair_${v}_vs_CACHE_${tag}.txt"
        "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$v" CACHE >"$pair_log" 2>&1 || true
        pair=$(extract_median_cycles "$pair_log")
        [[ -z "$pair" ]] && pair="N/A"
        solo="${SOLO_MED[$v]}"
        ratio=$(awk -v s="$solo" -v p="$pair" \
                'BEGIN{ if (s+0>0 && p+0>0) printf "%.4f", p/s; else print "N/A"; }')
        printf '%-14s %18s %18s %10s\n' "$v" "$solo" "$pair" "$ratio"
    done
done

# 3) Restore the binary to the default CACHE configuration.
echo
echo "== Restoring default CACHE configuration =="
rm -f build/CACHE/CACHE.o build/CACHE.o build/CACHE.o.tmp multi_proc_pmu
make -s >/dev/null
echo "Done. Per-run logs are in $OUTDIR/."
