#!/usr/bin/env python3
"""Parse results/ files and compute slowdown matrix.
Slowdown = avg_cycles(co-run) / avg_cycles(solo) for the same measured bench.
"""
import os, re, sys

BENCHES = ["adpcm_dec", "binarysearch", "fir2dim", "fmref", "iir", "statemate"]
RES = "results"

cycles_re = re.compile(r"^\s*cycles\s+\S+\s+\S+\s+(\S+)\s+\S+", re.M)
inst_re   = re.compile(r"^\s*instructions\s+\S+\s+\S+\s+(\S+)\s+\S+", re.M)
miss_re   = re.compile(r"^\s*cache-misses\s+\S+\s+\S+\s+(\S+)\s+\S+", re.M)

sample_re = re.compile(
    r"\[sample=\d+\][^\n]*cycles=(\d+)\s+instructions=(\d+)\s+cache-misses=(\d+)")

def parse(path):
    with open(path) as f:
        t = f.read()
    mc = cycles_re.search(t); mi = inst_re.search(t); mm = miss_re.search(t)
    if mc and mi and mm:
        return float(mc.group(1)), float(mi.group(1)), float(mm.group(1))
    # Fallback: average per-sample lines (handles truncated runs).
    samples = sample_re.findall(t)
    if not samples:
        raise RuntimeError(f"no data in {path}")
    cs = [int(s[0]) for s in samples]
    ins = [int(s[1]) for s in samples]
    ms = [int(s[2]) for s in samples]
    n = len(cs)
    print(f"[warn] {path}: only {n} sample(s); using per-sample average",
          file=sys.stderr)
    return sum(cs)/n, sum(ins)/n, sum(ms)/n

solo = {b: parse(f"{RES}/solo_{b}.txt") for b in BENCHES}
pair = {(a, b): parse(f"{RES}/pair_{a}_vs_{b}.txt")
        for a in BENCHES for b in BENCHES}

print("=" * 92)
print("SOLO baseline (cpu0 only, cpu1..cpu3 idle)")
print("=" * 92)
print(f"{'bench':<12}{'avg_cycles':>15}{'avg_instr':>15}{'avg_miss':>12}{'IPC':>10}")
for b in BENCHES:
    c, i, m = solo[b]
    print(f"{b:<12}{c:>15.1f}{i:>15.1f}{m:>12.2f}{i/c:>10.3f}")

print()
print("=" * 92)
print("SLOWDOWN MATRIX  (avg cycles co-run / avg cycles solo)")
print("rows = measured bench on cpu0;  cols = co-runner on cpu1..cpu3")
print("=" * 92)
tag = 'cpu0\\co'
hdr = f"{tag:<12}" + "".join(f"{b:>10}" for b in BENCHES) + f"{'WORST':>11}{'BY':>11}"
print(hdr)
worst_global = (None, None, 0.0)
for a in BENCHES:
    row = []
    for b in BENCHES:
        s = pair[(a, b)][0] / solo[a][0]
        row.append(s)
    wmax = max(row); wb = BENCHES[row.index(wmax)]
    if wmax > worst_global[2]:
        worst_global = (a, wb, wmax)
    line = f"{a:<12}" + "".join(f"{s:>10.3f}" for s in row) + f"{wmax:>11.3f}{wb:>11}"
    print(line)

print()
print("=" * 92)
print("CACHE-MISS RATIO MATRIX  (avg misses co-run / avg misses solo)")
print("=" * 92)
print(hdr.replace("WORST","WORST").replace("BY","BY"))
for a in BENCHES:
    row = []
    for b in BENCHES:
        ms = solo[a][2]
        s = pair[(a, b)][2] / ms if ms > 0 else 0
        row.append(s)
    wmax = max(row); wb = BENCHES[row.index(wmax)]
    line = f"{a:<12}" + "".join(f"{s:>10.2f}" for s in row) + f"{wmax:>11.2f}{wb:>11}"
    print(line)

print()
print("=" * 92)
A, B, R = worst_global
print(f"GLOBAL MAX SLOWDOWN: cpu0={A}, co-runners={B}  ->  {R:.3f}x")
print("=" * 92)
print(f"\nDetails of the worst case ({A} vs {B}):")
print(f"  solo  : cycles={solo[A][0]:.1f}  instr={solo[A][1]:.1f}  cache-miss={solo[A][2]:.2f}")
co = pair[(A,B)]
print(f"  co-run: cycles={co[0]:.1f}  instr={co[1]:.1f}  cache-miss={co[2]:.2f}")
print(f"  delta : cycles x{co[0]/solo[A][0]:.3f}  instr x{co[1]/solo[A][1]:.3f}  cache-miss x{co[2]/solo[A][2]:.2f}")
