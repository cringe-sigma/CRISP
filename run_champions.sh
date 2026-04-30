#!/usr/bin/env bash
# Reproduce the strongest attacks discovered across all sweeps.
#
# For each "champion" entry below we:
#   1) Rebuild the harness with that entry's exact -D defines.
#   2) Measure each victim solo (with this build) and paired with the
#      attacker workers, both at -n SAMPLES -f FREQ_KHZ on cpu0/cpu1..cpuN.
#   3) Append a row to results/champions/summary.csv.
#
# Finally we render a self-contained ASCII table showing both the BUILD
# parameters and the per-victim ratios.
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
OUTDIR=results/champions
mkdir -p "$OUTDIR"
DEFAULT_CFLAGS='-O0 -g -Wall -Wno-unused-result -Wno-unknown-pragmas -std=gnu11'

extract_median_cycles() { awk '/^  cycles / { print $5 }' "$1"; }

# Tuned per-bench winners from results/tim_others (used to compose ALL_BEST).
DEFS_CACHE_TUNED='-DCACHE_MEM_SIZE=4194304 -DCACHE_STRIDE=64 -DCACHE_RW_RATIO=50 -DCACHE_FR_RATIO=50 -DCACHE_ITER=1'
DEFS_BUS_TUNED='-DBUS_SIZE_MB=4 -DBUS_DATA_TYPE=8 -DBUS_DIR_RATIO=333 -DBUS_CPU_RATIO=100 -DBUS_ITER=4'
DEFS_POINTER_TUNED='-DPOINTER_ELEMENTS=65536u -DPOINTER_STRIDE=1u -DPOINTER_LOAD_RATIO=100 -DPOINTER_ITER=16384 -DPOINTER_CACHE_LINE=64'
DEFS_PIPELINE_TUNED='-DPIPELINE_RATIO=50 -DPIPELINE_PRECISION=6 -DPIPELINE_ITER=4096'
DEFS_MEM_TUNED='-DMEM_SIZE_MB=8 -DMEM_PAGE_SIZE=16384 -DMEM_OP_RATIO=2521 -DMEM_ITER=16'

ALL_BEST="$DEFS_CACHE_TUNED $DEFS_BUS_TUNED $DEFS_POINTER_TUNED $DEFS_PIPELINE_TUNED $DEFS_MEM_TUNED"

# ----------- Champion list -----------
# Format (3 lines per entry):
#   tag                         (short label)
#   bg_workers                  (positional bench list given to multi_proc_pmu)
#   defines                     (CFLAGS -D... used to rebuild)
#   reason                      (one-line rationale, source sweep)
CHAMPIONS=(
  "INSERTSORT_KILLER  | CACHE BUS MEM                 | $ALL_BEST                                                              | tim_others X_CACHE_BUS_MEM   (insertsort 8.43x)"
  "BS_POINTER_X3      | POINTER POINTER POINTER       | $DEFS_POINTER_TUNED                                                    | tim_others PA_64K_s1_l100    (binarysearch 7.07x)"
  "MEM_BUS_X2_BS      | MEM BUS BUS                   | $ALL_BEST                                                              | tim_others X_MEM_BUS_x2      (binarysearch 6.47x)"
  "POINTER_X3_FIR     | POINTER POINTER POINTER       | -DPOINTER_ELEMENTS=65536u -DPOINTER_STRIDE=17u -DPOINTER_LOAD_RATIO=0 -DPOINTER_ITER=16384 -DPOINTER_CACHE_LINE=64 | tim_others PA_64K_s17_l0  (insertsort 5.57x, fir2dim 2.82x)"
  "IIR_ONE_EACH_MEM   | MEM CACHE BUS POINTER         | $ALL_BEST                                                              | tim_others X_one_each_MEM    (iir 4.69x)"
  "BUS_X3_BS          | BUS BUS BUS                   | $DEFS_BUS_TUNED                                                        | tim_others X_BUS_x3          (binarysearch 4.44x)"
  "MEM_X3_p16K_i16    | MEM MEM MEM                   | $DEFS_MEM_TUNED                                                        | tim_mix3 A_p16384_i16        (insertsort 3.12x)"
  "BS_MEM_2x_p2048    | MEM MEM MEM                   | -DMEM_SIZE_MB=16 -DMEM_PAGE_SIZE=2048 -DMEM_OP_RATIO=2521 -DMEM_ITER=2 | tim_mix3 B_sz16_p2048_i2     (binarysearch 2.05x)"
  "MEM_X3_BUS         | MEM MEM MEM BUS               | $ALL_BEST                                                              | tim_others X_MEM_x3_BUS      (bs 2.32x, iir 2.06x)"
)

# Build the meta-CSV. Columns: tag + ratios.
SUMMARY="$OUTDIR/summary.csv"
{
    printf 'tag'
    for v in "${VICTIMS[@]}"; do printf ',%s' "$v"; done
    printf '\n'
} >"$SUMMARY"

# Detail TSV with parameters too.
DETAIL="$OUTDIR/detail.tsv"
{
    printf 'tag\tbg_workers\tdefines\tnote'
    for v in "${VICTIMS[@]}"; do printf '\tsolo_%s\tpair_%s\tratio_%s' "$v" "$v" "$v"; done
    printf '\n'
} >"$DETAIL"

run_champion() {
    local tag="$1"; local bg="$2"; local defs="$3"; local note="$4"

    rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/MEM/MEM.o \
          build/POINTER/POINTER.o build/PIPELINE/PIPELINE.o \
          build/CACHE.o build/BUS.o build/MEM.o build/POINTER.o build/PIPELINE.o \
          multi_proc_pmu
    make -s CFLAGS="$DEFAULT_CFLAGS $defs" >/dev/null

    echo
    echo "================================================================"
    echo "== $tag"
    echo "==   bg     : $bg"
    echo "==   defs   : $defs"
    echo "==   note   : $note"
    echo "================================================================"
    printf '%-14s %16s %16s %10s\n' "victim" "solo" "pair" "ratio"
    printf '%-14s %16s %16s %10s\n' "------" "----" "----" "-----"

    # shellcheck disable=SC2206
    local bg_args=( $bg )
    local csv_line="$tag"
    local detail_line="$tag"$'\t'"$bg"$'\t'"$defs"$'\t'"$note"
    for v in "${VICTIMS[@]}"; do
        solo_log="$OUTDIR/solo_${v}_${tag}.txt"
        pair_log="$OUTDIR/pair_${v}_${tag}.txt"
        "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$v" >"$solo_log" 2>&1 || true
        "$BIN" -n "$SAMPLES" "${FREQ_OPT[@]}" "$v" "${bg_args[@]}" >"$pair_log" 2>&1 || true
        local solo pair ratio
        solo=$(extract_median_cycles "$solo_log")
        pair=$(extract_median_cycles "$pair_log")
        [[ -z "$solo" ]] && solo="N/A"
        [[ -z "$pair" ]] && pair="N/A"
        ratio=$(awk -v s="$solo" -v p="$pair" \
                'BEGIN{ if (s+0>0 && p+0>0) printf "%.3f", p/s; else print "N/A"; }')
        printf '%-14s %16s %16s %10s\n' "$v" "$solo" "$pair" "$ratio"
        csv_line+=",$ratio"
        detail_line+=$'\t'"$solo"$'\t'"$pair"$'\t'"$ratio"
    done
    echo "$csv_line" >>"$SUMMARY"
    echo "$detail_line" >>"$DETAIL"
}

for entry in "${CHAMPIONS[@]}"; do
    IFS='|' read -r tag bg defs note <<<"$entry"
    tag="$(echo -n "$tag" | xargs)"
    bg="$(echo -n "$bg" | xargs)"
    defs="$(echo -n "$defs" | xargs)"
    note="$(echo -n "$note" | xargs)"
    run_champion "$tag" "$bg" "$defs" "$note"
done

# Restore default
echo
echo "== Restoring default configuration =="
rm -f build/CACHE/CACHE.o build/BUS/BUS.o build/MEM/MEM.o build/POINTER/POINTER.o build/PIPELINE/PIPELINE.o multi_proc_pmu
make -s >/dev/null

# ----- Render pretty tables -----
echo
echo "================ Champion ratio table ================"
column -t -s, "$SUMMARY"

echo
echo "================ Champion details (parameters) ================"
python3 - "$DETAIL" <<'PY'
import sys, textwrap
fp = sys.argv[1]
with open(fp) as f:
    rows = [line.rstrip('\n').split('\t') for line in f]
hdr, rows = rows[0], rows[1:]

# Per-bench attacker -D parsing for nice display
def parse_defs(defs):
    fams = {"CACHE":[], "BUS":[], "MEM":[], "POINTER":[], "PIPELINE":[]}
    for tok in defs.split():
        if not tok.startswith("-D"):
            continue
        body = tok[2:]
        for k in fams:
            if body.startswith(k+"_"):
                fams[k].append(body)
                break
    return fams

# Print one block per champion.
for r in rows:
    tag, bg, defs, note = r[0], r[1], r[2], r[3]
    print(f"\n--- {tag} ---")
    print(f"  workers : {bg}")
    print(f"  source  : {note}")
    fams = parse_defs(defs)
    for k, lst in fams.items():
        used = any(name == k or name.startswith(k+" ") or (" "+k+" ") in (" "+bg+" ") for name in [k])
        marker = " *" if k in bg.split() else "  "
        if lst:
            print(f"  {marker}{k:<8}: " + ", ".join(p.split('=',1)[1] if '=' in p else p
                                                   for p in
                                                   [t.replace(k+'_','',1) for t in lst]))
    # ratios
    headers = hdr[4:]  # solo_X, pair_X, ratio_X triples
    vics = []
    for i in range(0, len(headers), 3):
        vics.append(headers[i].replace("solo_",""))
    parts = []
    vals = r[4:]
    for i, v in enumerate(vics):
        solo = vals[3*i]; pair = vals[3*i+1]; ratio = vals[3*i+2]
        parts.append(f"{v}={ratio}x")
    print("  ratios  : " + "  ".join(parts))

print("\n* = worker actually scheduled (others are inert: their -D defines were\n"
      "    compiled in but the bench wasn't started by this run).\n")
PY

echo
echo "Per-run logs: $OUTDIR/  ;  CSV: $SUMMARY  ;  TSV: $DETAIL"
