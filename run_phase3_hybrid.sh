#!/usr/bin/env bash
# Phase-3 hybrid sweep: rebuild TIM with ALL_BEST tuned macros,
# then test mixes that combine tuned-TIM + POLY + MTH attackers
# on the strongest-responding victims from phase-1.
#
# Only sudo is used to invoke multi_proc_pmu (perf_event_open + cpufreq).
# Caller must already have key authentication / cached sudo.
set -euo pipefail
cd "$(dirname "$0")"

N="${N:-12}"
FREQ="${FREQ:-1600000}"
OUTDIR="${OUTDIR:-results/phase3}"
mkdir -p "$OUTDIR"

# ---- TIM tuned defs (mirrored from run_champions.sh) ----
DEFS_CACHE_TUNED='-DCACHE_MEM_SIZE=4194304 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50 -DCACHE_ITER=1'
DEFS_BUS_TUNED='-DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=8 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4'
DEFS_POINTER_TUNED='-DPOINTER_ELEMENTS=65536u -DPOINTER_STRIDE=1u -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384 -DPOINTER_CACHE_LINE=64'
DEFS_PIPELINE_TUNED='-DPIPELINE_RATIO=50 -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=4096'
DEFS_MEM_TUNED='-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE=16384 -DMEM_OP_RATIO=2521 -DMEM_ITER=16'
ALL_BEST="$DEFS_CACHE_TUNED $DEFS_BUS_TUNED $DEFS_POINTER_TUNED $DEFS_PIPELINE_TUNED $DEFS_MEM_TUNED"

echo "[phase3] rebuilding harness with TIM tuned macros..."
make clean >/dev/null
BASE_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'
make -j"$(nproc)" CFLAGS="$BASE_CFLAGS $ALL_BEST" >/dev/null
echo "[phase3] build OK"

# ---- Victims: top responders from phase-1 + a few others worth re-checking ----
VICTIMS=(
  prime petrinet minver ludcmp complex_updates cover iir duff fac
  cosf jfdctint insertsort fir2dim bitcount fft adpcm_dec statemate
  fmref pm bsort recursion ndes susan h264_dec rijndael_dec
)

# ---- Mixes: hybrid triples never tried in phase-1/phase-2 ----
declare -A MIX
MIX[H1_CACHE_PRC_MTHC]="CACHE PR_CACHE MTH_CACHE"
MIX[H2_CACHE_CACHE_PRC]="CACHE CACHE PR_CACHE"
MIX[H3_CACHE_PRC_PRC]="CACHE PR_CACHE PR_CACHE"
MIX[H4_TUNED_KILLER]="CACHE BUS MEM"
MIX[H5_MEM_PRR_MTHM]="MEM PR_ROWBUF MTH_MEM"
MIX[H6_CACHE_PRC_PRT]="CACHE PR_CACHE PR_TLB"
MIX[H7_CACHE_PRC_PRMEMBUS]="CACHE PR_CACHE PR_MEMBUS"
MIX[H8_BUS_PRMEMBUS_PRC]="BUS PR_MEMBUS PR_CACHE"
MIX[H9_PTR_PRPTR_MTHPTR]="POINTER PR_POINTER MTH_POINTER"
MIX[H10_PRC_PRT_PRROW]="PR_CACHE PR_TLB PR_ROWBUF"
MIX[H11_PRC_PRT_PRMEMBUS]="PR_CACHE PR_TLB PR_MEMBUS"
MIX[H12_PRC_PRROW_MTHM]="PR_CACHE PR_ROWBUF MTH_MEM"
MIX[H13_CACHE_BUS_PRC]="CACHE BUS PR_CACHE"
MIX[H14_CACHE_MEM_PRC]="CACHE MEM PR_CACHE"
MIX[H15_PRC_MTHC_MTHM]="PR_CACHE MTH_CACHE MTH_MEM"
MIX[H16_CACHE_MEM_BUS]="CACHE MEM BUS"
MIX[H17_PRC_PRC_PRT]="PR_CACHE PR_CACHE PR_TLB"
MIX[H18_PRC_PRC_PRMEMBUS]="PR_CACHE PR_CACHE PR_MEMBUS"
MIX[H19_MEM_MEM_PRROW]="MEM MEM PR_ROWBUF"
MIX[H20_PTR_PRPTR_PRC]="POINTER PR_POINTER PR_CACHE"

CSV="$OUTDIR/sweep_summary.csv"
RAW_DIR="$OUTDIR/raw"
mkdir -p "$RAW_DIR"
echo "victim,mix,attackers,baseline_med,bg_med,slowdown_med,baseline_avg,bg_avg,slowdown_avg" > "$CSV"

solo_med() { awk '/^  cycles /{print $3}' "$1" | head -1; }
solo_avg() { awk '/^  cycles /{print $5}' "$1" | head -1; }

# Cache solo baselines so we don't re-measure them N¡Á|MIX| times.
declare -A SOLO_MED SOLO_AVG
for V in "${VICTIMS[@]}"; do
  SF="$RAW_DIR/solo_${V}.txt"
  if [[ ! -s "$SF" ]]; then
    echo "[solo] $V"
    sudo -n ./multi_proc_pmu -n "$N" -f "$FREQ" "$V" > "$SF" 2>&1 || true
  fi
  med=$(awk '/^  cycles /{print $5}' "$SF" | head -1)
  avg=$(awk '/^  cycles /{print $4}' "$SF" | head -1)
  SOLO_MED[$V]="$med"
  SOLO_AVG[$V]="$avg"
done

total=$(( ${#VICTIMS[@]} * ${#MIX[@]} ))
i=0
for V in "${VICTIMS[@]}"; do
  base_med=${SOLO_MED[$V]:-0}
  base_avg=${SOLO_AVG[$V]:-0}
  for K in "${!MIX[@]}"; do
    A="${MIX[$K]}"
    i=$((i+1))
    f="$RAW_DIR/${V}__${K}.txt"
    echo "[$i/$total] $V :: $K :: $A"
    sudo -n ./multi_proc_pmu -n "$N" -f "$FREQ" $V $A > "$f" 2>&1 || true
    bg_med=$(awk '/^  cycles /{print $5}' "$f" | head -1)
    bg_avg=$(awk '/^  cycles /{print $4}' "$f" | head -1)
    if [[ -z "$bg_med" || -z "$base_med" || "$base_med" == "0" ]]; then continue; fi
    sd_med=$(awk -v b="$base_med" -v g="$bg_med" 'BEGIN{if(b>0)printf "%.3f",g/b; else print "0"}')
    sd_avg=$(awk -v b="$base_avg" -v g="$bg_avg" 'BEGIN{if(b>0)printf "%.3f",g/b; else print "0"}')
    printf '%s,%s,"%s",%s,%s,%s,%s,%s,%s\n' \
      "$V" "$K" "$A" "$base_med" "$bg_med" "$sd_med" "$base_avg" "$bg_avg" "$sd_avg" >> "$CSV"
  done
done

echo "[phase3] done -> $CSV"
echo "[phase3] top-25 by slowdown_med:"
sort -t, -k6 -rn "$CSV" | head -25
