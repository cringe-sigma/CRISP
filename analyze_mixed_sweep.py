#!/usr/bin/env python3
"""Analyze results/mixed/sweep_summary.csv and report:
   * top-K mixes per victim (by median slowdown)
   * global champion victim/mix pairs
   * which mix wins most often
"""
import csv, sys
from collections import defaultdict

PATH = sys.argv[1] if len(sys.argv) > 1 else "results/mixed/sweep_summary.csv"
TOPK = int(sys.argv[2]) if len(sys.argv) > 2 else 3

rows = []
with open(PATH) as f:
    rdr = csv.DictReader(f)
    for r in rdr:
        try:
            r["sd_med"] = float(r["slowdown_med"])
            r["sd_avg"] = float(r["slowdown_avg"])
        except ValueError:
            continue
        rows.append(r)

per_victim = defaultdict(list)
for r in rows:
    per_victim[r["victim"]].append(r)

print(f"=== Top-{TOPK} mixes per victim (by median slowdown) ===")
print(f"{'victim':18s} {'mix':14s} {'attackers':40s} {'sd_med':>7s} {'sd_avg':>7s}")
champion = None
for v, lst in sorted(per_victim.items()):
    lst.sort(key=lambda r: -r["sd_med"])
    for r in lst[:TOPK]:
        print(f"{v:18s} {r['mix']:14s} {r['attackers'][:40]:40s} "
              f"{r['sd_med']:7.3f} {r['sd_avg']:7.3f}")
        if champion is None or r["sd_med"] > champion["sd_med"]:
            champion = r
    print()

print("=== GLOBAL CHAMPION (highest median slowdown over all victims) ===")
if champion:
    print(f"  victim    : {champion['victim']}")
    print(f"  mix       : {champion['mix']}")
    print(f"  attackers : {champion['attackers']}")
    print(f"  baseline  : {champion['baseline_cycles_med']} cycles (med)")
    print(f"  with bg   : {champion['bg_cycles_med']} cycles (med)")
    print(f"  slowdown  : {champion['sd_med']}x median  ({champion['sd_avg']}x avg)")

print()
print("=== Win counts per mix (#victims where this mix is top-1) ===")
wins = defaultdict(int)
for v, lst in per_victim.items():
    lst.sort(key=lambda r: -r["sd_med"])
    if lst:
        wins[lst[0]["mix"]] += 1
for mix, n in sorted(wins.items(), key=lambda kv: -kv[1]):
    print(f"  {mix:18s} {n}")
