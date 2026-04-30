#!/usr/bin/env bash
# Deeper TIM mix sweep, focused around the new champion (MEM).
#
# Two phases:
#   Phase A: MEM-parameter sweep, each candidate run with topology MEM x3.
#   Phase B: Best MEM params combined with various other-attacker topologies
#            (MEM x3, MEM x2 + X, MEM + 2X, BUS_x2 + MEM, all-different mixes,
#             plus a few oversubscription tests with 4 background workers).
#
# Excludes huff_dec.

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
OUTDIR=results/tim_mix2
mkdir -p "$OUTDIR"

DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'
# Defaults for non-tuned attackers in phase B
CACHE_DEFS='-DCACHE_MEM_SIZE=1048576 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=0 -DCACHE_FR_RATIO=50 -DCACHE_ITER=1'
BUS_DEFS='-DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4'
POINTER_DEFS='-DPOINTER_ELEMENTS=65536u -DPOINTER_STRIDE=17u -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384 -DPOINTER_CACHE_LINE=64'
PIPELINE_DEFS='-DPIPELINE_RATIO=100 -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=8192'

extract_median_cycles() { awk '/^  cycles / { print $5 }' "$1"; }

declare -A SOLO_MED
build_and_baseline() {
    local extra="$1"
    rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/MEM/MEM.o \
          build/POINTER/POINTER.o build/PIPELINE/PIPELINE.o \
          build/CACHE.o build/BUS.o build/MEM.o build/POINTER.o build/PIPELINE.o \
          multi_proc_pmu
    make -s CFLAGS="$DEFAULT_CFLAGS $extra" >/dev/null
    for v in "${VICTIMS[@]}"; do
        log="$OUTDIR/solo_${v}_$(echo "$extra" | md5sum | cut -c1-6).txt"
        "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$v" >"$log" 2>&1
        SOLO_MED["$v"]=$(extract_median_cycles "$log")
    done
}

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

# =========================================================================
# Phase A: MEM-parameter sweep, fixed topology = MEM x3
# Each candidate is a different MEM compilation.
# =========================================================================
MEM_VARIANTS=(
  # tag                | MEM defines
  "MEM_def_x3          | -DMEM_SIZE_MB=1  -DMEM_PAGE_SIZE=4096  -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_8M_x3           | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=4096  -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_16M_x3          | -DMEM_SIZE_MB=16 -DMEM_PAGE_SIZE=4096  -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_32M_x3          | -DMEM_SIZE_MB=32 -DMEM_PAGE_SIZE=4096  -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_64M_x3          | -DMEM_SIZE_MB=64 -DMEM_PAGE_SIZE=4096  -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_8M_p64_x3       | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=64    -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_8M_p256_x3      | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=256   -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_8M_p1024_x3     | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=1024  -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_8M_p16K_x3      | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=16384 -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_8M_p64K_x3      | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=65536 -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_8M_full_x3      | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=4096  -DMEM_OP_RATIO=10000 -DMEM_ITER=4"
  "MEM_8M_iter16_x3    | -DMEM_SIZE_MB=8  -DMEM_PAGE_SIZE=4096  -DMEM_OP_RATIO=2521 -DMEM_ITER=16"
  "MEM_16M_p16K_x3     | -DMEM_SIZE_MB=16 -DMEM_PAGE_SIZE=16384 -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
  "MEM_32M_p16K_x3     | -DMEM_SIZE_MB=32 -DMEM_PAGE_SIZE=16384 -DMEM_OP_RATIO=2521 -DMEM_ITER=4"
)

echo "###############################################"
echo "# Phase A: MEM parameter sweep with topology MEM x3"
echo "###############################################"
for entry in "${MEM_VARIANTS[@]}"; do
    IFS='|' read -r tag mem_defs <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    mem_defs="$(echo -n "$mem_defs" | xargs)"
    echo "-- Building MEM variant: $tag --"
    build_and_baseline "$mem_defs"
    run_mix "$tag" MEM MEM MEM
done

# =========================================================================
# Phase B: Lock the best MEM params (from prior knowledge: 8M, 4096, default
# op_ratio, ITER=4) and explore many topologies, including 4-worker oversubs.
# Build once with everything aggressive.
# =========================================================================
echo
echo "###############################################"
echo "# Phase B: aggressive build + topology sweep"
echo "###############################################"
BEST_MEM='-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE=4096 -DMEM_OP_RATIO=2521 -DMEM_ITER=4'
ALL_DEFS="$CACHE_DEFS $BUS_DEFS $BEST_MEM $POINTER_DEFS $PIPELINE_DEFS"
build_and_baseline "$ALL_DEFS"

TOPOLOGIES=(
  # 3-attacker mixes already tested but with new build (sanity)
  "B_MEM_x3            | MEM MEM MEM"
  "B_MEM_x2_CACHE      | MEM MEM CACHE"
  "B_MEM_x2_BUS        | MEM MEM BUS"
  "B_MEM_x2_POINT      | MEM MEM POINTER"
  "B_MEM_x2_PIPE       | MEM MEM PIPELINE"
  "B_MEM_CACHE_BUS     | MEM CACHE BUS"
  "B_MEM_CACHE_POINT   | MEM CACHE POINTER"
  "B_MEM_BUS_POINT     | MEM BUS POINTER"
  "B_MEM_BUS_PIPE      | MEM BUS PIPELINE"
  "B_CACHE_BUS_POINT   | CACHE BUS POINTER"

  # 4-worker oversubs (more procs than free cpus -> contention + scheduling)
  "Q_MEM_x4            | MEM MEM MEM MEM"
  "Q_MEM_x3_CACHE      | MEM MEM MEM CACHE"
  "Q_MEM_x3_BUS        | MEM MEM MEM BUS"
  "Q_MEM_x2_CACHE_BUS  | MEM MEM CACHE BUS"
  "Q_MEM_x2_BUS_x2     | MEM MEM BUS BUS"
  "Q_BUS_x2_MEM_x2     | BUS BUS MEM MEM"
  "Q_CACHE_x2_MEM_x2   | CACHE CACHE MEM MEM"
  "Q_one_each_plus_MEM | MEM CACHE BUS POINTER"
  "Q_one_each          | CACHE BUS POINTER PIPELINE"

  # 5-worker very heavy oversubs
  "F_MEM_x5            | MEM MEM MEM MEM MEM"
  "F_MEM_x3_BUS_x2     | MEM MEM MEM BUS BUS"
)

for entry in "${TOPOLOGIES[@]}"; do
    IFS='|' read -r tag bg <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    bg="$(echo -n "$bg" | xargs)"
    # shellcheck disable=SC2206
    bg_args=( $bg )
    run_mix "$tag" "${bg_args[@]}"
done

# =========================================================================
# Phase C: try the strongest MEM variant from phase A combined with extras.
# We re-pick winners by re-reading summary.csv.
# =========================================================================
echo
echo "###############################################"
echo "# Phase C: pick top MEM variant and combine"
echo "###############################################"
TOP_MEM_TAG=$(python3 - <<'PY'
import csv
best = None
with open("results/tim_mix2/summary.csv") as f:
    rdr = csv.reader(f)
    hdr = next(rdr)
    bs = hdr.index("binarysearch")
    for r in rdr:
        if not r[0].startswith("MEM_"):  # phase A only
            continue
        try:
            x = float(r[bs])
        except ValueError:
            continue
        if best is None or x > best[1]:
            best = (r[0], x)
print(best[0] if best else "MEM_8M_x3")
PY
)
echo "Top MEM variant by binarysearch slowdown: $TOP_MEM_TAG"

# Map tag -> MEM defines
declare -A MEM_DEFS_MAP
for entry in "${MEM_VARIANTS[@]}"; do
    IFS='|' read -r t d <<<"$entry"
    t="$(echo -n "$t" | xargs)"
    d="$(echo -n "$d" | xargs)"
    MEM_DEFS_MAP["$t"]="$d"
done
TOP_MEM_DEFS="${MEM_DEFS_MAP[$TOP_MEM_TAG]:-$BEST_MEM}"
echo "Using MEM defines: $TOP_MEM_DEFS"

ALL_TOP="$CACHE_DEFS $BUS_DEFS $TOP_MEM_DEFS $POINTER_DEFS $PIPELINE_DEFS"
build_and_baseline "$ALL_TOP"

TOP_TOPOS=(
  "C_top_MEM_x3        | MEM MEM MEM"
  "C_top_MEM_x2_CACHE  | MEM MEM CACHE"
  "C_top_MEM_x2_BUS    | MEM MEM BUS"
  "C_top_MEM_x4        | MEM MEM MEM MEM"
  "C_top_MEM_x3_BUS    | MEM MEM MEM BUS"
  "C_top_MEM_x3_CACHE  | MEM MEM MEM CACHE"
  "C_top_MEM_x5        | MEM MEM MEM MEM MEM"
  "C_top_MEM_x3_BUS_x2 | MEM MEM MEM BUS BUS"
)
for entry in "${TOP_TOPOS[@]}"; do
    IFS='|' read -r tag bg <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    bg="$(echo -n "$bg" | xargs)"
    # shellcheck disable=SC2206
    bg_args=( $bg )
    run_mix "$tag" "${bg_args[@]}"
done

# ---- Restore default ----
echo
echo "== Restoring default configuration =="
rm -f build/*/MEM.o build/*/CACHE.o build/*/BUS.o build/*/POINTER.o build/*/PIPELINE.o multi_proc_pmu
make -s >/dev/null

echo
echo "== Combined slowdown ratio table =="
column -t -s, "$SUMMARY"
echo
echo "== Worst attack per victim =="
python3 - <<'PY'
import csv
with open("results/tim_mix2/summary.csv") as f:
    rows = list(csv.reader(f))
hdr = rows[0]
victims = hdr[1:]
worst = {v: (None, -1.0) for v in victims}
for r in rows[1:]:
    cfg = r[0]
    for v, val in zip(victims, r[1:]):
        try: x = float(val)
        except ValueError: continue
        if x > worst[v][1]: worst[v] = (cfg, x)
print(f"{'victim':<14} {'best_attack':<24} {'ratio':>8}")
print("-" * 50)
for v in victims:
    cfg, x = worst[v]
    print(f"{v:<14} {cfg:<24} {x:>8.4f}")
PY

echo
echo "Per-run logs: $OUTDIR/  ;  CSV: $SUMMARY"
