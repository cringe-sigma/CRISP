#!/usr/bin/env bash
# CACHE + BUS combined attack sweep.
#
# Strategy:
#   1. Probe BUS-only configs to learn its strongest tunings.
#   2. Combine the strongest single-core CACHE attack (wr0_1M_s64) with
#      various BUS tunings, on different core counts.
#   3. Try multi-CACHE + BUS combos.
#
# Excludes huff_dec.
#
# Usage: sudo ./run_cache_bus_combo.sh [-n SAMPLES] [-f FREQ_KHZ]

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
OUTDIR=results/cache_bus
mkdir -p "$OUTDIR"

# Best single-core CACHE config from previous sweep.
CACHE_BEST='-DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=0 -DCACHE_FR_RATIO=50'
# Variant tuned for iir/insertsort (rw=10) where multi-core CACHE worked best.
CACHE_RW10='-DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=10 -DCACHE_FR_RATIO=50'

# Format: tag | -D defines (CACHE+BUS) | bg bench list
# Three families:
#   bus_only_*   : 1 BUS on cpu1
#   cb_*         : 1 CACHE + 1 BUS
#   ccb_/cbb_    : 2+1 multi-core
CONFIGS=(
  # ----- BUS-only baselines -----
  "bus_def              | -DBUS_SIZE_MB=1   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=50  -DBUS_ITER=4  | BUS"
  "bus_4M_cpu100        | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4  | BUS"
  "bus_4M_cpu0          | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=0   -DBUS_ITER=4  | BUS"
  "bus_1M_u8_cpu100     | -DBUS_SIZE_MB=1   -DBUS_DATA_TYPE=1 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4  | BUS"
  "bus_1M_u64_cpu100    | -DBUS_SIZE_MB=1   -DBUS_DATA_TYPE=7 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4  | BUS"
  "bus_1M_self_cpu100   | -DBUS_SIZE_MB=1   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=001 -DBUS_CPU_RATIO=100 -DBUS_ITER=8  | BUS"
  "bus_8M_cpu100        | -DBUS_SIZE_MB=8   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4  | BUS"

  # ----- CACHE_best (cpu1) + BUS (cpu2) -----
  "C_wr0_1M__bus_def    | $CACHE_BEST  -DBUS_SIZE_MB=1 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=50  -DBUS_ITER=4 | CACHE BUS"
  "C_wr0_1M__bus_4M100  | $CACHE_BEST  -DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE BUS"
  "C_wr0_1M__bus_4M0    | $CACHE_BEST  -DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=0   -DBUS_ITER=4 | CACHE BUS"
  "C_wr0_1M__bus_8M100  | $CACHE_BEST  -DBUS_SIZE_MB=8 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE BUS"
  "C_wr0_1M__bus_1M_u8  | $CACHE_BEST  -DBUS_SIZE_MB=1 -DBUS_DATA_TYPE=1 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE BUS"
  "C_wr0_1M__bus_self   | $CACHE_BEST  -DBUS_SIZE_MB=1 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=001 -DBUS_CPU_RATIO=100 -DBUS_ITER=8 | CACHE BUS"

  # ----- 2x CACHE (cpu1+cpu2) + BUS (cpu3) -----
  "CC_wr10_1M__bus_4M100 | $CACHE_RW10 -DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE CACHE BUS"
  "CC_wr0_1M__bus_4M100  | $CACHE_BEST -DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE CACHE BUS"
  "CC_wr0_1M__bus_8M100  | $CACHE_BEST -DBUS_SIZE_MB=8 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE CACHE BUS"

  # ----- CACHE (cpu1) + 2x BUS (cpu2+cpu3) -----
  "C_wr0_1M__bus_4M100_x2 | $CACHE_BEST -DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE BUS BUS"
  "C_wr0_1M__bus_8M100_x2 | $CACHE_BEST -DBUS_SIZE_MB=8 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | CACHE BUS BUS"

  # ----- 3x BUS only (no CACHE) for reference -----
  "bus_4M100_x3          | -DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4 | BUS BUS BUS"
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

    rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/CACHE.o build/BUS.o multi_proc_pmu
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

for entry in "${CONFIGS[@]}"; do
    IFS='|' read -r tag defs bg <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    defs="$(echo -n "$defs" | xargs)"
    bg="$(echo -n "$bg" | xargs)"
    # shellcheck disable=SC2206
    bg_args=( $bg )
    run_one "$tag" "$defs" "${bg_args[@]}"
done

echo
echo "== Restoring default configuration =="
rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/CACHE.o build/BUS.o multi_proc_pmu
make -s >/dev/null

echo
echo "== Combined slowdown ratio table =="
column -t -s, "$SUMMARY"
echo
echo "Per-run logs: $OUTDIR/  ;  CSV: $SUMMARY"
