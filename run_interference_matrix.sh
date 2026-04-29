#!/usr/bin/env bash
# Run interference matrix: 6 benches x (solo + 6 co-runners) x N samples.
# Output written to results/ and summarized at the end.
set -u

BENCHES=(adpcm_dec binarysearch fir2dim fmref iir statemate)
SAMPLES=10
FREQ=1600000
OUTDIR=results
mkdir -p "$OUTDIR"

# refresh sudo
sudo -v

total=$(( ${#BENCHES[@]} * (1 + ${#BENCHES[@]}) ))
done=0

# 1) Solo runs
for A in "${BENCHES[@]}"; do
    out="$OUTDIR/solo_${A}.txt"
    if [[ -s "$out" ]]; then
        done=$((done+1)); continue
    fi
    echo "[$((++done))/$total] SOLO  cpu0=$A"
    sudo ./multi_proc_pmu -n "$SAMPLES" -f "$FREQ" "$A" > "$out" 2>&1
done

# 2) Pair runs: cpu0=A, cpu1..cpu3 = B
for A in "${BENCHES[@]}"; do
    for B in "${BENCHES[@]}"; do
        out="$OUTDIR/pair_${A}_vs_${B}.txt"
        if [[ -s "$out" ]]; then
            done=$((done+1)); continue
        fi
        echo "[$((++done))/$total] PAIR  cpu0=$A   cpu1..3=$B"
        sudo ./multi_proc_pmu -n "$SAMPLES" -f "$FREQ" "$A" "$B" "$B" "$B" > "$out" 2>&1
    done
done

echo "All runs complete. Results in $OUTDIR/"
