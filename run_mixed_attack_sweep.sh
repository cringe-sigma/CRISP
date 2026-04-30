#!/bin/bash
# run_mixed_attack_sweep.sh
#
# For every TACLeBench victim, run multi_proc_pmu locked at FREQ kHz with
# a curated set of 3-attacker BG mixes covering every interference channel
# (TIM/POLY/MTH x cache/bus/mem/pointer/pipeline/tlb/io/spawn).
# Records min/avg/median cycles and computes slowdown vs solo baseline.
#
# Output: results/mixed/<victim>__<mix>.txt   (raw)
#         results/mixed/sweep_summary.csv     (one row per (victim, mix))
set -u
FREQ=${FREQ:-1600000}
N=${N:-10}
OUT=${OUT:-results/mixed}
mkdir -p "$OUT"

# Curated 3-attacker mixes (one combo per channel + cross-category mixes).
MIXES=(
    "TIM_CACHE3:CACHE CACHE CACHE"
    "MTH_CACHE3:MTH_CACHE MTH_CACHE MTH_CACHE"
    "PR_CACHE3:PR_CACHE PR_CACHE PR_CACHE"
    "ALL_CACHE:CACHE MTH_CACHE PR_CACHE"

    "ALL_BUS:BUS MTH_BUS PR_MEMBUS"
    "ALL_MEM:MEM MTH_MEM PR_ROWBUF"
    "ALL_PTR:POINTER MTH_POINTER PR_POINTER"
    "ALL_PIPE:PIPELINE MTH_PIPELINE MTH_PIPELINE"
    "TLB_TRIPLE:PR_TLB PR_TLB PR_TLB"

    "MIX_CACHEBUS:PR_CACHE MTH_BUS PR_MEMBUS"
    "MIX_TLBMEM:PR_TLB MTH_MEM PR_ROWBUF"
    "MIX_KERNEL:PR_NET PR_FILESYS PR_SPAWN"
    "MIX_IO:PR_DISKIO PR_FILESYS MTH_SYSCALLS"
    "MIX_HARSH:PR_ROWBUF MTH_BUS PR_MEMBUS"
    "MIX_BROAD:PR_CACHE PR_MEMBUS PR_TLB"
)

# TACLeBench victims (kernel/app/sequential/test). Skip huff_dec (known
# crash on _init re-entry) and very long ones to keep sweep tractable.
VICTIMS=(
  adpcm_dec adpcm_enc ammunition anagram audiobeam binarysearch bitcount
  bitonic bsort cjpeg_transupp cjpeg_wrbmp complex_updates cosf countnegative
  cover cubic deg2rad dijkstra duff epic fac fft filterbank fir2dim fmref
  g723_enc gsm_dec gsm_enc h264_dec huff_enc iir insertsort isqrt jfdctint
  lift lms ludcmp matrix1 md5 minver mpeg2 ndes petrinet pm powerwindow
  prime quicksort rad2deg recursion rijndael_dec rijndael_enc sha st
  statemate susan test3
)

CSV="$OUT/sweep_summary.csv"
echo "victim,mix,attackers,baseline_cycles_med,bg_cycles_med,slowdown_med,baseline_cycles_avg,bg_cycles_avg,slowdown_avg" > "$CSV"

bin=./multi_proc_pmu
[[ -x "$bin" ]] || { echo "missing $bin; run 'make' first"; exit 1; }

extract_med() { awk '/^  cycles/{print $5; exit}' "$1"; }
extract_avg() { awk '/^  cycles/{print $4; exit}' "$1"; }

total=$(( ${#VICTIMS[@]} * (1 + ${#MIXES[@]}) ))
done=0
ts=$(date +%s)

for v in "${VICTIMS[@]}"; do
    solo="$OUT/${v}__solo.txt"
    if [[ ! -s "$solo" ]]; then
        sudo "$bin" -n "$N" -f "$FREQ" "$v" > "$solo" 2>&1 || echo "[warn] solo $v failed"
    fi
    base_med=$(extract_med "$solo"); base_avg=$(extract_avg "$solo")
    [[ -z "${base_med:-}" ]] && base_med=0
    [[ -z "${base_avg:-}" ]] && base_avg=0
    done=$((done+1))
    elapsed=$(( $(date +%s) - ts ))
    echo "[$done/$total t=${elapsed}s] solo $v base_med=$base_med"

    for entry in "${MIXES[@]}"; do
        name="${entry%%:*}"
        atks="${entry#*:}"
        out="$OUT/${v}__${name}.txt"
        if [[ ! -s "$out" ]]; then
            sudo "$bin" -n "$N" -f "$FREQ" "$v" $atks > "$out" 2>&1 || echo "[warn] $v + $name failed"
        fi
        bg_med=$(extract_med "$out"); bg_avg=$(extract_avg "$out")
        [[ -z "${bg_med:-}" ]] && bg_med=0
        [[ -z "${bg_avg:-}" ]] && bg_avg=0
        sd_med="0"
        sd_avg="0"
        if [[ "$base_med" != "0" && "$base_med" != "" ]]; then
            sd_med=$(awk -v a="$bg_med" -v b="$base_med" 'BEGIN{ if(b>0) printf "%.3f", a/b; else print "0" }')
        fi
        if [[ "$base_avg" != "0" && "$base_avg" != "" ]]; then
            sd_avg=$(awk -v a="$bg_avg" -v b="$base_avg" 'BEGIN{ if(b>0) printf "%.3f", a/b; else print "0" }')
        fi
        echo "$v,$name,\"$atks\",$base_med,$bg_med,$sd_med,$base_avg,$bg_avg,$sd_avg" >> "$CSV"
        done=$((done+1))
        elapsed=$(( $(date +%s) - ts ))
        echo "[$done/$total t=${elapsed}s] $v + $name slowdown=${sd_med}x"
    done
done

echo "=== sweep done ==="
echo "summary: $CSV"
