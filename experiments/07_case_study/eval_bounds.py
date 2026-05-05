#!/usr/bin/env python3
"""Retroactively re-evaluate E7 deadline misses under different bound choices.

Given an observed E7 case-study CSV and one or more bound CSVs, prints a
comparison table of deadline-miss rates per victim and overall, plus the
minimum per-bench safety factor needed to achieve 0 miss.
"""
from __future__ import annotations
import argparse, csv, sys
from collections import defaultdict
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--observed", required=True,
                    help="e7_case_study.csv with t_s,victim,c_obs,rampart_full,deadline_miss")
    ap.add_argument("--bounds", action="append", default=[],
                    help="extra bounds csv: NAME=PATH:COL  e.g. safe=e3_bounds_safe.csv:rampart_full_safe")
    a = ap.parse_args()

    rows = []
    with open(a.observed) as f:
        for r in csv.DictReader(f):
            try:
                r["c_obs"] = float(r["c_obs"])
                rows.append(r)
            except (KeyError, ValueError):
                continue
    if not rows:
        print("[err] observed csv empty"); return 1

    by_v: dict[str, list[float]] = defaultdict(list)
    for r in rows:
        by_v[r["victim"]].append(r["c_obs"])

    # baseline = original rampart_full from the observed file
    bound_sets = {"baseline": {v: float(next(r["rampart_full"] for r in rows
                                              if r["victim"] == v))
                                for v in by_v}}

    for spec in a.bounds:
        name, rest = spec.split("=", 1)
        path, col = rest.rsplit(":", 1)
        d = {}
        with open(path) as f:
            for r in csv.DictReader(f):
                if col in r and r["bench"] in by_v:
                    d[r["bench"]] = float(r[col])
        bound_sets[name] = d

    victims = sorted(by_v.keys())
    headers = ["victim", "n", "max_obs"] + [
        f"miss_{n}%" for n in bound_sets] + ["rho_min_for_0miss"]
    print(f"{'victim':10s} {'n':>5s} {'max_obs':>14s}", end="")
    for n in bound_sets:
        print(f" {('miss_'+n+'%'):>14s}", end="")
    print(f" {'rho_min_for_0miss':>18s}")

    totals = {n: [0, 0] for n in bound_sets}  # miss, total
    rho_min_global = 1.0
    for v in victims:
        xs = by_v[v]
        n = len(xs); mx = max(xs)
        line = f"{v:10s} {n:5d} {mx:14.1f}"
        for nm, dct in bound_sets.items():
            b = dct.get(v, float("inf"))
            mc = sum(1 for x in xs if x > b)
            totals[nm][0] += mc; totals[nm][1] += n
            line += f" {mc/n*100:14.2f}"
        # min rho relative to baseline rampart_full to give 0 miss
        ramp_base = bound_sets["baseline"][v]
        rho_need = max(1.0, mx / ramp_base) if ramp_base > 0 else float("inf")
        rho_min_global = max(rho_min_global, rho_need)
        line += f" {rho_need:18.4f}"
        print(line)

    print("-" * len(headers) * 12)
    line = f"{'TOTAL':10s} {sum(t[1] for t in totals.values())//len(totals):5d} {'':>14s}"
    for nm, (m, n) in totals.items():
        line += f" {m/n*100:14.2f}"
    line += f" {rho_min_global:18.4f}"
    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
