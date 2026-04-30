#!/bin/bash
# run_per_victim_hunt.sh
#
# Phase-1: for every TACLeBench victim, run each of the 20 single-source
#          attackers as a 3-cpu triple (A A A).  Pick top-3 by median
#          slowdown.
# Phase-2: for each victim, build victim-specific mixes from its top-3:
#          (top1 top1 top1), (top1 top2 top3), (top1 top1 top2),
#          (top2 top2 top2), (top1 top3 top3) -- 5 candidates.
# Phase-3: take overall champion mix for that victim and re-run with N=20
#          for confirmation.
#
# Output:
#   results/hunt/<victim>__<mix>.txt
#   results/hunt/per_victim_top.csv  (one row per attempted (victim, mix))
#   results/hunt/champions.csv        (one row per victim, the strongest)
set -u
FREQ=${FREQ:-1600000}
N=${N:-8}
NCONF=${NCONF:-20}
OUT=${OUT:-results/hunt}
mkdir -p "$OUT"

# 20 single-source attackers (TIM + MTH + POLY).  Excludes huff_dec-style
# crashable victims.
ATTACKERS=(
  CACHE BUS MEM POINTER PIPELINE
  MTH_CACHE MTH_BUS MTH_MEM MTH_POINTER MTH_PIPELINE MTH_SYSCALLS
  PR_CACHE PR_MEMBUS PR_ROWBUF PR_POINTER PR_TLB
  PR_DISKIO PR_FILESYS PR_NET PR_SPAWN
)

VICTIMS=(
  adpcm_dec adpcm_enc ammunition anagram audiobeam binarysearch bitcount
  bitonic bsort cjpeg_transupp cjpeg_wrbmp complex_updates cosf countnegative
  cover cubic deg2rad dijkstra duff epic fac fft filterbank fir2dim fmref
  g723_enc gsm_dec gsm_enc h264_dec huff_enc iir insertsort isqrt jfdctint
  lift lms ludcmp matrix1 md5 minver mpeg2 ndes petrinet pm powerwindow
  prime quicksort rad2deg recursion rijndael_dec rijndael_enc sha st
  statemate susan test3
)

CSV="$OUT/per_victim_top.csv"
CHAMP="$OUT/champions.csv"
echo "victim,mix_name,attackers,base_med,bg_med,sd_med,sd_avg" > "$CSV"
echo "victim,attackers,base_med,bg_med,sd_med,sd_avg" > "$CHAMP"

bin=./multi_proc_pmu
[[ -x "$bin" ]] || { echo "missing $bin"; exit 1; }
extract_med() { awk '/^  cycles/{print $5; exit}' "$1"; }
extract_avg() { awk '/^  cycles/{print $4; exit}' "$1"; }

run_once() {
    # $1=outfile $2=N $3=victim $4..=attackers
    local out="$1"; shift
    local n="$1"; shift
    if [[ ! -s "$out" ]]; then
        sudo "$bin" -n "$n" -f "$FREQ" "$@" > "$out" 2>&1 || echo "[warn] failed: $@"
    fi
}

ts=$(date +%s)
total=0
for v in "${VICTIMS[@]}"; do total=$((total + 1 + ${#ATTACKERS[@]} + 5)); done
done=0
echo "Estimated $total runs; phase-1 = $(( ${#VICTIMS[@]} * (1 + ${#ATTACKERS[@]}) ))"

for v in "${VICTIMS[@]}"; do
    # solo baseline
    solo="$OUT/${v}__solo.txt"
    run_once "$solo" "$N" "$v"
    base_med=$(extract_med "$solo"); base_avg=$(extract_avg "$solo")
    [[ -z "${base_med:-}" ]] && base_med=0
    [[ -z "${base_avg:-}" ]] && base_avg=0
    done=$((done+1))
    echo "[$done/$total t=$(( $(date +%s)-ts ))s] solo $v base=$base_med"

    # PHASE 1: single-source triples
    declare -a SCORES=()
    for a in "${ATTACKERS[@]}"; do
        out="$OUT/${v}__SOLO3_${a}.txt"
        run_once "$out" "$N" "$v" "$a" "$a" "$a"
        med=$(extract_med "$out"); avg=$(extract_avg "$out")
        [[ -z "${med:-}" ]] && med=0
        [[ -z "${avg:-}" ]] && avg=0
        sd_med=$(awk -v a="$med" -v b="$base_med" 'BEGIN{ if(b>0) printf "%.4f", a/b; else print "0" }')
        sd_avg=$(awk -v a="$avg" -v b="$base_avg" 'BEGIN{ if(b>0) printf "%.4f", a/b; else print "0" }')
        echo "$v,SOLO3_${a},\"$a $a $a\",$base_med,$med,$sd_med,$sd_avg" >> "$CSV"
        SCORES+=("$sd_med $a")
        done=$((done+1))
    done

    # Sort scores desc; pick top-3 attackers
    mapfile -t SORTED < <(printf '%s\n' "${SCORES[@]}" | sort -k1,1gr)
    top1=$(echo "${SORTED[0]}" | awk '{print $2}')
    top2=$(echo "${SORTED[1]}" | awk '{print $2}')
    top3=$(echo "${SORTED[2]}" | awk '{print $2}')
    top1_sd=$(echo "${SORTED[0]}" | awk '{print $1}')
    echo "  $v phase-1 top: $top1($top1_sd) > $top2 > $top3"

    # PHASE 2: 5 victim-specific mixes
    declare -A MIXMAP=(
        ["TOP1x3"]="$top1 $top1 $top1"
        ["TOP1_TOP2_TOP3"]="$top1 $top2 $top3"
        ["TOP1x2_TOP2"]="$top1 $top1 $top2"
        ["TOP2x3"]="$top2 $top2 $top2"
        ["TOP1_TOP3x2"]="$top1 $top3 $top3"
    )
    for name in TOP1x3 TOP1_TOP2_TOP3 TOP1x2_TOP2 TOP2x3 TOP1_TOP3x2; do
        atks="${MIXMAP[$name]}"
        out="$OUT/${v}__MIX_${name}.txt"
        run_once "$out" "$N" "$v" $atks
        med=$(extract_med "$out"); avg=$(extract_avg "$out")
        [[ -z "${med:-}" ]] && med=0
        [[ -z "${avg:-}" ]] && avg=0
        sd_med=$(awk -v a="$med" -v b="$base_med" 'BEGIN{ if(b>0) printf "%.4f", a/b; else print "0" }')
        sd_avg=$(awk -v a="$avg" -v b="$base_avg" 'BEGIN{ if(b>0) printf "%.4f", a/b; else print "0" }')
        echo "$v,MIX_${name},\"$atks\",$base_med,$med,$sd_med,$sd_avg" >> "$CSV"
        done=$((done+1))
    done

    # PHASE 3: pick best of all rows for this victim, re-confirm with NCONF samples.
    best_line=$(awk -F, -v vv="$v" 'BEGIN{best=0} $1==vv && $6+0>best { best=$6+0; row=$0 } END{print row}' "$CSV")
    best_atks=$(echo "$best_line" | awk -F, '{gsub(/"/,"",$3); print $3}')
    best_name=$(echo "$best_line" | awk -F, '{print $2}')
    confirm_out="$OUT/${v}__CONFIRM_${best_name}.txt"
    sudo "$bin" -n "$NCONF" -f "$FREQ" "$v" $best_atks > "$confirm_out" 2>&1 || true
    cmed=$(extract_med "$confirm_out"); cavg=$(extract_avg "$confirm_out")
    [[ -z "${cmed:-}" ]] && cmed=0
    [[ -z "${cavg:-}" ]] && cavg=0
    csd_med=$(awk -v a="$cmed" -v b="$base_med" 'BEGIN{ if(b>0) printf "%.4f", a/b; else print "0" }')
    csd_avg=$(awk -v a="$cavg" -v b="$base_avg" 'BEGIN{ if(b>0) printf "%.4f", a/b; else print "0" }')
    echo "$v,\"$best_atks\",$base_med,$cmed,$csd_med,$csd_avg" >> "$CHAMP"
    echo "  $v champion: $best_atks  -> ${csd_med}x median (N=$NCONF)"
done

echo "=== hunt done ==="
echo "all rows : $CSV"
echo "champions: $CHAMP"
