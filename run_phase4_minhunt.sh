#!/usr/bin/env bash
# Phase-4: worst-case interference hunt.
# - Reuses the TIM-tuned-macro build produced by run_phase3_hybrid.sh
#   (so CACHE/BUS/MEM/POINTER/PIPELINE benches embed the ALL_BEST flags).
# - Runs every phase-3 hybrid mix against every TACLeBench victim.
# - Ranks by MIN-cycles ratio (max(atk_min)/solo_min) -- the worst-case
#   slowdown a sample ever exhibited, which previous experiments showed
#   to be the most reproducible "¶¾ÐÔ" metric.
set -euo pipefail
cd "$(dirname "$0")"

N="${N:-30}"
FREQ="${FREQ:-1600000}"
OUTDIR="${OUTDIR:-results/phase4}"
mkdir -p "$OUTDIR"
RAW_DIR="$OUTDIR/raw"
mkdir -p "$RAW_DIR"

# Sanity-check that the binary was built with TIM tuned macros (presence
# of -DCACHE_MEM_SIZE in build artifacts is the cheapest proxy).
if ! grep -q CACHE_MEM_SIZE build/CACHE.o 2>/dev/null && \
   ! strings ./multi_proc_pmu 2>/dev/null | grep -q CACHE_MEM_SIZE; then
  echo "[phase4][warn] cannot confirm TIM tuned build; rebuild with run_phase3_hybrid.sh first."
fi

# All TACLeBench-style victims discovered in build/ (non-attacker categories).
mapfile -t VICTIMS < <(
  ls -1 bench/bench/kernel bench/bench/app bench/bench/sequential bench/bench/test 2>/dev/null \
    | sort -u
)
# Strip empties / non-dirs.
VICTIMS=($(for v in "${VICTIMS[@]}"; do [[ -d build/$v || -f build/$v.o ]] && echo "$v"; done))

# Curated mixes: a superset of phase-3 plus a few new permutations focused
# on the configurations that scored highest at N=12.
declare -A MIX
MIX[H4_TUNED_KILLER]="CACHE BUS MEM"
MIX[H16_CACHE_MEM_BUS]="CACHE MEM BUS"
MIX[H13_CACHE_BUS_PRC]="CACHE BUS PR_CACHE"
MIX[H14_CACHE_MEM_PRC]="CACHE MEM PR_CACHE"
MIX[H3_CACHE_PRC_PRC]="CACHE PR_CACHE PR_CACHE"
MIX[H1_CACHE_PRC_MTHC]="CACHE PR_CACHE MTH_CACHE"
MIX[H8_BUS_PRMEMBUS_PRC]="BUS PR_MEMBUS PR_CACHE"
MIX[H9_PTR_PRPTR_MTHPTR]="POINTER PR_POINTER MTH_POINTER"
MIX[H17_PRC_PRC_PRT]="PR_CACHE PR_CACHE PR_TLB"
MIX[H15_PRC_MTHC_MTHM]="PR_CACHE MTH_CACHE MTH_MEM"
MIX[H12_PRC_PRROW_MTHM]="PR_CACHE PR_ROWBUF MTH_MEM"
MIX[H5_MEM_PRR_MTHM]="MEM PR_ROWBUF MTH_MEM"
MIX[PR_CACHE3]="PR_CACHE PR_CACHE PR_CACHE"
MIX[MTH_CACHE3]="MTH_CACHE MTH_CACHE MTH_CACHE"
MIX[N1_CACHE_BUS_PRMEMBUS]="CACHE BUS PR_MEMBUS"
MIX[N2_CACHE_MEM_PRROW]="CACHE MEM PR_ROWBUF"
MIX[N3_BUS_MEM_PRMEMBUS]="BUS MEM PR_MEMBUS"
MIX[N4_PTR_PTR_PRC]="POINTER POINTER PR_CACHE"
MIX[N5_CACHE_BUS_MTHM]="CACHE BUS MTH_MEM"

CSV="$OUTDIR/sweep_summary.csv"
echo "victim,mix,attackers,solo_min,solo_avg,solo_med,atk_min,atk_avg,atk_med,r_min,r_avg,r_med" > "$CSV"

solo_stats() { awk '/^  cycles /{print $3,$4,$5; exit}' "$1"; }

declare -A SOLO
for V in "${VICTIMS[@]}"; do
  SF="$RAW_DIR/solo_${V}.txt"
  if [[ ! -s "$SF" ]]; then
    sudo -n ./multi_proc_pmu -n "$N" -f "$FREQ" "$V" > "$SF" 2>&1 || true
  fi
  st=$(solo_stats "$SF")
  if [[ -z "$st" ]]; then
    echo "[phase4][skip] $V: no solo stats"
    continue
  fi
  SOLO[$V]="$st"
done

total=$(( ${#VICTIMS[@]} * ${#MIX[@]} ))
i=0; t0=$(date +%s)
for V in "${VICTIMS[@]}"; do
  [[ -z "${SOLO[$V]:-}" ]] && continue
  read smin savg smed <<<"${SOLO[$V]}"
  for K in "${!MIX[@]}"; do
    A="${MIX[$K]}"
    i=$((i+1))
    f="$RAW_DIR/${V}__${K}.txt"
    if [[ ! -s "$f" ]]; then
      sudo -n ./multi_proc_pmu -n "$N" -f "$FREQ" $V $A > "$f" 2>&1 || true
    fi
    ast=$(solo_stats "$f")
    [[ -z "$ast" ]] && continue
    read amin aavg amed <<<"$ast"
    r=$(awk -v sm="$smin" -v sa="$savg" -v sd="$smed" -v am="$amin" -v aa="$aavg" -v ad="$amed" \
        'BEGIN{ if(sm>0&&sa>0&&sd>0) printf "%.3f,%.3f,%.3f", am/sm, aa/sa, ad/sd; else print "0,0,0" }')
    printf '%s,%s,"%s",%s,%s,%s,%s,%s,%s,%s\n' \
      "$V" "$K" "$A" "$smin" "$savg" "$smed" "$amin" "$aavg" "$amed" "$r" >> "$CSV"
    if (( i % 50 == 0 )); then
      dt=$(( $(date +%s) - t0 ))
      echo "[phase4] $i/$total  ${dt}s elapsed"
    fi
  done
done

echo "[phase4] done -> $CSV"
echo
echo "=== Top 25 by min-cycles ratio ==="
sort -t, -k10 -rn "$CSV" | head -25
echo
echo "=== Top 25 by avg ratio ==="
sort -t, -k11 -rn "$CSV" | head -25
echo
echo "=== Top 25 by median ratio ==="
sort -t, -k12 -rn "$CSV" | head -25
