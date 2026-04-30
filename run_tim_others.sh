#!/usr/bin/env bash
# Fine-grained parameter sweeps for *all* TIM attackers (not just MEM):
# CACHE, BUS, POINTER, PIPELINE.  Each phase tunes one bench using the
# best-known topology (typically x3 same workers on cpu1..cpu3).  After
# tuning each bench, we run cross-mixes that swap in the tuned variants.
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
OUTDIR=results/tim_others
mkdir -p "$OUTDIR"
DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'

# Best-known MEM (kept fixed in cross-mix phase)
BEST_MEM='-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE=16384 -DMEM_OP_RATIO=2521 -DMEM_ITER=16'

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
        log="$OUTDIR/solo_${v}.txt"
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
    echo "================================================================"
    echo "== $tag    bg=[${bg_args[*]}]"
    echo "================================================================"
    printf '%-14s %18s %18s %10s\n' "victim" "solo" "pair" "ratio"
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
# Phase A: CACHE parameter sweep, topology CACHE x3
# =========================================================================
echo "###############################################"
echo "# Phase A: CACHE param sweep (CACHE x3)"
echo "###############################################"
CACHE_VARIANTS=(
  "CA_1M_s64_w0     | -DCACHE_MEM_SIZE=1048576  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_2M_s64_w0     | -DCACHE_MEM_SIZE=2097152  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_4M_s64_w0     | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_8M_s64_w0     | -DCACHE_MEM_SIZE=8388608  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_16M_s64_w0    | -DCACHE_MEM_SIZE=16777216 -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_4M_s128_w0    | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=128 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_4M_s256_w0    | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=256 -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_4M_s64_iter4  | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=4"
  "CA_4M_s64_iter16 | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=16"
  "CA_4M_s64_iter64 | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=50 -DCACHE_ITER=64"
  "CA_4M_s64_w50    | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_4M_s64_w100   | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=100 -DCACHE_FR_RATIO=50 -DCACHE_ITER=1"
  "CA_4M_s64_fr0    | -DCACHE_MEM_SIZE=4194304  -DCACHE_STRIDE=64  -DCACHE_RW_RATIO=0  -DCACHE_FR_RATIO=0  -DCACHE_ITER=1"
)
for entry in "${CACHE_VARIANTS[@]}"; do
    IFS='|' read -r tag defs <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    defs="$(echo -n "$defs" | xargs)"
    echo "-- Build $tag --"
    build_and_baseline "$defs"
    run_mix "$tag" CACHE CACHE CACHE
done

# =========================================================================
# Phase B: BUS parameter sweep, topology BUS x3
# =========================================================================
echo "###############################################"
echo "# Phase B: BUS param sweep (BUS x3)"
echo "###############################################"
BUS_VARIANTS=(
  "BA_1M_t5_d333_c100 | -DBUS_SIZE_MB=1   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_t5_d333_c100 | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_8M_t5_d333_c100 | -DBUS_SIZE_MB=8   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_16M_t5_d333_c100| -DBUS_SIZE_MB=16  -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_32M_t5_d333_c100| -DBUS_SIZE_MB=32  -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_t1_d333_c100 | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=1 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_t7_d333_c100 | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=7 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_t8_d333_c100 | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=8 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_d111_c100    | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=111 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_d100_c100    | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=100 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_d001_c100    | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=001 -DBUS_CPU_RATIO=100 -DBUS_ITER=4"
  "BA_4M_d333_c0      | -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=0   -DBUS_ITER=4"
  "BA_4M_d333_c100_i16| -DBUS_SIZE_MB=4   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=16"
  "BA_8M_d333_c100_i16| -DBUS_SIZE_MB=8   -DBUS_DATA_TYPE=5 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=16"
)
for entry in "${BUS_VARIANTS[@]}"; do
    IFS='|' read -r tag defs <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    defs="$(echo -n "$defs" | xargs)"
    echo "-- Build $tag --"
    build_and_baseline "$defs"
    run_mix "$tag" BUS BUS BUS
done

# =========================================================================
# Phase C: POINTER parameter sweep, topology POINTER x3
# =========================================================================
echo "###############################################"
echo "# Phase C: POINTER param sweep (POINTER x3)"
echo "###############################################"
POINTER_VARIANTS=(
  "PA_4K_s17_l100_i8K   | -DPOINTER_ELEMENTS=4096u   -DPOINTER_STRIDE=17u   -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=8192    -DPOINTER_CACHE_LINE=64"
  "PA_64K_s17_l100_i16K | -DPOINTER_ELEMENTS=65536u  -DPOINTER_STRIDE=17u   -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384   -DPOINTER_CACHE_LINE=64"
  "PA_256K_s17_l100     | -DPOINTER_ELEMENTS=262144u -DPOINTER_STRIDE=17u   -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=32768   -DPOINTER_CACHE_LINE=64"
  "PA_1M_s17_l100       | -DPOINTER_ELEMENTS=1048576u -DPOINTER_STRIDE=17u  -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=65536   -DPOINTER_CACHE_LINE=64"
  "PA_64K_s1_l100       | -DPOINTER_ELEMENTS=65536u  -DPOINTER_STRIDE=1u    -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384   -DPOINTER_CACHE_LINE=64"
  "PA_64K_s521_l100     | -DPOINTER_ELEMENTS=65536u  -DPOINTER_STRIDE=521u  -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384   -DPOINTER_CACHE_LINE=64"
  "PA_64K_s17_l0        | -DPOINTER_ELEMENTS=65536u  -DPOINTER_STRIDE=17u   -DPOINTER_LOAD_RATIO=0   -DPOINTER_ITER=16384   -DPOINTER_CACHE_LINE=64"
  "PA_64K_s17_l50       | -DPOINTER_ELEMENTS=65536u  -DPOINTER_STRIDE=17u   -DPOINTER_LOAD_RATIO=50  -DPOINTER_ITER=16384   -DPOINTER_CACHE_LINE=64"
  "PA_64K_s17_l100_cl128| -DPOINTER_ELEMENTS=65536u  -DPOINTER_STRIDE=17u   -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384   -DPOINTER_CACHE_LINE=128"
)
for entry in "${POINTER_VARIANTS[@]}"; do
    IFS='|' read -r tag defs <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    defs="$(echo -n "$defs" | xargs)"
    echo "-- Build $tag --"
    build_and_baseline "$defs"
    run_mix "$tag" POINTER POINTER POINTER
done

# =========================================================================
# Phase D: PIPELINE parameter sweep, topology PIPELINE x3
# =========================================================================
echo "###############################################"
echo "# Phase D: PIPELINE param sweep (PIPELINE x3)"
echo "###############################################"
PIPELINE_VARIANTS=(
  "PL_r0_p6_i4K   | -DPIPELINE_RATIO=0   -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=4096"
  "PL_r50_p6_i4K  | -DPIPELINE_RATIO=50  -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=4096"
  "PL_r100_p6_i4K | -DPIPELINE_RATIO=100 -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=4096"
  "PL_r100_p6_i16K| -DPIPELINE_RATIO=100 -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=16384"
  "PL_r100_p6_i64K| -DPIPELINE_RATIO=100 -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=65536"
  "PL_r100_p2_i16K| -DPIPELINE_RATIO=100 -DPIPELINE_PRECISION=2 -DPIPELINE_ITER=16384"
  "PL_r100_p15_i16K| -DPIPELINE_RATIO=100 -DPIPELINE_PRECISION=15 -DPIPELINE_ITER=16384"
)
for entry in "${PIPELINE_VARIANTS[@]}"; do
    IFS='|' read -r tag defs <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    defs="$(echo -n "$defs" | xargs)"
    echo "-- Build $tag --"
    build_and_baseline "$defs"
    run_mix "$tag" PIPELINE PIPELINE PIPELINE
done

# =========================================================================
# Phase E: cross-mix using each bench's tuned variant + best MEM
# =========================================================================
echo "###############################################"
echo "# Phase E: cross-mix with all best params"
echo "###############################################"
BEST_CACHE=$(python3 - "$SUMMARY" <<'PY'
import csv, sys
fn=sys.argv[1]
rdr=list(csv.reader(open(fn)))
hdr=rdr[0]; bs=hdr.index("binarysearch")
ip=hdr.index("insertsort")
best=None
for r in rdr[1:]:
    if not r[0].startswith("CA_"): continue
    try: x=max(float(r[bs]),float(r[ip]))
    except ValueError: continue
    if best is None or x>best[1]: best=(r[0],x)
print(best[0])
PY
)
BEST_BUS=$(python3 - "$SUMMARY" <<'PY'
import csv, sys
fn=sys.argv[1]
rdr=list(csv.reader(open(fn)))
hdr=rdr[0]
fm=hdr.index("fmref")
ad=hdr.index("adpcm_dec")
best=None
for r in rdr[1:]:
    if not r[0].startswith("BA_"): continue
    try: x=max(float(r[fm]),float(r[ad]))
    except ValueError: continue
    if best is None or x>best[1]: best=(r[0],x)
print(best[0])
PY
)
BEST_POINTER=$(python3 - "$SUMMARY" <<'PY'
import csv, sys
fn=sys.argv[1]
rdr=list(csv.reader(open(fn)))
hdr=rdr[0]; cols=hdr[1:]
best=None
for r in rdr[1:]:
    if not r[0].startswith("PA_"): continue
    try: x=max(float(v) for v in r[1:])
    except ValueError: continue
    if best is None or x>best[1]: best=(r[0],x)
print(best[0])
PY
)
BEST_PIPELINE=$(python3 - "$SUMMARY" <<'PY'
import csv, sys
fn=sys.argv[1]
rdr=list(csv.reader(open(fn)))
hdr=rdr[0]; cols=hdr[1:]
best=None
for r in rdr[1:]:
    if not r[0].startswith("PL_"): continue
    try: x=max(float(v) for v in r[1:])
    except ValueError: continue
    if best is None or x>best[1]: best=(r[0],x)
print(best[0])
PY
)
echo "Tuned winners: CACHE=$BEST_CACHE  BUS=$BEST_BUS  POINTER=$BEST_POINTER  PIPELINE=$BEST_PIPELINE"

# Map tag -> defines
declare -A DEFS_MAP
for entry in "${CACHE_VARIANTS[@]}" "${BUS_VARIANTS[@]}" "${POINTER_VARIANTS[@]}" "${PIPELINE_VARIANTS[@]}"; do
    IFS='|' read -r t d <<<"$entry"
    t="$(echo -n "$t" | xargs)"; d="$(echo -n "$d" | xargs)"
    DEFS_MAP["$t"]="$d"
done

ALL_BEST="${DEFS_MAP[$BEST_CACHE]} ${DEFS_MAP[$BEST_BUS]} ${DEFS_MAP[$BEST_POINTER]} ${DEFS_MAP[$BEST_PIPELINE]} $BEST_MEM"
echo "Combined defines: $ALL_BEST"
build_and_baseline "$ALL_BEST"

CROSS_TOPOS=(
  "X_CACHE_x3      | CACHE CACHE CACHE"
  "X_BUS_x3        | BUS BUS BUS"
  "X_POINTER_x3    | POINTER POINTER POINTER"
  "X_PIPELINE_x3   | PIPELINE PIPELINE PIPELINE"
  "X_MEM_x3        | MEM MEM MEM"
  "X_CACHE_BUS_MEM | CACHE BUS MEM"
  "X_MEM_x2_CACHE  | MEM MEM CACHE"
  "X_MEM_x2_BUS    | MEM MEM BUS"
  "X_MEM_x2_POINT  | MEM MEM POINTER"
  "X_MEM_BUS_x2    | MEM BUS BUS"
  "X_BUS_x2_CACHE  | BUS BUS CACHE"
  "X_one_each      | CACHE BUS POINTER PIPELINE"
  "X_one_each_MEM  | MEM CACHE BUS POINTER"
  "X_MEM_x4        | MEM MEM MEM MEM"
  "X_MEM_x5        | MEM MEM MEM MEM MEM"
  "X_MEM_x3_BUS    | MEM MEM MEM BUS"
  "X_MEM_x3_CACHE  | MEM MEM MEM CACHE"
  "X_BUS_x4        | BUS BUS BUS BUS"
  "X_CACHE_x4      | CACHE CACHE CACHE CACHE"
)
for entry in "${CROSS_TOPOS[@]}"; do
    IFS='|' read -r tag bg <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    bg="$(echo -n "$bg" | xargs)"
    # shellcheck disable=SC2206
    bg_args=( $bg )
    run_mix "$tag" "${bg_args[@]}"
done

# ---- Restore default ----
rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/MEM/MEM.o build/POINTER/POINTER.o build/PIPELINE/PIPELINE.o multi_proc_pmu
make -s >/dev/null

echo
echo "== Worst attack per victim =="
python3 - <<'PY'
import csv
with open("results/tim_others/summary.csv") as f:
    rows = list(csv.reader(f))
hdr = rows[0]; victims = hdr[1:]
worst = {v: (None, -1.0) for v in victims}
for r in rows[1:]:
    cfg = r[0]
    for v, val in zip(victims, r[1:]):
        try: x = float(val)
        except ValueError: continue
        if x > worst[v][1]: worst[v] = (cfg, x)
print(f"{'victim':<14} {'best_attack':<28} {'ratio':>8}")
print("-" * 54)
for v in victims:
    cfg, x = worst[v]
    print(f"{v:<14} {cfg:<28} {x:>8.4f}")
PY

echo
echo "Per-run logs: $OUTDIR/  ;  CSV: $SUMMARY"
echo "Full table:"
column -t -s, "$SUMMARY"
