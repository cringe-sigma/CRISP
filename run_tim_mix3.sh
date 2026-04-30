#!/usr/bin/env bash
# Fine-grained MEM parameter sweep around the 1.97x champion.
#
# Phase A: dense PAGE_SIZE x ITER grid at SIZE_MB=8, topology MEM x3.
# Phase B: SIZE_MB grid at the best PAGE_SIZE (from phase A), topology MEM x3.
# Phase C: re-run top variant from A with topology x2/x3/x4 to confirm.
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
OUTDIR=results/tim_mix3
mkdir -p "$OUTDIR"
DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'

extract_median_cycles() { awk '/^  cycles / { print $5 }' "$1"; }

declare -A SOLO_MED
build_and_baseline() {
    local extra="$1"
    rm -f build/MEM/MEM.o build/MEM.o multi_proc_pmu
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

# ---- Phase A: dense PAGE_SIZE x ITER grid at SIZE_MB=8 ----
# PAGE_SIZE: focus on 512..16384 where the max was around 1024
PAGES=(256 512 768 1024 1536 2048 3072 4096 8192 16384)
ITERS=(2 4 8 16)

echo "###############################################"
echo "# Phase A: SIZE_MB=8, PAGE_SIZE x ITER grid, topology MEM x3"
echo "###############################################"
for ps in "${PAGES[@]}"; do
    for it in "${ITERS[@]}"; do
        tag="A_p${ps}_i${it}"
        defs="-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE=${ps} -DMEM_OP_RATIO=2521 -DMEM_ITER=${it}"
        echo "-- Build $tag : $defs --"
        build_and_baseline "$defs"
        run_mix "$tag" MEM MEM MEM
    done
done

# ---- Phase B: SIZE_MB sweep at the best PAGE_SIZE/ITER from A ----
TOP=$(python3 - <<'PY'
import csv
with open("results/tim_mix3/summary.csv") as f:
    rdr = csv.reader(f); hdr = next(rdr)
    bs = hdr.index("binarysearch")
    best = None
    for r in rdr:
        if not r[0].startswith("A_"): continue
        try: x = float(r[bs])
        except ValueError: continue
        if best is None or x > best[1]: best = (r[0], x)
print(best[0])
PY
)
echo "Top phase-A config (binarysearch): $TOP"
PS=$(echo "$TOP" | sed -n 's/^A_p\([0-9]*\)_i.*/\1/p')
IT=$(echo "$TOP" | sed -n 's/^A_p[0-9]*_i\([0-9]*\)/\1/p')
echo "Best PAGE_SIZE=$PS  ITER=$IT"

SIZES=(2 4 6 8 10 12 16 24 32 48 64)
echo "###############################################"
echo "# Phase B: SIZE_MB sweep at p$PS i$IT, topology MEM x3"
echo "###############################################"
for sz in "${SIZES[@]}"; do
    tag="B_sz${sz}_p${PS}_i${IT}"
    defs="-DMEM_SIZE_MB=${sz} -DMEM_PAGE_SIZE=${PS} -DMEM_OP_RATIO=2521 -DMEM_ITER=${IT}"
    echo "-- Build $tag --"
    build_and_baseline "$defs"
    run_mix "$tag" MEM MEM MEM
done

# ---- Phase C: pick global top, vary topology ----
TOP2=$(python3 - <<'PY'
import csv
with open("results/tim_mix3/summary.csv") as f:
    rdr = csv.reader(f); hdr = next(rdr)
    bs = hdr.index("binarysearch")
    best = None
    for r in rdr:
        if r[0].startswith("C_"): continue
        try: x = float(r[bs])
        except ValueError: continue
        if best is None or x > best[1]: best = (r[0], x)
print(best[0])
PY
)
echo "Global top (A or B): $TOP2"
# Re-derive defs
if [[ "$TOP2" == A_* ]]; then
    PSx=$(echo "$TOP2" | sed -n 's/^A_p\([0-9]*\)_i.*/\1/p')
    ITx=$(echo "$TOP2" | sed -n 's/^A_p[0-9]*_i\([0-9]*\)/\1/p')
    SZx=8
else
    SZx=$(echo "$TOP2" | sed -n 's/^B_sz\([0-9]*\)_p.*/\1/p')
    PSx=$(echo "$TOP2" | sed -n 's/^B_sz[0-9]*_p\([0-9]*\)_i.*/\1/p')
    ITx=$(echo "$TOP2" | sed -n 's/^B_sz[0-9]*_p[0-9]*_i\([0-9]*\)/\1/p')
fi
echo "Final defines: SIZE=$SZx PAGE=$PSx ITER=$ITx"
build_and_baseline "-DMEM_SIZE_MB=${SZx} -DMEM_PAGE_SIZE=${PSx} -DMEM_OP_RATIO=2521 -DMEM_ITER=${ITx}"

echo "###############################################"
echo "# Phase C: topology variations of best MEM config"
echo "###############################################"
TOPOS=(
  "C_x1   | MEM"
  "C_x2   | MEM MEM"
  "C_x3   | MEM MEM MEM"
  "C_x4   | MEM MEM MEM MEM"
  "C_x5   | MEM MEM MEM MEM MEM"
  "C_x6   | MEM MEM MEM MEM MEM MEM"
)
for entry in "${TOPOS[@]}"; do
    IFS='|' read -r tag bg <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    bg="$(echo -n "$bg" | xargs)"
    # shellcheck disable=SC2206
    bg_args=( $bg )
    run_mix "$tag" "${bg_args[@]}"
done

# ---- Restore default ----
rm -f build/MEM/MEM.o build/MEM.o multi_proc_pmu
make -s >/dev/null

echo
echo "== Worst attack per victim =="
python3 - <<'PY'
import csv
with open("results/tim_mix3/summary.csv") as f:
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
