#!/usr/bin/env bash
# Mixed TIM-bench attack sweep.
#
# Strategy: build the harness ONCE with aggressive parameters for every
# TIM attacker (CACHE / BUS / MEM / POINTER / PIPELINE), then enumerate
# attack mixes by varying only the positional background list.  This way
# each run only differs in which attackers (and how many copies) are
# scheduled on cpu1..cpuN.
#
# Excludes huff_dec.
#
# Usage: sudo ./run_tim_mix.sh [-n SAMPLES] [-f FREQ_KHZ]

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
OUTDIR=results/tim_mix
mkdir -p "$OUTDIR"

# ---- Aggressive defines, one set used for all runs ----
# CACHE: best single-core (1MB, stride 64, 100% writes)
CACHE_DEFS='-DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=0 -DCACHE_FR_RATIO=50 -DCACHE_ITER=1'
# BUS: 4 MB, CPU-op 100%, 3:3:3 directions, uint32
BUS_DEFS='-DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4'
# MEM: large 8 MB pure memset firehose, 4 KB pages
MEM_DEFS='-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE=4096 -DMEM_OP_RATIO=2521 -DMEM_ITER=4'
# POINTER: long dep chain, 64K nodes, prime-ish stride, 100% loads
POINTER_DEFS='-DPOINTER_ELEMENTS=65536u -DPOINTER_STRIDE=17u -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384 -DPOINTER_CACHE_LINE=64'
# PIPELINE: heavy FP ratio, more iterations
PIPELINE_DEFS='-DPIPELINE_RATIO=100 -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=8192'

ALL_DEFS="$CACHE_DEFS $BUS_DEFS $MEM_DEFS $POINTER_DEFS $PIPELINE_DEFS"
DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'

extract_median_cycles() { awk '/^  cycles / { print $5 }' "$1"; }

# ---- One build for everything ----
echo "== Building harness with aggressive TIM params =="
rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/MEM/MEM.o build/POINTER/POINTER.o build/PIPELINE/PIPELINE.o
rm -f build/CACHE.o build/BUS.o build/MEM.o build/POINTER.o build/PIPELINE.o multi_proc_pmu
make -s CFLAGS="$DEFAULT_CFLAGS $ALL_DEFS" >/dev/null

# ---- Solo victim baselines (no attackers) ----
echo "== Measuring solo baselines =="
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

# ---- Mix list ----
# Each entry: tag | space-separated bg bench names
# Cores: bg list length determines how many extra cores are used.
MIXES=(
  # 1-core single attackers
  "S_CACHE        | CACHE"
  "S_BUS          | BUS"
  "S_MEM          | MEM"
  "S_POINTER      | POINTER"
  "S_PIPELINE     | PIPELINE"

  # 2-core mixes: every pair of distinct attackers (C(5,2)=10)
  "P_CACHE_BUS    | CACHE BUS"
  "P_CACHE_MEM    | CACHE MEM"
  "P_CACHE_POINT  | CACHE POINTER"
  "P_CACHE_PIPE   | CACHE PIPELINE"
  "P_BUS_MEM      | BUS MEM"
  "P_BUS_POINT    | BUS POINTER"
  "P_BUS_PIPE     | BUS PIPELINE"
  "P_MEM_POINT    | MEM POINTER"
  "P_MEM_PIPE     | MEM PIPELINE"
  "P_POINT_PIPE   | POINTER PIPELINE"

  # 2-core homogeneous (same attacker x2)
  "P_CACHE_x2     | CACHE CACHE"
  "P_BUS_x2       | BUS BUS"
  "P_MEM_x2       | MEM MEM"

  # 3-core mixes (use all 3 remaining cores)
  "T_CACHE_BUS_MEM    | CACHE BUS MEM"
  "T_CACHE_BUS_POINT  | CACHE BUS POINTER"
  "T_CACHE_BUS_PIPE   | CACHE BUS PIPELINE"
  "T_CACHE_MEM_POINT  | CACHE MEM POINTER"
  "T_CACHE_MEM_PIPE   | CACHE MEM PIPELINE"
  "T_CACHE_POINT_PIPE | CACHE POINTER PIPELINE"
  "T_BUS_MEM_POINT    | BUS MEM POINTER"
  "T_BUS_MEM_PIPE     | BUS MEM PIPELINE"
  "T_BUS_POINT_PIPE   | BUS POINTER PIPELINE"
  "T_MEM_POINT_PIPE   | MEM POINTER PIPELINE"

  # 3-core: 2x best+1
  "T_CACHE_x2_BUS     | CACHE CACHE BUS"
  "T_CACHE_x2_MEM     | CACHE CACHE MEM"
  "T_CACHE_x2_POINT   | CACHE CACHE POINTER"
  "T_CACHE_x2_PIPE    | CACHE CACHE PIPELINE"
  "T_BUS_x2_CACHE     | BUS BUS CACHE"
  "T_MEM_x2_CACHE     | MEM MEM CACHE"
  "T_BUS_x2_MEM       | BUS BUS MEM"

  # 3-core: triples of one type
  "T_CACHE_x3         | CACHE CACHE CACHE"
  "T_BUS_x3           | BUS BUS BUS"
  "T_MEM_x3           | MEM MEM MEM"
)

SUMMARY="$OUTDIR/summary.csv"
{
    printf 'config'
    for v in "${VICTIMS[@]}"; do printf ',%s' "$v"; done
    printf '\n'
} >"$SUMMARY"

run_mix() {
    local tag="$1"; shift
    local bg_args=( "$@" )

    echo
    echo "================================================================"
    echo "== $tag    bg=[${bg_args[*]}]"
    echo "================================================================"
    printf '%-14s %18s %18s %10s\n' "victim" "solo_cyc(med)" "pair_cyc(med)" "ratio"
    printf '%-14s %18s %18s %10s\n' "------" "-------------" "-------------" "-----"

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

for entry in "${MIXES[@]}"; do
    IFS='|' read -r tag bg <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    bg="$(echo -n "$bg" | xargs)"
    # shellcheck disable=SC2206
    bg_args=( $bg )
    run_mix "$tag" "${bg_args[@]}"
done

echo
echo "== Restoring default configuration =="
rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/MEM/MEM.o build/POINTER/POINTER.o build/PIPELINE/PIPELINE.o
rm -f build/CACHE.o build/BUS.o build/MEM.o build/POINTER.o build/PIPELINE.o multi_proc_pmu
make -s >/dev/null

echo
echo "== Combined slowdown ratio table =="
column -t -s, "$SUMMARY"

echo
echo "== Worst attack per victim =="
python3 - <<'PY'
import csv
with open("results/tim_mix/summary.csv") as f:
    rows = list(csv.reader(f))
hdr = rows[0]
victims = hdr[1:]
worst = {v: (None, -1.0) for v in victims}
for r in rows[1:]:
    cfg = r[0]
    for v, val in zip(victims, r[1:]):
        try:
            x = float(val)
        except ValueError:
            continue
        if x > worst[v][1]:
            worst[v] = (cfg, x)
print(f"{'victim':<14} {'best_attack':<22} {'ratio':>8}")
print("-" * 46)
for v in victims:
    cfg, x = worst[v]
    print(f"{v:<14} {cfg:<22} {x:>8.4f}")
PY

echo
echo "Per-run logs: $OUTDIR/  ;  CSV: $SUMMARY"
